#include "matter_setup.h"

#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_matter.h"
#include "esp_matter_endpoint.h"
#include "esp_matter_cluster.h"
#include "esp_matter_attribute.h"
#include "freertos/portmacro.h"

#include "app_config.h"
#include "power_measurement.h"
#include "relay.h"
#include "status_led.h"

// Thread (C6) transport: the CHIP OpenThread launcher asserts unless it has been
// handed a platform config before esp_matter::start() brings the Thread stack up.
// Same pattern as esp32c6-radar-demo-matter/main/matter_setup.cpp.
#if CONFIG_OPENTHREAD_ENABLED
#include "esp_openthread.h"
#include "esp_openthread_types.h"
#include <platform/ESP32/OpenthreadLauncher.h>

#define OT_DEFAULT_RADIO_CONFIG()                                                    \
    {                                                                                \
        .radio_mode = RADIO_MODE_NATIVE,                                             \
    }
#define OT_DEFAULT_HOST_CONFIG()                                                     \
    {                                                                                \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,                           \
    }
#define OT_DEFAULT_PORT_CONFIG()                                                     \
    {                                                                                \
        .storage_partition_name = "nvs", .netif_queue_size = 10,                     \
        .task_queue_size = 10,                                                        \
    }
#endif // CONFIG_OPENTHREAD_ENABLED

// Matter/CHIP stack headers
#include <app/server/Server.h>
#include <app/server/CommissioningWindowManager.h>
#include <lib/core/CHIPError.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <system/SystemClock.h>
#include <platform/CHIPDeviceLayer.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app-common/zap-generated/ids/Attributes.h>

static const char *TAG = "matter_setup";
// Commissioning window timeout (s)
static constexpr uint16_t COMMISSIONING_WINDOW_TIMEOUT_S = 300;

// Sentinel for "endpoint not yet created" — 0xFFFF is the Matter wildcard/invalid ID.
static constexpr uint16_t MATTER_EP_INVALID = 0xFFFFu;

// Endpoint ID, populated on create
static uint16_t s_ep_plug = MATTER_EP_INVALID; // on_off_plug_in_unit (relay + BL0937 metering)

static EventGroupHandle_t s_boot_events      = NULL;
static EventBits_t        s_commissioned_bit = 0;
static EventBits_t        s_server_ready_bit = 0;

// Pairing code buffers written from the Matter event callback
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static char  s_qr_code[MATTER_QR_BUF_LEN]         = {};
static char  s_manual_code[MATTER_MANUAL_CODE_LEN] = {};
static EventGroupHandle_t s_matter_events = NULL;
#define MATTER_BIT_CODES_READY (1 << 0)

// ── Helpers ───────────────────────────────────────────────────────────────────

static void open_commissioning_window(void)
{
    auto &mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (mgr.IsCommissioningWindowOpen())
        return;

    CHIP_ERROR err = mgr.OpenBasicCommissioningWindow(
        chip::System::Clock::Seconds16(COMMISSIONING_WINDOW_TIMEOUT_S),
        chip::CommissioningWindowAdvertisement::kAllSupported);
    if (err != CHIP_NO_ERROR)
        ESP_LOGE(TAG, "OpenBasicCommissioningWindow: %" CHIP_ERROR_FORMAT, err.Format());
}

static void refresh_pairing_codes(void)
{
    char manual[sizeof(s_manual_code)] = {};
    char qr[sizeof(s_qr_code)]         = {};

    chip::RendezvousInformationFlags flags(chip::RendezvousInformationFlag::kBLE);

    chip::MutableCharSpan manual_span(manual);
    if (GetManualPairingCode(manual_span, flags) == CHIP_NO_ERROR)
        manual[manual_span.size()] = '\0';

    chip::MutableCharSpan qr_span(qr);
    if (GetQRCode(qr_span, flags) == CHIP_NO_ERROR)
        qr[qr_span.size()] = '\0';

    portENTER_CRITICAL(&s_mux);
    memcpy(s_manual_code, manual, sizeof(s_manual_code));
    memcpy(s_qr_code, qr, sizeof(s_qr_code));
    portEXIT_CRITICAL(&s_mux);

    if (s_matter_events)
        xEventGroupSetBits(s_matter_events, MATTER_BIT_CODES_READY);

    ESP_LOGI(TAG, "Matter manual code: %s", manual);
    ESP_LOGI(TAG, "Matter QR payload : %s", qr);
}

// ── Matter event callback ───────────────────────────────────────────────────

static void matter_event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t /*arg*/)
{
    if (!event)
        return;

    switch (event->Type)
    {
    case chip::DeviceLayer::DeviceEventType::kServerReady:
        ESP_LOGI(TAG, "Matter server ready");
        if (s_boot_events && s_server_ready_bit)
            xEventGroupSetBits(s_boot_events, s_server_ready_bit);
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() > 0)
        {
            ESP_LOGI(TAG, "Device already commissioned");
            if (s_boot_events)
                xEventGroupSetBits(s_boot_events, s_commissioned_bit);
        }
        else
        {
            open_commissioning_window();
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        status_led_set(STATUS_LED_OK);
        if (s_boot_events)
            xEventGroupSetBits(s_boot_events, s_commissioned_bit);
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        status_led_set(STATUS_LED_COMMISSIONING);
        refresh_pairing_codes();
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(TAG, "Fail-safe expired — reopening commissioning window");
        open_commissioning_window();
        break;

    default:
        break;
    }
}

// ── OnOff <-> relay bridge ───────────────────────────────────────────────────
//
// Ported from uascent-matter/src/zcl_callbacks.cpp: attribute writes (from a
// controller, or from matter_button_toggle()/AppUpdateOnOffCluster() after a
// button press or over-power trip) drive RelaySet() through attr_update_cb
// below; matter_update_onoff() is the reverse direction, pushing the relay's
// actual state back into the cluster so a subscriber sees it regardless of
// what caused the change.

static void apply_onoff(bool on)
{
    RelaySet(on);
}

// ── Attribute update callback (fired by Matter stack on attribute writes) ───

static esp_err_t attr_update_cb(esp_matter::attribute::callback_type_t type,
                                 uint16_t endpoint_id, uint32_t cluster_id,
                                 uint32_t attribute_id, esp_matter_attr_val_t *val,
                                 void * /*priv_data*/)
{
    using namespace chip::app::Clusters;

    if (type != esp_matter::attribute::POST_UPDATE)
        return ESP_OK;

    if (endpoint_id == s_ep_plug && cluster_id == OnOff::Id &&
        attribute_id == OnOff::Attributes::OnOff::Id)
    {
        ESP_LOGI(TAG, "OnOff cluster: attribute OnOff set to %d", (int)val->val.b);
        apply_onoff(val->val.b);
    }

    return ESP_OK;
}

// ── Endpoint creation ────────────────────────────────────────────────────────

static esp_err_t create_endpoints(esp_matter::node_t *node)
{
    using namespace esp_matter;

    endpoint::on_off_plug_in_unit::config_t cfg = {};

    // Matter spec: StartUpOnOff of *null* means "restore the previous value
    // on power-up" — esp_matter defaults this to 0 ("come up Off"), which
    // would discard the persisted relay state on every power cycle. Same
    // trap the sibling esp32c6-radar-demo-matter project hit with its light
    // endpoint's StartUpOnOff/StartUpCurrentLevel.
    //
    // StartUpOnOff belongs to the OnOff cluster's Lighting feature, which
    // on_off_plug_in_unit_device.cpp adds unconditionally (even though this
    // is a plug, not a light) -- esp_matter's generated device type exposes
    // it as the separate on_off_lighting config field, not on_off itself
    // (that struct only carries the cluster's initial "on_off" bool).
    cfg.on_off_lighting.start_up_on_off = nullable<uint8_t>();

    endpoint_t *ep = endpoint::on_off_plug_in_unit::create(node, &cfg, ENDPOINT_FLAG_NONE, NULL);
    if (!ep)
    {
        ESP_LOGE(TAG, "on_off_plug_in_unit create failed");
        return ESP_FAIL;
    }
    s_ep_plug = endpoint::get_id(ep);

    // ElectricalPowerMeasurement is wired entirely through the CHIP SDK's own
    // cluster API (power_measurement.cpp's Instance/Delegate) -- its Init()
    // self-registers directly with the data-model provider registry, with no
    // esp_matter::cluster::electrical_power_measurement helper involved at
    // all. Deferred until after esp_matter::start() below: Instance::Init()
    // touches the attribute/reporting engine, which is only valid once the
    // data model has loaded.
    //
    // ElectricalEnergyMeasurement is different: esp_matter needs its own
    // cluster_t on this endpoint (electrical_energy_measurement::create()
    // below) so ESPMatterElectricalEnergyMeasurementClusterServerInitCallback
    // (registered as that cluster's init callback, fired automatically once
    // the data model loads) can find it via cluster::get() and construct the
    // real ElectricalEnergyMeasurementCluster. Feature bits mirror what
    // power_measurement.cpp used to pass to the now-unused
    // ElectricalEnergyMeasurementAttrAccess constructor: imported + cumulative
    // energy only -- this endpoint neither exports nor reports periodically,
    // and declaring those features would make CumulativeEnergyExported /
    // PeriodicEnergyImported mandatory attributes this endpoint doesn't have.
    {
        using namespace chip::app::Clusters::ElectricalEnergyMeasurement;
        cluster::electrical_energy_measurement::config_t eem_cfg = {};
        eem_cfg.feature_flags = cluster::electrical_energy_measurement::feature::imported_energy::get_id() |
                                 cluster::electrical_energy_measurement::feature::cumulative_energy::get_id();
        if (!cluster::electrical_energy_measurement::create(ep, &eem_cfg, CLUSTER_FLAG_SERVER))
        {
            ESP_LOGE(TAG, "electrical_energy_measurement cluster create failed");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Endpoint: plug=%u", s_ep_plug);
    return ESP_OK;
}

// ── Public API ────────────────────────────────────────────────────────────────

extern "C" void matter_setup(EventGroupHandle_t boot_events,
                              EventBits_t commissioned_bit,
                              EventBits_t server_ready_bit)
{
    s_boot_events      = boot_events;
    s_commissioned_bit = commissioned_bit;
    s_server_ready_bit = server_ready_bit;
    s_matter_events    = xEventGroupCreate();

    using namespace esp_matter;

    node::config_t node_cfg = {};
    node_t *node = node::create(&node_cfg, attr_update_cb, nullptr);
    if (!node)
    {
        ESP_LOGE(TAG, "node::create failed — restarting");
        esp_restart();
    }

    if (create_endpoints(node) != ESP_OK)
    {
        ESP_LOGE(TAG, "Endpoint creation failed — restarting");
        esp_restart();
    }

#if CONFIG_OPENTHREAD_ENABLED
    // Hand the OpenThread launcher its platform config before the stack starts,
    // otherwise openthread_init_stack() asserts on a null s_platform_config.
    static esp_openthread_platform_config_t ot_config = {
        .radio_config = OT_DEFAULT_RADIO_CONFIG(),
        .host_config  = OT_DEFAULT_HOST_CONFIG(),
        .port_config  = OT_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&ot_config);
#endif

    ESP_ERROR_CHECK(esp_matter::start(matter_event_cb));
    ESP_LOGI(TAG, "Matter stack started");

    // EPM/EEM init: only valid once the data model has loaded, i.e. after
    // esp_matter::start(). See uascent-matter/src/app_task.cpp's
    // mPostServerInitClbk comment for the same ordering requirement on Zephyr.
    //
    // Instance::Init() below calls into CHIP's data-model provider
    // (RegisterAttributeChangeListener et al.), which asserts the CHIP stack
    // lock is held by the caller (Provider.cpp's
    // assertChipStackLockedByCurrentThread()). Unlike esp_matter::attribute::
    // update()/get_val() (which take esp_matter's own lock::ScopedChipStackLock
    // internally), this raw CHIP SDK call does not -- app_main's task must take
    // the lock itself, or the Matter/CHIP main-loop task racing on the same
    // provider trips the "unsafe/racy" abort seen here.
    {
        chip::DeviceLayer::StackLock lock;
        if (PowerMeasurementInit(s_ep_plug) != CHIP_NO_ERROR)
            ESP_LOGE(TAG, "PowerMeasurementInit failed — metering will not report");
    }

    // esp_matter restores persisted attribute values from NVS into its
    // internal store on start(), but that restore does not go through
    // attr_update_cb (no POST_UPDATE fires), so RelaySet() would otherwise
    // never run and the relay would stay open across a power cycle
    // regardless of the persisted OnOff value. Pull the actual value back
    // out here, matching the sibling project's light-state restore and
    // uascent-matter's emberAfOnOffClusterInitCallback().
    {
        using namespace chip::app::Clusters;
        esp_matter_attr_val_t val = esp_matter_bool(false);
        if (attribute::get_val(s_ep_plug, OnOff::Id, OnOff::Attributes::OnOff::Id, &val) == ESP_OK)
            apply_onoff(val.val.b);
        ESP_LOGI(TAG, "Restored relay state: on=%d", (int)val.val.b);
    }

    // Name the board in the controller UI.
    {
        char label[33] = {};
        strncpy(label, BOARD_NODE_LABEL, sizeof(label) - 1);
        attribute_t *attr = attribute::get(
            0, chip::app::Clusters::BasicInformation::Id,
            chip::app::Clusters::BasicInformation::Attributes::NodeLabel::Id);
        if (attr)
        {
            esp_matter_attr_val_t val = esp_matter_char_str(label, strlen(label));
            attribute::update(0, chip::app::Clusters::BasicInformation::Id,
                              chip::app::Clusters::BasicInformation::Attributes::NodeLabel::Id, &val);
        }
    }
}

extern "C" bool matter_is_commissioned(void)
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

extern "C" void matter_update_onoff(void)
{
    using namespace esp_matter;
    using namespace chip::app::Clusters;

    if (s_ep_plug == MATTER_EP_INVALID)
        return;

    esp_matter_attr_val_t val = esp_matter_bool(RelayIsOn());
    attribute::update(s_ep_plug, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
}

extern "C" void matter_button_toggle(void)
{
    using namespace esp_matter;
    using namespace chip::app::Clusters;

    if (s_ep_plug == MATTER_EP_INVALID)
        return;

    esp_matter_attr_val_t val = esp_matter_bool(false);
    attribute::get_val(s_ep_plug, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
    val.val.b = !val.val.b;
    attribute::update(s_ep_plug, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);

    ESP_LOGI(TAG, "Relay toggled via button: %s", val.val.b ? "on" : "off");
}

extern "C" void matter_get_pairing_codes(char *qr_buf,  size_t qr_len,
                                          char *code_buf, size_t code_len)
{
    // Wait up to 30 s for the commissioning window to open and codes to be ready.
    if (s_matter_events)
    {
        xEventGroupWaitBits(s_matter_events, MATTER_BIT_CODES_READY,
                            pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));
    }

    portENTER_CRITICAL(&s_mux);
    if (qr_buf && qr_len > 0)
    {
        strlcpy(qr_buf, s_qr_code, qr_len);
    }
    if (code_buf && code_len > 0)
    {
        strlcpy(code_buf, s_manual_code, code_len);
    }
    portEXIT_CRITICAL(&s_mux);
}

extern "C" void matter_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset requested");
    esp_matter::factory_reset();
}
