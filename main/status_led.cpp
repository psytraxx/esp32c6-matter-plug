#include "status_led.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "board_pins.h"

static const char *TAG = "status_led";

namespace
{
TimerHandle_t sBlinkTimer;
bool sBlinkOn;

// The two inputs the onboard LED arbitrates between.
bool sCommissioning;
bool sRelayOn;

void SetLevel(bool on)
{
    // Active-low (tuya-config.json netled1_lv:0) — see status_led.h.
    gpio_set_level(PIN_LED, on ? 0 : 1);
}

// Active-low: the XIAO's user LED sinks through the pin, so 0 lights it.
void SetOnboardLevel(bool on)
{
    gpio_set_level(PIN_ONBOARD_LED, on ? 0 : 1);
}

// While the commissioning window is open the onboard LED blinks with the
// plug's LED; once paired it falls back to showing relay state, which is the
// only toggle feedback available on USB-only bench power (see relay.h).
void RefreshOnboard(void)
{
    SetOnboardLevel(sCommissioning ? sBlinkOn : sRelayOn);
}

void BlinkTimerCallback(TimerHandle_t)
{
    sBlinkOn = !sBlinkOn;
    SetLevel(sBlinkOn);
    RefreshOnboard();
}

void StopBlink(void)
{
    if (sBlinkTimer)
    {
        xTimerStop(sBlinkTimer, 0);
    }
}
} // namespace

void status_led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_LED) | (1ULL << PIN_ONBOARD_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    SetLevel(false);
    status_led_set_relay(false);

    sBlinkTimer = xTimerCreate("led_blink", pdMS_TO_TICKS(500), pdTRUE, NULL, BlinkTimerCallback);
    if (!sBlinkTimer)
    {
        ESP_LOGE(TAG, "Failed to create blink timer");
    }
}

void status_led_set_relay(bool on)
{
    sRelayOn = on;
    RefreshOnboard();
}

void status_led_set(status_led_state_t state)
{
    sCommissioning = (state == STATUS_LED_COMMISSIONING);

    switch (state)
    {
    case STATUS_LED_BOOT:
        StopBlink();
        SetLevel(true);
        break;
    case STATUS_LED_COMMISSIONING:
        sBlinkOn = false;
        if (sBlinkTimer)
        {
            xTimerStart(sBlinkTimer, 0);
        }
        break;
    case STATUS_LED_OK:
        StopBlink();
        SetLevel(true);
        break;
    case STATUS_LED_ERROR:
        StopBlink();
        SetLevel(false);
        break;
    }

    RefreshOnboard();
}
