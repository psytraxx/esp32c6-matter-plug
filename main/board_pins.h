#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"

// ─────────────────────────────────────────────────────────────────────────────
// Single source of truth for every GPIO used by this firmware, on a Seeed
// XIAO ESP32-C6 wired into the CB2S module's 11-pad castellated footprint
// (the module itself is desoldered — this is a flying-wire rework, not a
// drop-in; see README.md's "Wiring" section for the header pad ↔ XIAO pad
// table and the physical/power caveats) plus a flying-wire LD2410 presence
// radar that is NOT part of the CB2S module at all.
//
// This plug has no energy metering — tuya-config.json/info.txt show no
// BL0937 pin roles at all, unlike the metered sibling esp32h2-matter-plug.
// D0-D2 are simply free, which is what makes room for the radar's UART pair.
//
// Signal polarities below come straight from tuya-config.json (the stock
// firmware's own dumped config), not a bench measurement:
//   bt1_lv:0      -> button active-low
//   netled1_lv:0  -> status LED active-low
//   rl1_lv:1      -> relay active-high
//
// PIN ROLES ARE NEARLY INVERTED relative to the sibling uascent-matter
// project (a UAM023-based plug). Do NOT reuse that project's board overlay
// or pin numbers — info.txt / tuya-config.json are the only authoritative
// source for THIS plug:
//
//   Signal        Uascent (UAM023)   This plug (CB2S)
//   Relay         P6                 P26 -> here: PIN_RELAY
//   LED           P7                 P8  -> here: PIN_LED
//   Button        RX1 (P10)          RX1 (P10, same convention)
//
// XIAO-side pin choices (D0-D5) are ours, since the two boards are joined by
// hand. Constraints applied:
//   - D6/D7 (GPIO16/17) are the ESP32-C6's default console UART0 pins —
//     wiring anything there crash-loops boot (hit and documented in the
//     sibling esp-demo-matter project's board_pins.h). Deliberately unused.
//   - None of D0-D10 are ESP32-C6 strapping pins (those are GPIO4/5/8/9/15,
//     on the MTMS/MTDI/Boot/Light pads, not used by this design) — so there
//     is no boot-state constraint on any of the choices below.
//   - Radar UART grouped on D1/D2, simply because those pins are otherwise
//     unused on this plug; relay on D5, furthest from the radar's UART pair
//     to reduce switching-noise coupling into the receiver.
// ─────────────────────────────────────────────────────────────────────────────

// LD2410 24 GHz presence radar (UART1, 256000 baud 8N1, factory defaults —
// see main/ld2410_bridge.{h,cpp} and RADAR_* in app_config.h). Not on the
// CB2S footprint; flying-wire to a separate LD2410 module. Crossed, not
// same-name-to-same-name: sensor TX -> ESP RX, sensor RX -> ESP TX.
#define RADAR_UART_PORT UART_NUM_1
#define RADAR_UART_TX   GPIO_NUM_2  // D2 -> LD2410 pin 3 (RX)
#define RADAR_UART_RX   GPIO_NUM_1  // D1 <- LD2410 pin 2 (TX)

// D0/GPIO0 spare.

// Plug's own tactile button. Measured pinout: the button sits on P10, which
// on this module's footprint is the pad silkscreened RX1 — confirmed against
// the plug's schematic, not a rework or jumper. Active-low (tuya-config.json
// bt1_lv:0).
#define PIN_BUTTON GPIO_NUM_21 // D3

// XIAO ESP32-C6 module's own onboard BOOT button (not on the CB2S footprint —
// this is the dev-board button soldered to the XIAO itself, GPIO9, active-low
// to GND via the module's own pull-up, independent of PIN_BUTTON above). Wired
// in as a second, bench-only factory-reset trigger: once the plug is closed up
// this pin isn't reachable, so PIN_BUTTON remains the real user-facing control.
#define PIN_BOOT_BUTTON GPIO_NUM_9

// Plug's WiFi-status LED (repurposed here as the Matter network/commissioning
// indicator — see status_led.h). Active-low (tuya-config.json netled1_lv:0).
#define PIN_LED GPIO_NUM_22 // D4

// Plug's relay, switching the load. Active-high (tuya-config.json rl1_lv:1).
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
