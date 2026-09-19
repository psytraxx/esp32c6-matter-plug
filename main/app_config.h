#pragma once

#include <stddef.h>
#include <stdint.h>

// ─────────────────────────────────────────────────────────────────────────────
// Tunable application constants. Pin assignments live in board_pins.h.
// ─────────────────────────────────────────────────────────────────────────────

// Name this device reports to Matter controllers (Home Assistant shows it as
// the device name). Max 32 chars per the Matter spec.
inline constexpr const char *BOARD_NODE_LABEL = "CB2S Power Plug";

// Hold the plug's button (PIN_BUTTON) this long to factory-reset.
inline constexpr uint32_t FACTORY_RESET_HOLD_MS = 5000;

// ─────────────────────────────────────────────────────────────────────────────
// BL0937 metering (see main/bl0937.{h,cpp} for the algorithm this cadence is
// derived from — ported from uascent-matter/src/bl0937.cpp).
// ─────────────────────────────────────────────────────────────────────────────

// Poll rate is part of the calibration, not a free parameter: readings are a
// median of three consecutive samples, so active power refreshes every 3 s,
// and SEL holds for one such window so V and I take turns and each refreshes
// every 6 s. Changing this without re-deriving the calibration divisors below
// will silently skew every reading.
inline constexpr uint32_t METER_POLL_INTERVAL_MS = 1000;

// Median-of-3 filter depth. See bl0937.cpp's MedianFilter — a median of three
// discards the one sample straddling a SEL flip as an outlier, which is what
// makes a separate SEL-settling delay unnecessary. Do not change without
// re-reading that reasoning.
inline constexpr size_t METER_FILTER_DEPTH = 3;

// ⚠️ PLACEHOLDER CALIBRATION — NOT YET DERIVED FOR THIS UNIT.
//
// uascent-matter's divisors (8.0773 / 91.6364 / 0.77521) came from THAT
// Uascent unit's own NV store and are specific to its shunt; they do not
// transfer to this CB2S plug.
//
// This plug's README ("Calibration coefficients") recovered a *different*
// set of numbers (voltage 1.0, current 2.2, power 0.1, energy 0.1) from its
// Tuya config block — but those are Tuya-format *multipliers*, not the
// "counts-per-second-per-unit" *divisors* this driver expects, and the
// conversion between the two conventions has not been worked out. Treating
// them as divisors would silently produce wrong readings, not an error.
//
// The values below are UNVERIFIED PLACEHOLDERS (copied from uascent-matter
// only so the driver has *some* value to divide by while bringing the rest
// of the stack up over USB with mains disconnected). Do not trust any
// power/voltage/current reading from this firmware until they are re-derived
// against a real load — see README.md's Verification section.
inline constexpr int64_t METER_MILLI_COUNTS_PER_SEC_PER_WATT = 775;   // 0.7752066 — PLACEHOLDER
inline constexpr int64_t METER_MILLI_COUNTS_PER_SEC_PER_VOLT = 8077;  // 8.0772724 — PLACEHOLDER
inline constexpr int64_t METER_MILLI_COUNTS_PER_SEC_PER_AMP = 91636;  // 91.6363602 — PLACEHOLDER

// SEL polarity is device-specific: on the Uascent unit, SEL HIGH selects
// voltage (the opposite of the HLW8012 convention). UNKNOWN for this CB2S —
// must be measured (README already recommends toggling SEL and watching
// CF1's character change). This constant is what main/bl0937.cpp reads to
// decide the boot phase and the meaning of each SEL level; get it wrong and
// every V/I reading in Home Assistant is silently swapped.
inline constexpr bool METER_SEL_HIGH_SELECTS_VOLTAGE = true; // UNVERIFIED — confirm on bench

// ─────────────────────────────────────────────────────────────────────────────
// Over-power protection — the one safety behaviour that has to be local
// rather than waiting for a controller to notice an overload.
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr bool APP_OVERPOWER_PROTECTION_ENABLED = true;

// SET THIS TO MATCH YOUR PLUG. 2400 W suits a 10 A/230 V plug; a 16 A plug
// wants roughly 3700 W. Take the figure from the plug's own housing/rating
// label, not from the donor board's silkscreen.
inline constexpr int64_t APP_OVERPOWER_THRESHOLD_MW = 2'400'000;

// Consecutive over-threshold 1 Hz samples required before tripping — long
// enough that inrush current cannot nuisance-trip, matching the stock
// firmware's behaviour on the Uascent unit (docs there recovered a
// 5-sample debounce; used unchanged here as a reasonable default).
inline constexpr uint32_t APP_OVERPOWER_SAMPLES = 5;
