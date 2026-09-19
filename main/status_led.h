#pragma once

// Plug's WiFi-status LED (PIN_LED in board_pins.h), repurposed here as the
// Matter network/commissioning indicator. The plug's other LED (measured on
// the P26/relay net in the original CB2S wiring) sits on the relay drive net
// in hardware and follows relay state without a GPIO of its own; see relay.cpp.
//
// The XIAO's own onboard LED (PIN_ONBOARD_LED) shows both, with commissioning
// taking priority: it blinks along with the plug's LED while the commissioning
// window is open, and otherwise follows relay state. Relay state matters on the
// bench because the relay coil runs off a mains-derived rail and will not
// physically click on USB-only power (see relay.h), so this LED is the only
// feedback that a controller toggle actually landed.
//
// PIN_LED is active-low (tuya-config.json netled1_lv:0 — the stock firmware's
// own dumped config, not a bench guess). PIN_ONBOARD_LED is also driven
// active-low, which is the usual wiring for the XIAO's user LED.

#include <stdbool.h>

typedef enum
{
    STATUS_LED_BOOT,          // device alive, boot in progress
    STATUS_LED_COMMISSIONING, // commissioning window open
    STATUS_LED_OK,            // commissioned and paired
    STATUS_LED_ERROR,         // error indication
} status_led_state_t;

#ifdef __cplusplus
extern "C" {
#endif

void status_led_init(void);

// Drives the plug's network/commissioning LED (PIN_LED).
void status_led_set(status_led_state_t state);

// Records relay state for the onboard LED. Takes effect immediately unless the
// commissioning blink is currently overriding it.
void status_led_set_relay(bool on);

#ifdef __cplusplus
}
#endif
