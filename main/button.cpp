// Originally ported near-verbatim from esp32c6-radar-demo-matter/main/button.cpp
// for a single fixed pin; generalized into a per-instance struct so a second,
// independent button (the XIAO's onboard BOOT button) could be added
// alongside the plug's own tactile switch without sharing debounce/hold state.

#include "button.h"

#include <inttypes.h>

#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_pins.h"
#include "app_config.h"

static const char *TAG = "button";

// Supports a small, fixed number of button_init() callers (currently two:
// the plug's own button and the XIAO's onboard BOOT button).
#define MAX_BUTTONS 2

struct ButtonState
{
    gpio_num_t pin;
    void (*on_long_press)(void);
    void (*on_short_press)(void);
    TaskHandle_t task;
};

static ButtonState s_buttons[MAX_BUTTONS];
static int         s_button_count = 0;

// ISR: only unblocks the button task. All real work (logging, Matter calls)
// happens in task context where it is safe. The interrupt is level-triggered
// (see button_init for why), so mask this line immediately — otherwise it
// would re-fire continuously while the button is held down. The task re-arms
// it once the button is released.
static void IRAM_ATTR button_isr(void *arg)
{
    ButtonState *btn = (ButtonState *)arg;
    gpio_intr_disable(btn->pin);
    BaseType_t hpw = pdFALSE;
    vTaskNotifyGiveFromISR(btn->task, &hpw);
    portYIELD_FROM_ISR(hpw);
}

// Blocks until a press wakes it, then times how long the button is held: a
// hold of FACTORY_RESET_HOLD_MS or longer fires the long-press callback, a
// shorter press fires the short-press callback. It blocks while idle rather
// than polling.
static void button_task(void *arg)
{
    ButtonState        *btn  = (ButtonState *)arg;
    const TickType_t     step = pdMS_TO_TICKS(50);
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Debounce: confirm the button is genuinely down. If it was a spurious
        // blip, re-arm the (now masked) interrupt and wait for the next press.
        vTaskDelay(pdMS_TO_TICKS(20));
        if (gpio_get_level(btn->pin) != 0)
        {
            gpio_intr_enable(btn->pin);
            continue;
        }

        TickType_t press_start = xTaskGetTickCount();
        bool       fired_long  = false;
        while (gpio_get_level(btn->pin) == 0)
        {
            vTaskDelay(step);
            uint32_t held_ms = (xTaskGetTickCount() - press_start) * portTICK_PERIOD_MS;
            if (!fired_long && held_ms >= FACTORY_RESET_HOLD_MS)
            {
                fired_long = true;
                ESP_LOGW(TAG, "GPIO%d held %" PRIu32 " ms — long press", btn->pin, held_ms);
                if (btn->on_long_press)
                    btn->on_long_press();
            }
        }

        uint32_t held_ms = (xTaskGetTickCount() - press_start) * portTICK_PERIOD_MS;
        if (!fired_long && held_ms > 0 && btn->on_short_press)
        {
            ESP_LOGI(TAG, "GPIO%d short press (%" PRIu32 " ms)", btn->pin, held_ms);
            btn->on_short_press();
        }

        // Button released — re-arm the level interrupt for the next press.
        gpio_intr_enable(btn->pin);
    }
}

void button_init(gpio_num_t pin, void (*on_long_press)(void), void (*on_short_press)(void))
{
    if (s_button_count >= MAX_BUTTONS)
    {
        ESP_LOGE(TAG, "button_init: no free slots for GPIO%d", pin);
        return;
    }
    ButtonState *btn    = &s_buttons[s_button_count++];
    btn->pin            = pin;
    btn->on_long_press  = on_long_press;
    btn->on_short_press = on_short_press;

    // Level-triggered (LOW), NOT edge-triggered. The ISR masks itself as soon
    // as it fires and the task re-arms it once the button is released, so a
    // held button does not re-enter the handler continuously.
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_LOW_LEVEL,
    };
    gpio_config(&cfg);

    // Task must exist before the ISR can notify it.
    char task_name[16];
    snprintf(task_name, sizeof(task_name), "button%d", pin);
    if (xTaskCreate(button_task, task_name, 3072, btn, 5, &btn->task) != pdPASS)
    {
        ESP_LOGE(TAG, "button task create failed for GPIO%d — that button disabled", pin);
        s_button_count--;
        return;
    }

    // gpio_install_isr_service returns INVALID_STATE if already installed
    // (e.g. by an earlier button_init call); both outcomes are fine, so
    // don't ESP_ERROR_CHECK it.
    gpio_install_isr_service(0);
    gpio_isr_handler_add(pin, button_isr, btn);

    ESP_LOGI(TAG, "Button on GPIO%d ready (hold %" PRIu32 " ms to factory-reset)",
             pin, (uint32_t)FACTORY_RESET_HOLD_MS);
}
