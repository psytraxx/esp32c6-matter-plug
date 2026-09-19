#pragma once

#include "driver/gpio.h"

// Interrupt-driven button, generalized so it can drive either the CB2S
// plug's own tactile switch (PIN_BUTTON) or the XIAO module's onboard BOOT
// button (PIN_BOOT_BUTTON) — see board_pins.h for both.
//
// A hold of at least FACTORY_RESET_HOLD_MS fires on_long_press; a shorter
// press fires on_short_press. Both callbacks run in a dedicated task
// context, so they may safely call Matter APIs. Either callback may be NULL.
// Each call to button_init() owns its own GPIO, ISR and task, so independent
// buttons don't interfere with each other's debounce/hold state.
//
// Originally ported near-verbatim from esp32c6-radar-demo-matter's
// button.{h,cpp} for a single fixed pin; generalized to take the GPIO
// explicitly so a second instance could be added for PIN_BOOT_BUTTON.
void button_init(gpio_num_t pin, void (*on_long_press)(void), void (*on_short_press)(void));
