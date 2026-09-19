# openbeken-matter

Two things live in this repo:

1. **Reverse-engineering notes** (`README.md`, `doc/`) for a Tuya CB2S
   (BK7231N) energy-metering smart plug — measured pinout, BL0937
   calibration block, flash layout, stock Tuya schema.
2. **A Matter firmware** (`main/`, ESP-IDF) that replaces the CB2S module
   with a Seeed Studio XIAO ESP32-C6, turning the plug into an On/Off
   Plug-in Unit with live power/energy metering over Matter-over-Thread.

The firmware is a **port**, not a from-scratch build. Its two ancestors:

- **`uascent-matter`** (sibling repo, nRF52840/Zephyr) — the *application*.
  BL0937 driver algorithm, ElectricalPowerMeasurement/
  ElectricalEnergyMeasurement Matter cluster wiring, relay/button/LED,
  over-power trip. Hardware-verified against real mains on a *different*
  plug (Uascent UAM023-based, not this CB2S one).
- **`esp32c6-radar-demo-matter`** (sibling repo, ESP-IDF/esp_matter) — the
  *platform*. Commissioning flow, button handling, sdkconfig, build/flash
  workflow, the `tools/spake2p_verifier.py` pairing-code generator.

## Build

```sh
source ~/.espressif/v6.0.3/esp-idf/export.sh
idf.py set-target esp32c6
idf.py build
idf.py flash monitor
```

Matter builds are memory-hungry with LTO and can get OOM-killed at default
parallelism on a constrained machine. Pass
`-- -DCMAKE_JOB_POOLS="compile=4;link=1"` to `idf.py build` if that happens.

Give this device its own pairing code before flashing (see
`main/chip_project_config.h`):

```sh
python3 tools/spake2p_verifier.py <passcode>
```

## Hardware — pin roles are NOT the same as the sibling project

**Do not port `uascent-matter`'s board overlay or pin numbers.** The two
donor plugs place BL0937/relay/LED on almost inverted pins:

| Signal | Uascent (UAM023, sibling repo) | This plug (CB2S) |
|---|---|---|
| BL0937 `CF` | P24 | **P7** |
| BL0937 `CF1` | P26 | **P6** |
| BL0937 `SEL` | P8 | **P24** |
| Relay | P6 | **P26** |
| LED | P7 | **P8** |
| Button | RX1 (P10) | RX1 (P10) — same convention |

`main/board_pins.h` is the single source of truth for this plug's XIAO-side
GPIO assignments; the pad-level mapping is in README.md's "Wiring" section.

**`P10` is the pad silkscreened `RX1` on this module** — confirmed against
the plug's schematic, not a rework or jumper. A reader looking at the CB2S
drawing would otherwise expect a UART line at that pad.

## No esp_matter patch needed (unlike the sibling project)

`esp32c6-radar-demo-matter`'s root `CMakeLists.txt` patches
`managed_components/espressif__esp_matter` after every fresh checkout,
because that project's OccupancySensing cluster keeps its setter private
(`attribute::update()` silently no-ops for a registered cluster's reads).

**That problem does not apply here — do not port that patch machinery.**
`ElectricalPowerMeasurement` uses esp_matter's Delegate/Instance pattern
(`PlugPowerDelegate` in `main/power_measurement.cpp`), which self-registers
correctly. `ElectricalEnergyMeasurement` in this esp_matter version (1.6.0)
is registered by adding the cluster via
`esp_matter::cluster::electrical_energy_measurement::create()`
(`main/matter_setup.cpp`'s `create_endpoints()`), which triggers an
esp_matter-owned init callback
(`ESPMatterElectricalEnergyMeasurementClusterServerInitCallback`,
in `managed_components/espressif__esp_matter/components/esp_matter/
data_model_provider/clusters/electrical_energy_measurement/integration.cpp`)
that constructs the real cluster automatically once the data model loads.
`power_measurement.cpp` then talks to it purely through that integration
header's free functions (`GetClusterInstance()`, `SetMeasurementAccuracy()`,
`NotifyCumulativeEnergyMeasured()`) — no manual `AttrAccess` construction,
which this esp_matter build doesn't even compile CHIP's own
`ElectricalEnergyMeasurementAttrAccess` shim for (link error if you try).

If bumping `espressif/esp_matter` past 1.6.0 ever reintroduces that
constructor, `PowerMeasurementInit()`'s `GetClusterInstance() == nullptr`
check will fail loudly with a clear log line, not silently drop energy
reports — check there first.

## Calibration — unverified for this unit

`main/app_config.h`'s `METER_MILLI_COUNTS_PER_SEC_PER_*` constants and
`METER_SEL_HIGH_SELECTS_VOLTAGE` are **placeholders copied from
uascent-matter's Uascent unit**, not this CB2S plug's own values. They
exist only so the driver has something to divide by during USB-only
bring-up. Two independent facts make this plug's true calibration unknown:

- This README's "Calibration coefficients" section recovered a *different*
  set of numbers (1.0 / 2.2 / 0.1 / 0.1) from the CB2S's own Tuya config
  block, but those are Tuya-format *multipliers* — a different convention
  from the counts-per-second-per-unit *divisors* `main/bl0937.cpp` expects.
  The conversion between the two has not been worked out.
- Even if it were worked out, those values belong to this specific unit's
  shunt, not a transferable constant.
- SEL polarity (`METER_SEL_HIGH_SELECTS_VOLTAGE`) is likewise unmeasured for
  this plug. Getting it wrong silently swaps voltage and current in every
  reading — it will not error.

Do not trust any power/voltage/current reading from this firmware until
these are re-derived on the bench (README.md's Verification checklist).

## Mains safety

The BL0937's ground sits at mains potential. Never connect USB to the XIAO
while the plug is connected to mains. Develop with the board USB-powered
and mains disconnected — everything except real power readings works that
way.

## Matter data model

Endpoint 1 = `on_off_plug_in_unit` (0x010A), with `ElectricalPowerMeasurement`
(0x0090) and `ElectricalEnergyMeasurement` (0x0091) added directly onto it
in `create_endpoints()`. Unlike the Zephyr sibling project, there is no ZAP
GUI or hand-maintained `.matter`/`.zap` file to keep in sync — esp_matter
builds the data model programmatically from the `config_t` structs and
`cluster::*::create()` calls in `matter_setup.cpp`.

Transport is **Matter over Thread**, FTD (always-on, no ICD/sleep) — see
`sdkconfig.defaults`'s comment for why this differs from the nRF sibling's
MTD+ICD choice. Requires a Thread Border Router on the network to commission.
