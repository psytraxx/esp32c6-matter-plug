# esp32c6-matter-plug

Two things live in this repo:

1. **Reverse-engineering notes** (`README.md`, `docs/`) for a Tuya CB2S
   (BK7231N) smart plug — measured pinout, flash layout, stock Tuya schema.
2. **A Matter firmware** (`main/`, ESP-IDF) that replaces the CB2S module
   with a Seeed Studio XIAO ESP32-C6, exposing two independent endpoints:
   an On/Off Plug-in Unit driving the relay, and an Occupancy Sensor backed
   by an LD2410 24 GHz presence radar flying-wired in separately.

This plug has no energy metering — `tuya-config.json` and `info.txt` show no
BL0937 pin roles anywhere in the dump. See README.md's "Datapoints declared
by the original Tuya schema" for what the stock hardware reports.

The firmware is assembled from two sibling projects, not a from-scratch
build:

- **`esp32h2-matter-plug`** (sibling repo) — this repo's own history before
  it was ported to the ESP32-H2 SuperMini (commit `c43a0cd^`): the CB2S
  wiring, commissioning flow, button/LED/relay, Thread sdkconfig, and the
  `tools/spake2p_verifier.py` pairing-code generator. That project's metering
  subsystem (`bl0937.cpp`, `power_measurement.cpp`) was deleted here.
- **`esp-demo-matter`** (sibling repo, ESP32-C6/esp_matter) — the radar half:
  `ld2410_bridge.cpp` (copied verbatim), the OccupancySensing cluster setup,
  and the **required esp_matter patch** (see below).

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

## Hardware — pin roles are NOT the same as the sibling projects

**Do not port another CB2S project's board overlay or pin numbers without
checking.** `main/board_pins.h` is the single source of truth for this
plug's XIAO-side GPIO assignments; the pad-level mapping is in README.md's
"Wiring" section, cross-checked against `info.txt` / `tuya-config.json`.

| Signal | Pad | XIAO pin | GPIO |
|---|---|---|---|
| Button | RX1 (P10) | D3 | GPIO21 |
| Status LED | P8 | D4 | GPIO22 |
| Relay | P26 | D5 | GPIO23 |
| Radar RX (← sensor TX) | — (not CB2S) | D1 | GPIO1 |
| Radar TX (→ sensor RX) | — (not CB2S) | D2 | GPIO2 |

**`P10` is the pad silkscreened `RX1` on this module** — confirmed against
the plug's schematic, not a rework or jumper. A reader looking at the CB2S
drawing would otherwise expect a UART line at that pad.

D0/GPIO0 is spare; D6/D7 (GPIO16/17) are the console UART0 pins and must
never be claimed by another peripheral — doing so crash-loops boot.

## ⚠️ Occupancy reporting depends on a local patch in `managed_components/`

**This build depends on a hand-patched file inside `managed_components/`,
which is gitignored and does not survive a clean checkout or component
re-fetch.** Without it the occupancy endpoint silently stops reporting: the
value appears to be written (`attribute::update()` would return `ESP_OK` if
it were used) but every read — including a controller's live read of
`2/1030/0` — returns `0`, and Home Assistant's occupancy `binary_sensor`
never changes state.

### Why the patch is needed

Reads for a *registered* cluster are served by the cluster object, not by
esp_matter's own attribute store (`esp_matter_data_model_provider.cpp`,
`ReadAttribute`):

```cpp
if (auto *cluster = mRegistry.Get(request.path); cluster != nullptr) {
    return cluster->ReadAttribute(request, encoder);   // registered cluster wins
}
```

`OccupancySensing` is registered, so `Occupancy` lives in
`OccupancySensingCluster::mOccupancy`. `attribute::update()` writes a
parallel store that is never consulted for this cluster, so application
writes would no-op while still reporting success. The correct setter is the
cluster's own `SetOccupancy()`, which also raises the report via
`NotifyAttributeChanged()` — but the cluster instance lives in a file-local
`gServers` map with no public accessor.

Confirmed independently upstream:
[espressif/esp-matter#1738](https://github.com/espressif/esp-matter/issues/1738)
(open) traces the identical bug. Checked on `release/v1.6` (in use) — same
private structure, so upgrading esp_matter does not fix it on its own; worth
re-checking that issue before bumping the dependency.

### How it's applied

The root `CMakeLists.txt` appends `patches/esp_matter_occupancy_accessor.cpp.in`
to `managed_components/espressif__esp_matter/components/esp_matter/data_model_provider/clusters/occupancy_sensing/integration.cpp`
**after** `project()` runs (that call is what triggers the component manager
to fetch `esp_matter`; patching earlier would silently no-op on a fresh
checkout). It is idempotent (skipped if already applied) and hard-fails the
build with a clear message if the append doesn't land — so a broken patch is
a loud configure-time error, not a silent runtime no-op.

`matter_report_occupancy()` in `main/matter_setup.cpp` forward-declares the
appended accessor and calls `SetOccupancy()` on it, scheduled onto the
Matter thread via `SystemLayer().ScheduleLambda()` (the radar task must not
touch cluster state directly).

Lifted verbatim from `esp-demo-matter`, which hit and fixed this first.

## Radar — no calibration, no warm-up delay

`main/ld2410_bridge.cpp` issues no sensor-side configuration at all — no
gate/sensitivity setup, no baud change. The LD2410 runs on its factory
defaults and starts streaming target frames within about a second of
power-on, so the firmware needs no warm-up delay before the first read.

Presence flicker is absorbed entirely in software: `RADAR_NO_ONE_WINDOW_S`
(`app_config.h`, default 5 s) keeps reporting "occupied" for that many
seconds after the last frame that showed a target, since the `ld2410c`
component has no equivalent sensor-side hold setting. Raise it if occupancy
clears while someone in the room is holding still enough to occasionally
drop below the sensor's noise floor.

The radar path is fully decoupled from the relay: `on_radar_presence()` in
`app_main.cpp` only calls `matter_report_occupancy()`. It never touches
`RelaySet()`. Coupling presence to the load, if wanted, belongs in a
controller automation (Home Assistant etc.), not in this firmware. A missing
or unresponsive radar is non-fatal — `radar_bridge_init()` logs a warning and
the relay/commissioning continue normally.

## Mains safety

The plug's low-voltage section sits close to mains potential. Never connect
USB to the XIAO while the plug is connected to mains. Develop with the board
USB-powered and mains disconnected. On USB-only bench power the relay coil
(fed from a mains-derived rail) will not physically click even though the
GPIO toggles correctly — the onboard LED (GPIO15) is the only feedback that
a toggle actually landed in that case.

## Matter data model

- **Endpoint 1** = `on_off_plug_in_unit` (0x010A). `cfg.on_off_lighting.start_up_on_off
  = nullable<uint8_t>()` is a deliberate fix: the Matter spec treats a null
  StartUpOnOff as "restore the previous value on power-up", but esp_matter
  defaults this field to 0 ("come up Off"), which would discard persisted
  relay state on every power cycle.
- **Endpoint 2** = `occupancy_sensor` (0x0107). `occupancy_sensor_type[_bitmap]`
  is set to `kPir` (the legacy pre-1.4 enum has no radar value, and Home
  Assistant expects `kPir` for an occupancy `binary_sensor`), while
  `feature_flags` is set to `kRadar` (the newer Matter 1.4 Feature bitmap) —
  `create()` hard-asserts if `feature_flags` carries no recognised bit, so
  this is not optional.

No ZAP GUI or hand-maintained `.matter`/`.zap` file — esp_matter builds the
data model programmatically from the `config_t` structs and `cluster::*::create()`
/ `endpoint::*::create()` calls in `matter_setup.cpp`'s `create_endpoints()`.

Transport is **Matter over Thread**, FTD (always-on, no ICD/sleep) — see
`sdkconfig.defaults`'s comment for the reasoning (mains-powered, no reason to
trade latency for sleep). Requires a Thread Border Router on the network to
commission.
