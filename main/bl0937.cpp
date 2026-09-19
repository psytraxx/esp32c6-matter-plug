/*
 * See bl0937.h. Algorithm ported from uascent-matter/src/bl0937.cpp
 * (nRF52840/Zephyr, hardware-verified against real mains on that plug) —
 * only the GPIO/ISR/atomics layer below is new; the measurement scheme
 * itself is unchanged, since changing it would invalidate the calibration
 * cadence it depends on.
 *
 * Measurement strategy: CF and CF1 are pulse-frequency outputs (active power
 * and V/I respectively, the latter muxed by SEL). Pulses are counted via GPIO
 * edge interrupts and turned into a frequency once per second rather than
 * timing individual periods -- far more robust at low power, where pulses can
 * be seconds apart.
 */

#include "bl0937.h"

#include <inttypes.h>
#include <stdatomic.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

#include "app_config.h"
#include "app_main.h"
#include "board_pins.h"
#include "power_measurement.h"
#include "relay.h"

static const char *TAG = "bl0937";

namespace
{

/* Incremented from GPIO ISR context (IRAM), read/reset from MeterPoll() on
 * the app's own task -- atomic rather than a lock, since the only operations
 * needed are "add one" and "swap out for zero". C11 atomics stand in for
 * Zephyr's atomic_t/atomic_set here. */
volatile atomic_uint sCfPulses;
volatile atomic_uint sCf1Pulses;

/* Median-of-3 filter -- see MedianFilter below. Push() returns true exactly
 * once per METER_FILTER_DEPTH calls, when a window completes. */
constexpr size_t kFilterDepth = METER_FILTER_DEPTH;

/* SEL holds for one full filter window -- 3 s at the 1 Hz poll rate. Since
 * the two quantities take turns, each of V and I refreshes every 6 s; active
 * power, which is not muxed, refreshes every 3 s. */
bool sSelIsVoltage = METER_SEL_HIGH_SELECTS_VOLTAGE;

int64_t sLastActivePowerMw;
int64_t sLastRmsVoltageMv;
int64_t sLastRmsCurrentMa;
int64_t sLastPollMs;

/* Median-of-three over a channel's counts-per-second samples. Push() returns
 * true exactly once per kFilterDepth calls, when a window completes. */
class MedianFilter {
public:
	bool Push(uint32_t sample, uint32_t *median)
	{
		mSamples[mCount++] = sample;
		if (mCount < kFilterDepth) {
			return false;
		}
		mCount = 0;

		/* Sorting network for three elements -- matches the stock
		 * Uascent firmware's bubble-sort-and-take-the-middle, just
		 * without the loop. */
		uint32_t a = mSamples[0], b = mSamples[1], c = mSamples[2];
		if (a > b) {
			const uint32_t t = a; a = b; b = t;
		}
		if (b > c) {
			const uint32_t t = b; b = c; c = t;
		}
		if (a > b) {
			const uint32_t t = a; a = b; b = t;
		}
		*median = b;
		return true;
	}

private:
	uint32_t mSamples[kFilterDepth];
	size_t mCount = 0;
};

MedianFilter sCfFilter;
MedianFilter sCf1Filter;

/* Consecutive samples seen above the trip threshold. Reset by any sample at
 * or below it, so only a *sustained* overload counts. */
uint32_t sOverPowerSamples;

/* Checked against the raw per-second sample rather than the median: the
 * median deliberately lags by three seconds, and protection should not.
 *
 * No latch: a trip opens the relay and nothing more, so turning the plug
 * back on simply re-arms the check -- a persistent overload trips again
 * after APP_OVERPOWER_SAMPLES seconds. A latch's own failure mode (plug
 * stuck off with no obvious way to clear it) is worse than the cost here
 * (a controller automation that blindly re-enables the plug could cycle
 * the relay). */
void CheckOverPower(int64_t instantPowerMw)
{
	if (!APP_OVERPOWER_PROTECTION_ENABLED) {
		return;
	}

	if (instantPowerMw <= APP_OVERPOWER_THRESHOLD_MW) {
		sOverPowerSamples = 0;
		return;
	}

	if (++sOverPowerSamples < APP_OVERPOWER_SAMPLES) {
		return;
	}

	sOverPowerSamples = 0;

	if (!RelayIsOn()) {
		return;
	}

	ESP_LOGE(TAG, "Over-power: %" PRId64 " mW above %" PRId64 " mW for %" PRIu32 " s -- opening relay",
		 instantPowerMw, APP_OVERPOWER_THRESHOLD_MW, APP_OVERPOWER_SAMPLES);

	/* Same path the button/controller take, so the relay, the plug LED and
	 * the OnOff attribute cannot disagree about what happened. */
	RelaySet(false);
	AppUpdateOnOffCluster();
}

void IRAM_ATTR CfIsr(void *)
{
	atomic_fetch_add(&sCfPulses, 1);
}

void IRAM_ATTR Cf1Isr(void *)
{
	atomic_fetch_add(&sCf1Pulses, 1);
}

/* Swaps the given pulse counter out for zero and returns what it held,
 * atomically -- so a pulse arriving between the read and the reset is never
 * lost, unlike a plain read-then-clear. */
uint32_t TakePulses(volatile atomic_uint *counter)
{
	return atomic_exchange(counter, 0);
}

/* Normalise a window's raw pulse count to counts per second, immune to timer
 * jitter (unlike assuming the tick is exactly 1 s and using the raw count). */
uint32_t CountsPerSec(uint32_t pulses, int64_t windowMs)
{
	return static_cast<uint32_t>((static_cast<int64_t>(pulses) * 1000) / windowMs);
}

} /* namespace */

void MeterInit(void)
{
	ESP_LOGI(TAG, "BL0937 driver active (SEL %s selects voltage)",
		 METER_SEL_HIGH_SELECTS_VOLTAGE ? "HIGH" : "LOW");

	gpio_config_t in_cfg = {
		.pin_bit_mask = (1ULL << PIN_BL0937_CF) | (1ULL << PIN_BL0937_CF1),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_POSEDGE,
	};
	ESP_ERROR_CHECK(gpio_config(&in_cfg));

	/* gpio_install_isr_service() returns ESP_ERR_INVALID_STATE if a service
	 * is already installed elsewhere (e.g. by button.cpp) -- both outcomes
	 * are fine, so this is not ESP_ERROR_CHECK'd. */
	esp_err_t isr_svc_err = gpio_install_isr_service(0);
	if (isr_svc_err != ESP_OK && isr_svc_err != ESP_ERR_INVALID_STATE) {
		ESP_ERROR_CHECK(isr_svc_err);
	}

	ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BL0937_CF, CfIsr, NULL));
	ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BL0937_CF1, Cf1Isr, NULL));

	/* SEL polarity is device-specific -- see METER_SEL_HIGH_SELECTS_VOLTAGE
	 * in app_config.h and its UNVERIFIED warning. Start in the voltage
	 * phase, matching sSelIsVoltage's initial value above. */
	gpio_config_t sel_cfg = {
		.pin_bit_mask = (1ULL << PIN_BL0937_SEL),
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	ESP_ERROR_CHECK(gpio_config(&sel_cfg));
	gpio_set_level(PIN_BL0937_SEL, METER_SEL_HIGH_SELECTS_VOLTAGE ? 1 : 0);
}

void MeterPoll(void)
{
	const int64_t nowMs = esp_timer_get_time() / 1000;
	const int64_t windowMs = sLastPollMs ? (nowMs - sLastPollMs) : 0;
	sLastPollMs = nowMs;

	const uint32_t cfPulses = TakePulses(&sCfPulses);
	const uint32_t cf1Pulses = TakePulses(&sCf1Pulses);

	/* First call has no elapsed window to normalise against; it only
	 * starts the clock. Discard its counts rather than feeding a bogus
	 * rate into the filters. */
	if (windowMs <= 0) {
		return;
	}

	uint32_t median;
	const uint32_t cfCountsPerSec = CountsPerSec(cfPulses, windowMs);

	CheckOverPower((static_cast<int64_t>(cfCountsPerSec) * 1'000'000) / METER_MILLI_COUNTS_PER_SEC_PER_WATT);

	/* Active power is measured continuously -- CF is not muxed. */
	if (sCfFilter.Push(cfCountsPerSec, &median)) {
		sLastActivePowerMw = (static_cast<int64_t>(median) * 1'000'000) / METER_MILLI_COUNTS_PER_SEC_PER_WATT;
	}

	/* SEL is flipped only here, when a CF1 window completes, so the level
	 * was constant across all kFilterDepth samples that produced this
	 * median and sSelIsVoltage still names it. */
	if (sCf1Filter.Push(CountsPerSec(cf1Pulses, windowMs), &median)) {
		if (sSelIsVoltage) {
			sLastRmsVoltageMv = (static_cast<int64_t>(median) * 1'000'000) / METER_MILLI_COUNTS_PER_SEC_PER_VOLT;
		} else {
			sLastRmsCurrentMa = (static_cast<int64_t>(median) * 1'000'000) / METER_MILLI_COUNTS_PER_SEC_PER_AMP;
		}

		sSelIsVoltage = !sSelIsVoltage;
		gpio_set_level(PIN_BL0937_SEL, sSelIsVoltage ? 1 : 0);
	}

	PowerMeasurementUpdate(sLastActivePowerMw, sLastRmsVoltageMv, sLastRmsCurrentMa);
}
