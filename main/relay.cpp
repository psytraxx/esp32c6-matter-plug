#include "relay.h"

#include "esp_log.h"
#include "driver/gpio.h"

#include "board_pins.h"
#include "status_led.h"

static const char *TAG = "relay";

namespace
{
bool sOn;
} // namespace

void RelayInit(void)
{
	gpio_config_t cfg = {
		.pin_bit_mask = (1ULL << PIN_RELAY),
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	ESP_ERROR_CHECK(gpio_config(&cfg));

	// Relay starts open regardless of persisted state; the OnOff cluster's
	// restore path (matter_setup.cpp, after esp_matter::start()) may switch
	// it back on shortly after, once the stack has read the persisted OnOff
	// attribute back out of NVS.
	gpio_set_level(PIN_RELAY, 0);
	sOn = false;
}

void RelaySet(bool on)
{
	gpio_set_level(PIN_RELAY, on ? 1 : 0);

	if (on != sOn) {
		ESP_LOGI(TAG, "Relay %s", on ? "on" : "off");
	}
	sOn = on;

	// The plug's own relay-state LED (if any) sits on the relay drive net in
	// hardware and needs no GPIO. The XIAO's onboard LED does need driving,
	// and shows relay state because the coil runs off a mains-derived rail:
	// on USB-only bench power the relay will not click, so this LED is the
	// only feedback that a controller toggle actually landed (see relay.h).
	status_led_set_relay(on);
}

bool RelayIsOn(void)
{
	return sOn;
}
