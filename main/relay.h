#pragma once

// Relay control for the smart plug. Ported from uascent-matter/src/relay.h —
// drives the plug's existing relay via the GPIO the CB2S module used
// (PIN_RELAY in board_pins.h).
//
// Note the relay coil is driven from a mains-derived rail, not from the
// XIAO's 3.3 V. When the board is bench-powered over USB with mains
// disconnected, the GPIO will toggle correctly but the relay may not
// physically click. That is expected, not a fault; verify with a meter/scope
// on the pad rather than by ear. See README.md's Verification section.

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configures the relay GPIO, leaving the relay OFF.
void RelayInit(void);

// Switches the load. Safe to call repeatedly with the same value.
void RelaySet(bool on);

// Last value passed to RelaySet(), or false before the first call. Reflects
// what the firmware commanded, not any sensed state -- the plug has no relay
// feedback contact.
bool RelayIsOn(void);

#ifdef __cplusplus
}
#endif
