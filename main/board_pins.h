#pragma once

#include "driver/gpio.h"

// ─────────────────────────────────────────────────────────────────────────────
// Single source of truth for every GPIO used by this firmware, on a Seeed
// XIAO ESP32-C6 wired into the CB2S module's 11-pad castellated footprint
// (the module itself is desoldered — this is a flying-wire rework, not a
// drop-in; see README.md's "Wiring" section for the header pad ↔ XIAO pad
// table and the physical/power caveats).
//
// PIN ROLES ARE NEARLY INVERTED relative to the sibling uascent-matter
// project (a UAM023-based plug). Do NOT reuse that project's board overlay
// or pin numbers — this repo's README "Measured pinout" is the only
// authoritative source for THIS plug:
//
//   Signal        Uascent (UAM023)   This plug (CB2S)
//   BL0937 CF     P24                P7  -> here: PIN_BL0937_CF
//   BL0937 CF1    P26                P6  -> here: PIN_BL0937_CF1
//   BL0937 SEL    P8                 P24 -> here: PIN_BL0937_SEL
//   Relay         P6                 P26 -> here: PIN_RELAY
//   LED           P7                 P8  -> here: PIN_LED
//   Button        RX1 (P10)          RX1 (P10, same convention)
//
// XIAO-side pin choices (D0-D5) are ours, since the two boards are joined by
// hand. Constraints applied:
//   - D6/D7 (GPIO16/17) are the ESP32-C6's default console UART0 pins —
//     deliberately left unused, matching the trap documented in the sibling
//     esp32c6-radar-demo-matter project's board_pins.h.
//   - None of D0-D10 are ESP32-C6 strapping pins (those are GPIO4/5/8/9/15,
//     on the MTMS/MTDI/Boot/Light pads, not used by this design) — so there
//     is no boot-state constraint on any of the choices below.
//   - BL0937 signals grouped on D0-D2; relay on D5, furthest from the pulse
//     inputs to reduce switching-noise coupling into the pulse counters.
// ─────────────────────────────────────────────────────────────────────────────

// BL0937 energy-metering IC (see main/bl0937.h for the driver).
#define PIN_BL0937_CF  GPIO_NUM_0  // D0 — active-power pulse input
#define PIN_BL0937_CF1 GPIO_NUM_1  // D1 — voltage/current pulse input (muxed by SEL)
#define PIN_BL0937_SEL GPIO_NUM_2  // D2 — output; selects what CF1 currently carries

// Plug's own tactile button. Measured pinout: the button sits on P10, which
// on this module's footprint is the pad silkscreened RX1 — confirmed against
// the plug's schematic, not a rework or jumper.
#define PIN_BUTTON GPIO_NUM_21 // D3

// XIAO ESP32-C6 module's own onboard BOOT button (not on the CB2S footprint —
// this is the dev-board button soldered to the XIAO itself, GPIO9, active-low
// to GND via the module's own pull-up, independent of PIN_BUTTON above). Wired
// in as a second, bench-only factory-reset trigger: once the plug is closed up
// this pin isn't reachable, so PIN_BUTTON remains the real user-facing control.
#define PIN_BOOT_BUTTON GPIO_NUM_9

// Plug's WiFi-status LED (repurposed here as the Matter network/commissioning
// indicator — see status_led.h).
#define PIN_LED GPIO_NUM_22 // D4

// Plug's relay, switching the load.
#define PIN_RELAY GPIO_NUM_23 // D5

// D6/D7 (GPIO16/17) intentionally unused — console UART0. D8-D10 are spare.

// XIAO ESP32-C6 module's own onboard LED (not on the CB2S footprint at all —
// this is the dev-board LED soldered to the XIAO itself, GPIO15, independent
// of PIN_LED above). Mirrored to the same state as PIN_LED so the status is
// visible even before the plug's own LED net is wired up on the bench.
#define PIN_ONBOARD_LED GPIO_NUM_15

// XIAO ESP32-C6 RF antenna switch (module-internal, not on the CB2S
// footprint). The board has both an onboard ceramic antenna and a U.FL
// connector; this design uses the onboard one, selected explicitly rather
// than left to whatever the pins float to. Per the XIAO ESP32-C6 pinout:
// GPIO3 low enables the RF switch, GPIO14 selects internal (low) vs
// external (high). Neither pad is exposed on the castellated footprint, so
// there is nothing to wire — this is purely a software selection.
#define PIN_RF_SWITCH_EN  GPIO_NUM_3   // drive LOW to enable the RF switch
#define PIN_RF_ANT_SELECT GPIO_NUM_14  // LOW = onboard ceramic, HIGH = U.FL
