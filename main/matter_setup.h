#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// Buffer sizes for the QR payload ("MT:...") and manual pairing code strings
// returned by matter_get_pairing_codes().
#define MATTER_QR_BUF_LEN      256
#define MATTER_MANUAL_CODE_LEN 32

#ifdef __cplusplus
extern "C" {
#endif

// Initialise the Matter On/Off Plug-in Unit endpoint (with
// ElectricalPowerMeasurement + ElectricalEnergyMeasurement), register the
// commissioning event callback, and start the Matter stack. boot_events/
// commissioned_bit are used to signal MATTER_COMMISSIONING_COMPLETE and
// server_ready_bit for kServerReady.
void matter_setup(EventGroupHandle_t boot_events,
                   EventBits_t commissioned_bit,
                   EventBits_t server_ready_bit);

// True if the device has a commissioned fabric in NVS.
bool matter_is_commissioned(void);

// Push RelayIsOn()'s current value into the OnOff cluster so a controller
// subscriber sees it, regardless of what changed the relay (button press or
// over-power trip). Called from AppUpdateOnOffCluster() (app_main.cpp).
void matter_update_onoff(void);

// Toggle the relay via the OnOff cluster and push the result. Called from
// the plug button's short-press handler.
void matter_button_toggle(void);

// Fill qr_buf with the "MT:…" QR payload and code_buf with the manual pairing
// code. Blocks until the commissioning window is open and the codes are ready;
// returns immediately if the device is already commissioned.
void matter_get_pairing_codes(char *qr_buf,  size_t qr_len,
                               char *code_buf, size_t code_len);

// Trigger a factory reset (clears fabric, reopens commissioning window).
void matter_factory_reset(void);

#ifdef __cplusplus
}
#endif
