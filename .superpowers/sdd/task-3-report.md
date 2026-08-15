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

## Review fixes

### RED

After adding tests for a four-page registry and an empty registry, the first
covering build was run with:

```text
cmake -S tests/host -B build-host && cmake --build build-host
```

It failed as expected because `CarouselState` had no `page_count` member:

```text
error: no member named 'page_count' in 'app_core::CarouselState'
```

### GREEN and fixes

- `CarouselState.page_count` now composes the pure controller with the current
  registry size; four-page previous wrapping works and count zero makes
  `tick`, `next`, and `previous` safe no-ops.
- Tests assert just-before and exact thresholds: Home 29,999/30,000 ms and
  data pages 11,999/12,000 ms, plus backward-time safety.
- Fixture regression coverage now asserts Clock Hero, TAIEX 24,334/+0.52%,
  TW50 +0.44%/20,871, all US fields, current Taipei weather, all seven
  forecast entries, indoor 24.8/57, and exact non-empty intraday samples.
- Taiwan-session coverage asserts the complete ordered page vector.
- The public no-op `observe` method was removed; cycle stability is proven by
  mutating the held snapshot after `begin_cycle` and checking the registry
  order remains unchanged until the next `begin_cycle`.

Covering commands:

```text
cmake -S tests/host -B build-host && cmake --build build-host
ctest --test-dir build-host --output-on-failure
100% tests passed, 0 tests failed out of 1
./build-host/host_tests
16 cases, 0 failures
git diff --check
```
