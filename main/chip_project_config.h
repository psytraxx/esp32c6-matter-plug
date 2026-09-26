#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Per-build Matter commissioning identity.
//
// Without this file every flashed device uses the CHIP SDK's shared test setup
// values (passcode 20202021 / discriminator 0xF00) and therefore prints the
// same pairing code. Loaded into the CHIP build via CONFIG_CHIP_PROJECT_CONFIG
// in sdkconfig.defaults.
//
// Discriminator/passcode deliberately differ from both sibling projects
// (esp32h2-matter-plug uses 0x822/29312364, esp-demo-matter uses
// 0x820/20250816) so all three can be commissioned onto the same fabric at
// once.
//
// To give another device its own pairing code, change BOTH values below:
//   1. Pick a new discriminator (any value 0x000–0xFFF; must differ between
//      devices that are commissioned at the same time).
//   2. Pick a new passcode (1–99999998, not a trivial value like 12345678)
//      and regenerate the matching verifier:
//          python3 tools/spake2p_verifier.py <passcode>
//      The passcode and verifier MUST be kept in sync — a mismatch bricks
//      commissioning silently.
// ─────────────────────────────────────────────────────────────────────────────

#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR 0x824

#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_PIN_CODE 37415520

// Generated with: python3 tools/spake2p_verifier.py 37415520
#define CHIP_DEVICE_CONFIG_USE_TEST_SPAKE2P_VERIFIER                        \
    "FyFxqLYFkakuBhHFotNLndJhADkp91E7TBxQxf4cNqwExeeQhKJUGpNMpqQwLsWYsnYPIN" \
    "UOzgKSfv8DkeOr1JVHzB6RpWybvW5Vae+DFA+Sdi4uqP/SZR81zYP6Ht5h4Q=="

// ─────────────────────────────────────────────────────────────────────────────
// Basic Information cluster identity, shown by controllers (e.g. Home
// Assistant's "Device info" panel). Without these the CHIP SDK defaults to
// TEST_VENDOR / TEST_PRODUCT / TEST_VERSION.
// ─────────────────────────────────────────────────────────────────────────────

#include "sdkconfig.h"

#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME "psytraxx"

// Hardware version and serial number shown in the controller's device info
// (SDK defaults: 0 and "TEST_SN"). Set via Kconfig (CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION
// / CONFIG_USE_TEST_SERIAL_NUMBER in sdkconfig.defaults), not #define'd here:
// CHIPDevicePlatformConfig.h #defines CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION
// and CHIP_DEVICE_CONFIG_TEST_SERIAL_NUMBER itself from those Kconfig values,
// unconditionally and after this file is included — a #define here would collide
// with that and is a hard error under -Werror.
#define CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION_STRING "XIAO ESP32-C6 (CB2S retrofit)"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "CB2S Switch"

// Firmware version string is deliberately NOT overridden: the SDK default is
// the git describe of the build, which is exactly what we want to see against
// a running device.
