# RLCD Layout Carousel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build and flash a safe, standalone first firmware slice that renders five dense monochrome layouts on the ESP32-S3-RLCD-4.2, advances them automatically, and supports KEY/BOOT manual navigation without compromising ROM download recovery.

**Architecture:** Keep the vendor-derived ST7305/LVGL board port isolated from a portable application core. A single immutable `AppSnapshot` feeds a page registry and carousel controller; LVGL renderers consume only snapshot data plus layout bounds. All page changes happen on the LVGL thread, while GPIO callbacks only enqueue button events.

**Tech Stack:** ESP-IDF 5.5.2, C++17, LVGL 9.3.0, FreeRTOS, Espressif GPIO/SPI/PSRAM APIs, CMake/CTest for host logic tests, esptool for non-destructive device checks.

**Global Constraints:** Use Waveshare BSP logic pinned to commit `eb1f63427d735a22b9c30e22fa63ebddae1834d3`. Target 400×300 landscape, QIO 16 MB Flash, octal 8 MB PSRAM. Never erase Flash. Never drive or reconfigure PWR. Keep GPIO0 as an input with its normal pull-up; assign only a debounced short-release event after application startup. Verify the existing 16 MB factory backup before the first write. Do not add Wi-Fi, API access, captive portal, NVS, OTA, SD, audio, or live sensor acquisition in this slice. Do not initialize a Git repository or create commits unless the user explicitly approves it.

---

## File Structure

```text
CMakeLists.txt                              ESP-IDF project entry
sdkconfig.defaults                         esp32s3, 16 MB Flash, octal PSRAM, LVGL font flags
partitions.csv                             one 3 MB factory app plus NVS/PHY slots
main/
  CMakeLists.txt                           app entry registration
  idf_component.yml                        exact ESP-IDF/LVGL requirements
  app_main.cpp                             startup, fatal checks, service wiring
components/
  board_rlcd/
    CMakeLists.txt
    UPSTREAM.md                            exact Waveshare source provenance
    include/board_pins.hpp                 audited pin constants
    include/display_port.hpp               ST7305 framebuffer API
    include/lvgl_port.hpp                  LVGL task/lock/flush API
    include/board_buttons.hpp              debounced short-release event API
    display_port.cpp                       vendor-derived ST7305 transport
    lvgl_port.cpp                          full-frame PSRAM buffers and display flush
    board_buttons.cpp                      KEY/BOOT input polling and queue
  app_core/
    CMakeLists.txt
    include/app_snapshot.hpp               immutable UI data contract
    include/page_registry.hpp              page descriptors and scenario ordering
    include/carousel_controller.hpp        timing/manual-mode state machine
    app_snapshot.cpp                       deterministic five-page fixtures
    page_registry.cpp                      availability and cycle-boundary ordering
    carousel_controller.cpp                pure navigation transitions
  ui/
    CMakeLists.txt
    include/ui_app.hpp                     LVGL UI lifecycle
    include/ui_theme.hpp                   monochrome metrics/styles
    ui_app.cpp                              LVGL timer, event drain, atomic page replace
    ui_theme.cpp                            reusable styles and primitive icons
    render_home.cpp                        Clock Hero home
    render_market.cpp                      Taiwan/US quote plus tall polyline chart
    render_weather.cpp                     current and seven-day weather
    render_indoor.cpp                      temperature/humidity page
    render_shared.cpp                      clock mast, side tiles, dots, transient overlay
tests/host/
  CMakeLists.txt                           portable logic test target
  test_main.cpp                            tiny assertion runner
  test_page_registry.cpp                   omission and priority tests
  test_carousel.cpp                        dwell/manual-resume tests
scripts/
  bootstrap-idf.sh                         local pinned ESP-IDF installer
  idf.sh                                   project-local idf.py wrapper
  verify-factory-backup.sh                 size/hash gate before flashing
docs/hardware/first-layout-checklist.md     ten-minute physical acceptance record
```

## Task 1: Add the reproducible toolchain and safe project shell

**Files:**
- Create: `CMakeLists.txt`
- Create: `sdkconfig.defaults`
- Create: `partitions.csv`
- Create: `main/CMakeLists.txt`
- Create: `main/idf_component.yml`
- Create: `scripts/bootstrap-idf.sh`
- Create: `scripts/idf.sh`
- Create: `scripts/verify-factory-backup.sh`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: macOS shell, Git, project root, existing factory backup.
- Produces: `./scripts/idf.sh <idf.py args>` and a zero-write backup verification gate.

- [ ] Create a project-local installer that clones tag `v5.5.2` into ignored `.tools/esp-idf` and runs `install.sh esp32s3` only when needed:

```bash
#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
idf_dir="$project_dir/.tools/esp-idf"
if [[ ! -d "$idf_dir/.git" ]]; then
  git clone --depth 1 --branch v5.5.2 https://github.com/espressif/esp-idf.git "$idf_dir"
fi
git -C "$idf_dir" describe --tags --exact-match | grep -qx 'v5.5.2'
"$idf_dir/install.sh" esp32s3
```

- [ ] Add `scripts/idf.sh`; it sources `.tools/esp-idf/export.sh` without mutating the caller's shell, then `exec idf.py "$@"`.
- [ ] Pin managed dependencies exactly:

```yaml
dependencies:
  idf: "==5.5.2"
  lvgl/lvgl: "==9.3.0"
```

- [ ] Add the minimum board configuration:

```ini
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_FREERTOS_HZ=1000
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_28=y
CONFIG_LV_FONT_MONTSERRAT_48=y
```

- [ ] Use a conservative non-OTA partition table: NVS at `0x9000/0x6000`, PHY at `0xf000/0x1000`, factory app at `0x10000/0x300000`.
- [ ] Add `verify-factory-backup.sh` that checks exact byte count `16777216` and SHA-256 `68db31b92d8a37bd321101d9ffb093bf2f3213d3e0bf111368e9a8f59919650f`; exit non-zero on either mismatch.
- [ ] Ignore `.tools/`, `build/`, `build-host/`, `managed_components/`, `dependencies.lock`, and `sdkconfig` while retaining `sdkconfig.defaults`.
- [ ] Run the no-network safety gate first:

```bash
./scripts/verify-factory-backup.sh
```

Expected: `factory backup verified: 16777216 bytes, sha256 68db31b92d8a37bd321101d9ffb093bf2f3213d3e0bf111368e9a8f59919650f`.

- [ ] Bootstrap and verify the pinned toolchain:

```bash
./scripts/bootstrap-idf.sh
./scripts/idf.sh --version
```

Expected: `ESP-IDF v5.5.2`.

- [ ] Commit checkpoint only if the user has explicitly approved Git setup: `git add CMakeLists.txt sdkconfig.defaults partitions.csv main scripts .gitignore && git commit -m "build: bootstrap pinned ESP-IDF firmware project"`.

## Task 2: Port and audit the display/board layer

**Files:**
- Create: `components/board_rlcd/CMakeLists.txt`
- Create: `components/board_rlcd/UPSTREAM.md`
- Create: `components/board_rlcd/include/board_pins.hpp`
- Create: `components/board_rlcd/include/display_port.hpp`
- Create: `components/board_rlcd/include/lvgl_port.hpp`
- Create: `components/board_rlcd/display_port.cpp`
- Create: `components/board_rlcd/lvgl_port.cpp`

**Interfaces:**
- Consumes: LVGL RGB565 full-frame flushes and fixed RLCD pin constants.
- Produces: `board::display_init()`, `board::lvgl_init()`, `board::lvgl_lock()`, and `board::lvgl_unlock()`.

- [ ] Record upstream repo, commit, and original example paths in `UPSTREAM.md`; list intentional changes: namespaces, error returns, PSRAM checks, logging, and removal of unrelated generated UI.
- [ ] Define pins once and add compile-time guards:

```cpp
namespace board {
inline constexpr gpio_num_t kDisplaySck = GPIO_NUM_11;
inline constexpr gpio_num_t kDisplayMosi = GPIO_NUM_12;
inline constexpr gpio_num_t kDisplayDc = GPIO_NUM_5;
inline constexpr gpio_num_t kDisplayCs = GPIO_NUM_40;
inline constexpr gpio_num_t kDisplayReset = GPIO_NUM_41;
inline constexpr gpio_num_t kDisplayTe = GPIO_NUM_6;
inline constexpr gpio_num_t kKey = GPIO_NUM_18;
inline constexpr gpio_num_t kBoot = GPIO_NUM_0;
inline constexpr int kWidth = 400;
inline constexpr int kHeight = 300;
static_assert(kBoot == GPIO_NUM_0, "BOOT recovery pin changed");
}
```

- [ ] Port `display_bsp.cpp/.h` from the pinned Waveshare LVGL 9 example, preserving ST7305 init bytes, 10 MHz SPI, landscape LUT, 15,000-byte 1-bit framebuffer, and full `RLCD_Display()` transfer.
- [ ] Replace constructor-time `ESP_ERROR_CHECK` with explicit `esp_err_t init()` so startup can log a clear fatal error; validate every allocation.
- [ ] Port the LVGL task and mutex. Allocate two 400×300 RGB565 render buffers with `MALLOC_CAP_SPIRAM`, return `ESP_ERR_NO_MEM` if either fails, and keep all `lv_timer_handler()` calls on the pinned LVGL task.
- [ ] Implement flush conversion with the official threshold and exact bounds:

```cpp
for (int y = area->y1; y <= area->y2; ++y) {
  for (int x = area->x1; x <= area->x2; ++x) {
    display.set_pixel(x, y, *pixels++ < 0x7fff ? Color::Black : Color::White);
  }
}
display.refresh();
lv_display_flush_ready(display_handle);
```

- [ ] Add startup diagnostics for chip target, Flash size, PSRAM size/free bytes, display buffer allocation, and LVGL task creation. Treat absent PSRAM and display initialization failure as fatal.
- [ ] Build without flashing:

```bash
./scripts/idf.sh set-target esp32s3
./scripts/idf.sh build
```

Expected: `Project build complete` and no compiler warnings from `board_rlcd`.

- [ ] Commit checkpoint if Git is approved: `git commit -am "feat: add audited RLCD display and LVGL board port"`.

## Task 3: Implement and host-test the immutable application model

**Files:**
- Create: `components/app_core/CMakeLists.txt`
- Create: `components/app_core/include/app_snapshot.hpp`
- Create: `components/app_core/include/page_registry.hpp`
- Create: `components/app_core/include/carousel_controller.hpp`
- Create: `components/app_core/app_snapshot.cpp`
- Create: `components/app_core/page_registry.cpp`
- Create: `components/app_core/carousel_controller.cpp`
- Create: `tests/host/CMakeLists.txt`
- Create: `tests/host/test_main.cpp`
- Create: `tests/host/test_page_registry.cpp`
- Create: `tests/host/test_carousel.cpp`

**Interfaces:**
- Consumes: deterministic scenario selection and monotonic milliseconds.
- Produces: immutable `AppSnapshot`, ordered available page IDs, and pure carousel transitions.

- [ ] Define value types only—no LVGL or ESP-IDF headers:

```cpp
enum class PageId { Home, TaiwanMarket, UsMarket, Weather, Indoor };
enum class DemoScenario { MorningAlert, TaiwanSession, NightSession };

struct AppSnapshot {
  ClockData clock;
  MarketData taiwan_market;
  MarketData us_market;
  WeatherData weather;
  IndoorData indoor;
  Availability availability;
  DemoScenario scenario;
};

struct PageDescriptor {
  PageId id;
  uint8_t dwell_seconds;
  bool (*is_available)(const AppSnapshot&);
};
```

- [ ] Build deterministic mock fixtures with Clock Hero, TAIEX `24,334 / +0.52%`, TW50 `+0.44% / 20,871`, a US index fixture, Taipei current weather plus seven days, indoor `24.8°C / 57%`, and fixed intraday polyline samples.
- [ ] Implement registry filtering: Home is always first; unavailable data pages are omitted; dynamic ordering is recomputed only by `begin_cycle(snapshot)`.
- [ ] Implement the pure controller contract:

```cpp
struct CarouselState {
  size_t index;
  uint64_t page_started_ms;
  uint64_t manual_until_ms;
  bool manual_mode;
};

Transition tick(CarouselState, uint64_t now_ms, uint8_t dwell_seconds);
Transition next(CarouselState, uint64_t now_ms);
Transition previous(CarouselState, uint64_t now_ms);
```

- [ ] Write failing registry tests first: all five pages present, unavailable page omitted, morning alert ordering, Taiwan-session ordering, night-session ordering, and no mid-cycle reorder.
- [ ] Write failing carousel tests first: Home advances at 30,000 ms; data advances at 12,000 ms; KEY goes next; BOOT wraps previous; manual action sets 60,000 ms pause; timeout returns Home and resumes auto.
- [ ] Use a dependency-free test runner that reports each named case and returns non-zero on failure.
- [ ] Run tests:

```bash
cmake -S tests/host -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

Expected: `100% tests passed, 0 tests failed`.

- [ ] Rebuild firmware to catch component-boundary errors: `./scripts/idf.sh build`.
- [ ] Commit checkpoint if Git is approved: `git commit -am "feat: add tested snapshot registry and carousel core"`.

## Task 4: Add safe KEY/BOOT event handling

**Files:**
- Create: `components/board_rlcd/include/board_buttons.hpp`
- Create: `components/board_rlcd/board_buttons.cpp`
- Modify: `components/board_rlcd/CMakeLists.txt`
- Modify: `tests/host/test_main.cpp`
- Create: `tests/host/test_button_filter.cpp`

**Interfaces:**
- Consumes: active-low GPIO18/GPIO0 samples every 10 ms.
- Produces: queue events `ButtonEvent::Next` and `ButtonEvent::Previous`, exactly once per stable short release.

- [ ] Extract a portable debounce state machine with 30 ms stable threshold and no long-press/double-click behavior in this slice.
- [ ] Test bouncing press/release sequences, held input, simultaneous keys, and exactly-one-event semantics before wiring GPIO.
- [ ] Configure both pins as `GPIO_MODE_INPUT`, `GPIO_PULLUP_ONLY`, active low. Start polling only after PSRAM/display/LVGL initialization so GPIO0 remains untouched during early boot.
- [ ] Use a fixed FreeRTOS queue of eight events. An overflow logs a warning and discards only the newest event; no UI operation may run in the polling task.
- [ ] Add a source comment immediately beside GPIO0 setup: `ROM download strap; short-release only; never add pulldown or long-press ownership.`
- [ ] Run `ctest --test-dir build-host --output-on-failure` and `./scripts/idf.sh build`.

Expected: all host tests pass; firmware links; pin audit finds no output configuration for GPIO0 or GPIO18:

```bash
rg -n 'GPIO_NUM_(0|18)|kBoot|kKey' components main
```

- [ ] Commit checkpoint if Git is approved: `git commit -am "feat: add recovery-safe button navigation events"`.

## Task 5: Build the shared monochrome UI system and Clock Hero

**Files:**
- Create: `components/ui/CMakeLists.txt`
- Create: `components/ui/include/ui_theme.hpp`
- Create: `components/ui/include/ui_app.hpp`
- Create: `components/ui/ui_theme.cpp`
- Create: `components/ui/render_shared.cpp`
- Create: `components/ui/render_home.cpp`

**Interfaces:**
- Consumes: `AppSnapshot`, `PageId`, `lv_obj_t* root`, and rectangular bounds.
- Produces: full-screen monochrome widgets with shared clock mast, tiles, dots, and temporary overlay.

- [ ] Define the visual constants in one place: 400×300 canvas, 6 px outer safe margin, 1 px separators, black/white only, no shadows, no rounded card backgrounds, no animation.
- [ ] Create reusable helpers for labels, thin dividers, simple line-based weather/temperature/humidity icons, five bottom-right page dots, and the inverse navigation overlay.
- [ ] Make the overlay self-delete after 2,000 ms and display `BOOT  ‹   AUTO   ›  KEY`; it must not reserve layout height when hidden.
- [ ] Implement Clock Hero as the default page: time is the largest type using Montserrat 48, no seconds, top-left dominant area; date/status remains adjacent; right-hand Weather, Indoor, and Market tiles fill their cells and vertically center their content.
- [ ] Fill the main area with useful date/day, update-age, and concise next-event information instead of a persistent footer.
- [ ] Add debug-only geometry assertions that every top-level object stays within `[6, 6, 394, 294]` and no right tile has empty reserved footer space.
- [ ] Implement `ui::render_page()` by creating into a detached replacement root, then atomically loading/deleting the old root under the LVGL lock.
- [ ] Build: `./scripts/idf.sh build`.

Expected: no LVGL API errors; firmware size fits the 3 MB factory partition.

- [ ] Commit checkpoint if Git is approved: `git commit -am "feat: render dense clock hero layout"`.

## Task 6: Render the four data pages

**Files:**
- Create: `components/ui/render_market.cpp`
- Create: `components/ui/render_weather.cpp`
- Create: `components/ui/render_indoor.cpp`
- Modify: `components/ui/render_shared.cpp`
- Modify: `components/ui/ui_app.cpp`

**Interfaces:**
- Consumes: data-page portions of the immutable snapshot.
- Produces: Taiwan market, US market, weather/forecast, and indoor pages using the same dense shell.

- [ ] Implement a 28 px-high persistent top mast: large `HH:MM` at upper-left, context/update state beside it, a fixed `Wi-Fi OFF` indicator for this offline slice, and indoor summary at upper-right.
- [ ] Give market primary content 72% width. Put index name/value/change in a compact block, then give the polyline chart all remaining vertical height; show 09:00/start, midpoint, and NOW/end labels.
- [ ] Render chart polylines from normalized integer samples with a one-pixel stroke and three sparse dotted grid lines; clamp all coordinates to the chart bounds.
- [ ] Fill and vertically center each right-side tile. Taiwan page side tiles: TW50, Taipei weather, Indoor. US page: secondary US index, New York weather fixture, Indoor.
- [ ] Implement Weather with a large current condition and seven equal forecast columns; use line icons and high/low/rain labels that remain legible at native resolution.
- [ ] Implement Indoor with the large `24.8°` reading, humidity `57%`, comfort band, and mini history line; reuse the right column for weather and market summaries.
- [ ] Use the availability flag demo build to prove the registry removes one page cleanly:

```bash
./scripts/idf.sh -D SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.demo-missing-page' build
```

Expected: build succeeds and serial diagnostics later report four registered pages, not an empty fifth page.

- [ ] Run normal build again: `./scripts/idf.sh fullclean && ./scripts/idf.sh build`.
- [ ] Commit checkpoint if Git is approved: `git commit -am "feat: add market weather and indoor layouts"`.

## Task 7: Wire automatic switching, real RTC fallback, and diagnostics

**Files:**
- Create: `components/ui/ui_app.cpp`
- Modify: `main/app_main.cpp`
- Modify: `components/ui/CMakeLists.txt`
- Modify: `components/app_core/app_snapshot.cpp`

**Interfaces:**
- Consumes: button queue, LVGL monotonic timer, optional PCF85063 time read.
- Produces: one running five-page carousel and structured serial evidence.

- [ ] In `app_main`, verify PSRAM, initialize display, initialize LVGL, build the initial snapshot, initialize buttons, then create the UI. Stop with a logged fatal loop on any mandatory failure.
- [ ] Add a minimal read-only I2C PCF85063 probe on SDA13/SCL14. If the RTC is absent or invalid, use firmware compile date/time plus monotonic elapsed seconds and show `RTC fallback` in serial diagnostics; do not write RTC registers.
- [ ] Create one LVGL timer firing every 100 ms. It drains button events, advances the pure carousel controller, rebuilds the page only on transitions, and refreshes visible clock text once per minute.
- [ ] Enforce dwell values from the registry: Home 30 seconds, every data page 12 seconds.
- [ ] On KEY short release, go next and show the 2-second overlay. On BOOT short release, go previous and show the overlay. Either event pauses auto mode for 60 seconds; expiry returns directly to Home and restarts the cycle.
- [ ] Recompute availability/priority only when wrapping from the last page into a new cycle. Log `cycle`, `page`, `reason`, `dwell_s`, and `manual_until_ms` on each transition.
- [ ] If a renderer reports failure, log its page ID and skip it for the current cycle; if all data pages fail, return Home.
- [ ] Run all automated checks:

```bash
cmake -S tests/host -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
./scripts/idf.sh build
./scripts/verify-factory-backup.sh
```

Expected: host tests 100% pass, firmware build completes, backup gate passes.

- [ ] Commit checkpoint if Git is approved: `git commit -am "feat: integrate automatic RLCD layout carousel"`.

## Task 8: Flash once and perform hardware acceptance

**Files:**
- Create: `docs/hardware/first-layout-checklist.md`
- Modify only if evidence requires it: UI renderer/theme files from Tasks 5–7.

**Interfaces:**
- Consumes: verified build artifacts, `/dev/cu.usbmodem1101`, factory backup, physical screen/button observations.
- Produces: flashed first-slice firmware and a completed ten-minute acceptance record.

- [ ] Re-resolve the connected port; do not assume it stayed unchanged:

```bash
ls /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null
```

Expected: exactly one intended ESP32-S3 port. Stop if ambiguous.

- [ ] Verify the backup immediately before the write: `./scripts/verify-factory-backup.sh`.
- [ ] Flash through the normal ESP-IDF target—never use `erase-flash` and never write a full-chip image:

```bash
./scripts/idf.sh -p /dev/cu.usbmodem1101 flash monitor
```

Expected serial evidence: correct chip/Flash/PSRAM, display initialized, five registered pages, `page=Home`, no allocation failure or reset loop. Exit monitor with `Ctrl-]`.

- [ ] Observe at least ten complete carousel cycles / ten minutes and record: all five pages appear; Home is visibly clock-first; the clock mast remains visible on data pages; charts use the available height; right tiles are vertically centered; no clipping/ghost corruption; dwell timing is 30/12 seconds.
- [ ] Press KEY ten times slowly and verify one next transition per release. Press BOOT ten times slowly and verify one previous transition per release. Confirm the 2-second overlay and 60-second manual timeout to Home.
- [ ] Power-cycle normally and confirm firmware returns to Clock Hero.
- [ ] Verify ROM download recovery without erasing or writing: hold BOOT, tap RESET/reattach as appropriate, then run:

```bash
python3 -m esptool --chip esp32s3 --port /dev/cu.usbmodem1101 chip-id
```

Expected: esptool connects and reports ESP32-S3. Reset normally afterward and confirm Clock Hero returns.

- [ ] If display proportions are unsatisfactory, change only metrics in `ui_theme.cpp`, rebuild, and repeat the visual checklist. Do not introduce live data services into this iteration.
- [ ] Document the recovery command but do not execute it unless needed:

```bash
python3 -m esptool --chip esp32s3 --port /dev/cu.usbmodem1101 write-flash 0x0 firmware/backups/waveshare-factory-full-flash-2026-08-15.bin
```

- [ ] Final verification: rerun host tests, firmware build, backup hash, and review the completed checklist for ten cycles with no unresolved failure.
- [ ] Commit checkpoint only if Git is approved and the physical checklist passes: `git commit -am "test: record first RLCD hardware layout acceptance"`.

## Completion Criteria

- The firmware builds reproducibly with ESP-IDF 5.5.2 and LVGL 9.3.0.
- The factory backup hash gate passes before every Flash write.
- Five mock-data pages render at 400×300 with Home clock-first, persistent time on data pages, tall market charts, filled/centered side tiles, no fixed footer, tiny page dots, and a temporary navigation overlay.
- Auto/manual timing and availability/priority behavior pass portable host tests.
- KEY and BOOT each generate one short-release navigation event; GPIO0 still enters ROM download mode.
- The physical unit completes the ten-minute/ten-cycle checklist without clipping, reset loops, corrupt refreshes, or loss of recovery access.
