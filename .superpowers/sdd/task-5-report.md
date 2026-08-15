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
