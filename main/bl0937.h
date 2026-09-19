#pragma once

// BL0937 energy-metering chip driver — ESP-IDF port of the algorithm
// developed and hardware-verified in uascent-matter/src/bl0937.cpp (nRF52840
// / Zephyr). The measurement algorithm (1 Hz edge-count, median-of-3, SEL
// dwell) is ported unchanged; only the GPIO/ISR layer is rewritten against
// ESP-IDF's driver/gpio.h.
//
// No ESP-IDF in-tree driver exists for this part, so this is written from
// scratch against the chip's pulse-frequency output: CF's frequency is
// proportional to active power, CF1's frequency is proportional to RMS
// voltage or RMS current depending on SEL.
//
// ⚠️ Calibration constants in app_config.h are UNVERIFIED PLACEHOLDERS for
// this specific plug — see the comment there before trusting any reading.

void MeterInit(void);

// Called once per second (METER_POLL_INTERVAL_MS in app_config.h, from a
// FreeRTOS timer set up in app_main.cpp) to turn the pulses counted since the
// last call into a reading and push it into the Matter clusters via
// PowerMeasurementUpdate().
//
// The rate is part of the calibration, not a free parameter: readings are a
// median of three consecutive samples, so active power refreshes every 3 s,
// and SEL holds for one such window so V and I take turns and each refreshes
// every 6 s.
void MeterPoll(void);
