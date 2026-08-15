# RLCD System Tray and Battery Sensing

Date: 2026-08-15
Target: Waveshare ESP32-S3-RLCD-4.2
Status: Approved design, fully implemented in the working tree. Tray geometry and per-page visibility (`ui::page_shows_tray`, `ui::content_bounds`, `ui::system_tray_layout`), battery conversion math (`app_core::battery_millivolts`, `battery_percent`, `battery_reading_valid`), live ADC sampling (`board::battery_read`) with its 30 s task, and the repaint-throttling rule in `ui_app.cpp`'s snapshot-consumption path (`ui::update_visible_fields`, `set_label_text_if_changed`) are all landed.

Verified off-device only: 90 host test cases with 0 failures and a clean ESP-IDF build. **Nothing here has been flashed or seen on the physical panel.** The battery percentage is unvalidated against a multimeter and `CONFIG_BATTERY_CALIBRATION_PERMILLE` is still at its untuned default of 1000, so reported charge should be treated as approximate until the calibration row in the acceptance checklist is completed.

## Goal

Give every non-Home page a persistent system tray reporting clock, network state, and battery, without repainting the whole reflective panel on every one of the frequent background updates that feed it (Wi-Fi state changes, a battery sample every 30 s, and future live-data providers).

## Approved decisions

### 2. Persistent system tray; Home stays tray-free

**Approved.** Every page except Home shows a 28 px system tray at the top: time flush left, network status filling the middle, battery flush right. Home deliberately does **not** show the tray and keeps its full Clock Hero canvas — Home already has its own always-visible clock as the page's entire purpose, so a second clock in a tray above it would be redundant and would shrink the one thing Home is for.

Which pages carry the tray is decided in exactly **one place**: `ui::page_shows_tray(app_core::PageId)` in `components/ui/include/ui_data.hpp`. It is a one-line `constexpr` function (`return page != app_core::PageId::Home;`). No renderer hand-checks the page kind itself; every page derives its content area by calling `ui::content_bounds(bounds, page)`, which either returns the untouched bounds (Home) or slices `kSystemTrayReservedHeight` off the top (every other page). Adding, removing, or reordering pages never requires touching more than this one function.

**Geometry**, from `components/ui/include/ui_data.hpp`:

| Constant | Value | Meaning |
| --- | --- | --- |
| `kSystemTrayHeight` | 28 px | The tray band itself (time/network/battery cells). |
| `kSeparatorWidth` | 1 px | Divider line directly under the tray. |
| `kSystemTrayContentGap` | 7 px | Gap between the separator and page content. |
| `kSystemTrayReservedHeight` | 36 px (28 + 1 + 7) | Total height `content_bounds` removes from the top of a tray page. |

This total is deliberately sized to exactly match the previous single-band "mast" footprint (28 px band + 8 px trailing gap = 36 px) so that splitting the mast into an explicit tray band + 1 px separator + 7 px gap leaves data-page content pixel-identical to before this change — market charts, weather, and indoor pages did not shrink or shift.

Cell layout (`ui::system_tray_layout`): time is flush left at a fixed 60×25 px cell; battery is flush right in a fixed 64 px cell; network status fills whatever width is left between them. All three cells sit inside the tray band with a 3 px top inset and 18 px height, and a `static_assert` block in `ui_data.hpp` proves at compile time that the three cells never overlap and stay within the safe canvas — there is no runtime path where a long network string can be clipped into the battery cell.

Tray content:

- **Time** — `HH:MM`, same minute-precision formatting as the existing clock label (`ui::format_minute_clock`).
- **Network** — one of three fixed strings: `SETUP` (setup mode active), `WIFI` (`snapshot.setup.connected`), `NO WIFI` (neither — STA is retrying or has no credentials).
- **Battery** — `"BAT NN%"` right-aligned. When `snapshot.battery.valid` is false (Decision 3's 2500 mV floor), the cell renders **blank**, not `BAT 0%` — an unplugged/uninstalled battery must never look like a nearly-dead one.

The Setup page also carries the tray (it is not Home), so `setup_layout` is computed against the tray-reduced `content_bounds`, not the full canvas; this is covered by the same compile-time `static_assert`s as the other tray pages.

### 3. Lithium battery sensing

**Approved.** The board now has a physical Li-ion cell installed. Sensing follows the pinned first-party divider research (`.agents/skills/esp32-s3-rlcd-dev/references/official-development.md`): GPIO4, ADC1 channel 3, behind the board's onboard 3x resistor divider.

**Read path** (`components/board_rlcd/battery.cpp`, `board::battery_read`):

1. ADC1 oneshot, 12-bit width, 12 dB attenuation (full range for the divided cell voltage).
2. Calibration: `adc_cali_curve_fitting` when the SoC's efuse curve-fitting scheme is available; otherwise a raw-scaling fallback against the attenuation's nominal ~3.3 V full scale. The fallback is logged once as a warning, not treated as an error.
3. 8 samples averaged per read to damp ADC noise (a plain average, not a filter).
4. The averaged ADC millivolts are scaled by 3 (the divider) and by the calibration factor below to get cell millivolts.

**Conversion math is pure and host-tested**, in `components/app_core/app_snapshot.{hpp,cpp}` — no ESP-IDF or ADC dependency, so it is exercised by `tests/host/test_battery.cpp` on every host build:

- `battery_millivolts_scaled(adc_millivolts, calibration_permille)` — `adc_millivolts * 3 * calibration_permille / 1000`.
- `battery_millivolts(adc_millivolts)` — the production entry point; always applies `CONFIG_BATTERY_CALIBRATION_PERMILLE` (see Calibration below).
- `battery_percent(millivolts)` — a **piecewise single-cell Li-ion discharge table**, not a linear map. Breakpoints run from 4200 mV → 100% down to 3000 mV → 0%, with denser points in the 3.7–4.0 V knee where Li-ion voltage sags fastest per percent. Values between breakpoints are linearly interpolated within that segment only.
- `battery_reading_valid(millivolts)` — `millivolts >= kBatteryValidThresholdMillivolts` (2500 mV). Below this, no battery is assumed to be installed/connected; the caller (`board::battery_read`) leaves `BatteryData::valid` false rather than reporting a misleading near-zero percentage, and the tray renders a blank battery cell (Decision 2 above).

**Sampling cadence**: a dedicated FreeRTOS task (`battery_monitor_task` in `main/app_main.cpp`) calls `board::battery_read` every 30 s (`kBatterySamplePeriodMs`) and, on success, calls `wifi_provision::set_battery(battery)`, which merges the reading into the shared `AppSnapshot` and republishes it through `ui::publish_snapshot()` — the same single publish path Wi-Fi state changes use, so there is exactly one snapshot publisher, not two competing ones.

### Calibration procedure

Real dividers run a few percent off the nominal 3x ratio. `CONFIG_BATTERY_CALIBRATION_PERMILLE` (default `1000`, i.e. no trim) exists precisely so this is a one-line `menuconfig` change, not a firmware edit:

1. Read the board's currently-reported battery millivolts (tray, Setup page, or serial log).
2. Measure the actual cell voltage with a multimeter across the battery terminals.
3. Set `CONFIG_BATTERY_CALIBRATION_PERMILLE = 1000 * multimeter_mV / reported_mV` via `idf.py menuconfig` under "Battery sensing".
4. Re-flash; the reported value should now track the multimeter within normal ADC noise.

The host build always uses the untrimmed default (`1000`) regardless of any device-specific `sdkconfig`, so `battery_millivolts`'s behavior stays deterministic and testable off-device; only `board::battery_read` on real hardware is affected by the Kconfig value.

**Carried-forward board pitfall** (already recorded in the board skill, restated here because it now applies to every battery-sensing session): after installing the cell, connect Type-C first to start the power path, then unplug to verify battery-only operation. Skipping the Type-C step first can leave the board unable to power on from the battery alone.

### 4. Repaint throttling is a standing architectural constraint

**Approved.** See the shared rationale and rule already recorded in `docs/design/specs/2026-08-15-rlcd-wifi-provisioning.md`'s "Update boundary contract" section — this document does not duplicate it, only calls out the battery-specific consequence: the battery task publishes every 30 s, forever, for the lifetime of the device. If every publish triggered a full `render_page` rebuild, the panel would visibly flash every 30 s indefinitely, not just during the short-lived Wi-Fi setup window.

The rule stays the same regardless of which provider publishes: the LVGL timer rebuilds a page only on a genuine page-identity change (a different page is now showing); any other published change — including a battery percentage that moved by one point — updates only the specific label(s) whose text actually differs, following the existing `update_visible_clock` pattern (look up the cached `lv_obj_t*` label, compare/set its text, do not touch the rest of the tree).

This is recorded here explicitly as a constraint binding **all future live-data providers** (weather, market, SNTP, indoor sensor) that will be layered onto the same `AppSnapshot` + `publish_snapshot()` path: none of them may assume a full repaint is acceptable on their own update cadence, and most of them will publish far more often than every 30 s.

## Acceptance criteria

- [ ] `ui::page_shows_tray` is the only place page-tray membership is decided; no renderer contains its own page-kind check for tray visibility.
- [ ] Home renders with no tray and the full safe-canvas height for its Clock Hero content.
- [ ] Every other page (TaiwanMarket, UsMarket, Weather, Indoor, Setup) renders the tray and receives content bounds `kSystemTrayReservedHeight` (36 px) shorter than Home's, with the same bottom edge as before (page dots do not move).
- [ ] Tray time, network, and battery cells never overlap and stay within the safe canvas (compile-time `static_assert`s already cover this; hardware photo confirms no clipping).
- [ ] Network cell shows exactly one of `SETUP` / `WIFI` / `NO WIFI` matching the current Wi-Fi provisioning state.
- [ ] Battery cell shows `BAT NN%` when `battery.valid` is true and renders blank (not `BAT 0%`) when false.
- [ ] `board::battery_read` samples GPIO4 / ADC1 channel 3, applies curve-fitting calibration when available and the raw-scaling fallback otherwise, and averages 8 samples per read.
- [ ] `battery_percent` follows the piecewise discharge table (verify at minimum the 4200→100 and 3000→0 endpoints and one interpolated midpoint against the host test).
- [ ] A reading below 2500 mV (or no battery installed) leaves `BatteryData::valid` false end to end, from ADC read through to a blank tray cell.
- [ ] `battery_monitor_task` samples every 30 s and reaches the tray through the same `ui::publish_snapshot()` path as Wi-Fi state.
- [ ] `CONFIG_BATTERY_CALIBRATION_PERMILLE` changes the on-device reported millivolts proportionally and does not affect the host build's test results.
- [ ] A battery-only publish (no page-identity change) does not trigger `render_page`; only the battery label's text changes when its value differs from what's currently displayed.
