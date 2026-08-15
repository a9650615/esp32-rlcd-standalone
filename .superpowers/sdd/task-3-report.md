# Task 3 report

Status: complete.

## RED

The first host configure was run before any `app_core` implementation existed:
`cmake -S tests/host -B build-host-red` failed because the required
`components/app_core/app_snapshot.cpp` source was absent.

## GREEN

The dependency-free host runner now reports 12 named registry and carousel
cases. `cmake -S tests/host -B build-host && cmake --build build-host` and
`ctest --test-dir build-host --output-on-failure` pass with `100% tests passed,
0 tests failed`.

The implementation provides deterministic mock snapshot fixtures, availability
filtering, cycle-boundary scenario ordering, and pure automatic/manual carousel
transitions. `app_core` includes no LVGL or ESP-IDF headers.

## Firmware

`./scripts/idf.sh build` passes with ESP-IDF 5.5.2 and links `__idf_app_core`.
No hardware was accessed or flashed.

## Scope note

`main/CMakeLists.txt` now declares a private `app_core` dependency so the
temporary smoke firmware exercises the new component boundary during linking.
