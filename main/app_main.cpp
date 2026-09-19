#include "app_main.h"

#include <inttypes.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"

#include "app_config.h"
#include "matter_setup.h"
#include "button.h"
#include "board_pins.h"
#include "status_led.h"
#include "relay.h"
#include "bl0937.h"

static const char *TAG = "app_main";

// Boot event bits — set by subsystems when they reach a ready state.
#define BOOT_BIT_COMMISSIONED  (1 << 0)
#define BOOT_BIT_SERVER_READY  (1 << 1)

static EventGroupHandle_t g_boot_events = NULL;
static TimerHandle_t      g_meter_poll_timer = NULL;

static void app_init();
static bool run_commissioning();

// ── Button callback (runs in the button task context) ───────────────────────

static void on_button_long_press(void)
{
    ESP_LOGW(TAG, "Factory reset!");
    matter_factory_reset();
}

static void on_button_short_press(void)
{
    // Toggles the relay via the OnOff cluster (matter_setup.cpp's
    // attr_update_cb then drives RelaySet() from the resulting attribute
    // write), so a controller subscriber sees the change the same way it
    // would from its own write.
    matter_button_toggle();
}

// Onboard BOOT button (GPIO9) — bench/dev convenience only. It's the XIAO
// module's own button, unreachable once the plug is closed up, so it only
// gets the long-press factory-reset action; the plug's own button remains
// the real user-facing control (short press too, via on_button_short_press).
static void on_boot_button_long_press(void)
{
    ESP_LOGW(TAG, "Factory reset via onboard BOOT button!");
    matter_factory_reset();
}

// ── Meter poll timer (runs in the FreeRTOS timer service task) ─────────────

static void meter_poll_timer_cb(TimerHandle_t)
{
    MeterPoll();
}

// Routes the XIAO's radio to its onboard ceramic antenna rather than the
// unpopulated U.FL connector. Runs before the Matter stack brings the radio up.
static void rf_antenna_init()
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_RF_SWITCH_EN) | (1ULL << PIN_RF_ANT_SELECT),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_set_level(PIN_RF_SWITCH_EN, 0));   // enable RF switch
    ESP_ERROR_CHECK(gpio_set_level(PIN_RF_ANT_SELECT, 0));  // onboard antenna

    ESP_LOGI(TAG, "RF switch enabled, onboard antenna selected");
}

// One-time hardware and subsystem initialisation.
static void app_init()
{
    g_boot_events = xEventGroupCreate();
    configASSERT(g_boot_events);

    // Before matter_setup() — the radio must not come up on the wrong antenna.
    rf_antenna_init();

    status_led_init();
    status_led_set(STATUS_LED_BOOT);

    // Before the Matter stack starts, so the load is guaranteed off until
    // something explicitly switches it on (matter_setup()'s post-start
    // restore, or a button/controller write).
    RelayInit();

    matter_setup(g_boot_events, BOOT_BIT_COMMISSIONED, BOOT_BIT_SERVER_READY);

    // Interrupt-driven plug button — short press toggles the relay via the
    // OnOff cluster, long hold factory-resets.
    button_init(PIN_BUTTON, on_button_long_press, on_button_short_press);

    // Onboard BOOT button — a second, bench-only long-press factory-reset
    // trigger for when the plug enclosure isn't open (see PIN_BOOT_BUTTON).
    button_init(PIN_BOOT_BUTTON, on_boot_button_long_press, NULL);

    // BL0937 energy meter. Poll rate is part of the calibration -- see
    // METER_POLL_INTERVAL_MS in app_config.h.
    MeterInit();
    g_meter_poll_timer = xTimerCreate("meter_poll", pdMS_TO_TICKS(METER_POLL_INTERVAL_MS),
                                       pdTRUE, NULL, meter_poll_timer_cb);
    if (g_meter_poll_timer)
        xTimerStart(g_meter_poll_timer, 0);
    else
        ESP_LOGE(TAG, "Failed to create meter poll timer — metering disabled");
}

// Run the commissioning flow when the device is not yet paired. Blocks until
// MATTER_COMMISSIONING_COMPLETE is signalled via the boot EventGroup (fabric
// committed to NVS). Returns true if a fresh commission happened this boot.
static bool run_commissioning()
{
    if (matter_is_commissioned())
        return false;

    status_led_set(STATUS_LED_COMMISSIONING);

    char qr_buf[MATTER_QR_BUF_LEN]        = {};
    char code_buf[MATTER_MANUAL_CODE_LEN] = {};
    matter_get_pairing_codes(qr_buf, sizeof(qr_buf), code_buf, sizeof(code_buf));

    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "  MATTER COMMISSIONING — device not yet paired (Thread)");
    ESP_LOGI(TAG, "  Pair over BLE; a Thread Border Router must be present.");
    ESP_LOGI(TAG, "  Manual pairing code : %s", code_buf);
    ESP_LOGI(TAG, "  QR payload          : %s", qr_buf);
    ESP_LOGI(TAG, "============================================================");

    // Wait for MATTER_COMMISSIONING_COMPLETE (fabric committed to NVS), not just
    // isDeviceCommissioned() which fires ~5 s earlier on "fabric updated" before
    // the NVS flush. HA opens a second commissioning window if we proceed too early.
    // 10-minute guard prevents an indefinite hang if the event is never delivered.
    const TickType_t timeout = pdMS_TO_TICKS(10UL * 60UL * 1000UL);
    EventBits_t bits = xEventGroupWaitBits(g_boot_events, BOOT_BIT_COMMISSIONED,
                                            pdFALSE, pdTRUE, timeout);
    if (!(bits & BOOT_BIT_COMMISSIONED))
    {
        ESP_LOGE(TAG, "Commissioning timeout — restarting to retry");
        status_led_set(STATUS_LED_ERROR);
        esp_restart();
    }

    ESP_LOGI(TAG, "Commissioning complete — joining Thread, staying live for controller interview");
    return true;
}

// ── Exposed to bl0937.cpp for the over-power trip (see app_main.h) ─────────

extern "C" void AppUpdateOnOffCluster(void)
{
    matter_update_onoff();
}

extern "C" void app_main(void)
{
    esp_log_level_set("BLE_INIT", ESP_LOG_WARN);

    ESP_LOGI(TAG, "=== CB2S power plug (XIAO ESP32-C6) boot ===");

    app_init();

    run_commissioning();

    // Wait for the Matter server to finish init (kServerReady) — attribute
    // writes before this point return INVALID_STATE.
    xEventGroupWaitBits(g_boot_events, BOOT_BIT_SERVER_READY,
                        pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));

    // Every boot, not just a freshly-commissioned one: an already-paired device
    // returns early from run_commissioning() and would otherwise sit on the
    // boot indication forever. This also clears the commissioning blink's hold
    // on the onboard LED, handing it back to relay state.
    status_led_set(STATUS_LED_OK);

    ESP_LOGI(TAG, "Ready — button toggles the relay, metering reports over Matter");
}
