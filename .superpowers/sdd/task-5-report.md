Status: complete.

## RED

The new portable geometry test was wired into the host target before the UI
implementation existed. `cmake --build build-host --clean-first` failed with
`fatal error: 'ui_theme.hpp' file not found`, proving the test exercised the
missing feature rather than passing accidentally.

## GREEN

Task 5 adds the `ui` ESP-IDF component with a single monochrome theme: 400x300
canvas, 6 px safe margins, one-pixel separators, black/white styles, no card
shadows or animation, and no board-layer dependency. It provides reusable
labels, dividers, line-segment weather/temperature/humidity icons, five
bottom-right page dots, and an inverse `BOOT  ‹   AUTO   ›  KEY` overlay that
deletes itself after 2,000 ms without reserving layout space.

Clock Hero uses the configured Montserrat 48 font, strips seconds, fills the
left canvas with date/status/update-age/next-event information, and fills the
three right Weather/Indoor/Market cells with vertically centered values. The
page renderer builds a hidden replacement root, deletes the previous root,
then reveals the replacement; the caller owns the LVGL lock. Debug assertions
guard the safe rectangle and replacement-root geometry.

Verification:

```text
cmake --build build-host --clean-first
ctest --test-dir build-host --output-on-failure
100% tests passed, 0 tests failed out of 1
./scripts/idf.sh build
Project build complete.
layout_carousel.bin binary size 0x309d0 bytes; 0x2cf630 bytes (94%) free.
git diff --check
```

No hardware was accessed or flashed. `main/app_main.cpp` remains the requested
log-only smoke entry. Static UI audit found no GPIO, PWR, network, NVS, OTA,
audio, sensor, or Flash operations.

## Review fixes

- Added `ui` to `main`'s private component requirements. A serialized forced
  component build compiled all three UI translation units against LVGL 9.3:
  `ui_theme.cpp.obj`, `render_shared.cpp.obj`, and `render_home.cpp.obj`, then
  reported `Built target __idf_ui`. The subsequent full build also completed
  and retained the 3 MB partition fit (`0x309d0`, 94% free).
- Kept the Home page as Clock Hero. `render_mast()` is now an explicitly
  exported helper for Task 6, while `navigation_overlay()` remains an exported
  callable for Task 7; neither is invoked during boot or by `app_main`.
- Added portable right-cell/content geometry invariants and recursive debug
  LVGL coordinate assertions after `lv_obj_update_layout()`. Tile content is
  centered by a shared content rect and asserts that no footer band is
  reserved.
- Replaced the process-global root with owner state stored on the stable host's
  reserved LVGL user-data slot. Detached staging screens build the replacement
  before reparenting, swapping, and deleting only the prior owned root; host
  deletion releases the owner state.
- Overlay timer state is tied to an overlay delete event. Deleting a page/root
  cancels the timer, while the live overlay still self-deletes on its one-shot
  2,000 ms timer without layout height.
- Sync text now derives source from `ClockData` and reports unknown age as
  `AGE --`; next-event text derives market display/index fields, and weather
  status derives the snapshot rain probability. No model fields were added or
  mutated.

Review-fix verification:

```text
RED: test_ui_theme.cpp failed before helpers existed (missing right_tile_cells,
tile_content_is_centered, and tile_content_has_no_reserved_footer).
GREEN: cmake --build build-host --clean-first
GREEN: ctest --test-dir build-host --output-on-failure
       100% tests passed, 0 tests failed out of 1
GREEN: serialized forced UI target build; Built target __idf_ui
GREEN: ./scripts/idf.sh build; Project build complete
GREEN: git diff --check

## Review fixes 2

- Migrated page ownership from host user-data discovery to an explicit
  caller-owned `UiContext` passed to `render_page()` and the context overload
  of `navigation_overlay()`. `init_context()` registers a host delete hook;
  `reset_context()` removes the hook, deletes the owned root, and clears all
  fields. Host deletion clears the context without dereferencing or probing
  arbitrary pointers. No process-global owner map remains.
- Captured API-migration RED when the host geometry test referenced the missing
  `UiContext` and `context_ready()` symbols. GREEN host verification then
  passed after the API was added.
- The serialized full firmware rebuild compiled the changed UI sources against
  LVGL 9.3 and reported `Built target __idf_ui`; the final build completed with
  `0x309d0` bytes and 94% factory-partition headroom. The UI component target
  was also present in the forced target invocation; no UI source was linked to
  `app_main` and no hardware was accessed.
```
