Status: complete.

## RED

After adding the four button-filter cases and host target wiring, the required
host build failed because `components/board_rlcd/include/board_buttons.hpp`
did not yet exist. This was the expected missing-feature failure.

## GREEN

`ButtonFilter` is dependency-free and samples active-low KEY/BOOT inputs every
10 ms. Three stable samples (30 ms) are required for each transition; a
stable press followed by a stable release emits exactly one event. The tests
cover bounce, held input, BOOT previous, simultaneous release ordering, and no
repeat while held. The board implementation uses a static eight-event
FreeRTOS queue, drops only the newest event on overflow with a warning, and
keeps UI work out of the polling task.

Verification:

```text
cmake -S tests/host -B build-host && cmake --build build-host
ctest --test-dir build-host --output-on-failure
100% tests passed, 0 tests failed out of 1
./build-host/host_tests
20 cases, 0 failures
./scripts/idf.sh build
Project build complete.
```

The requested pin audit command reports only pin constants, input polling, and
the input-only GPIO configuration; no GPIO0/GPIO18 output configuration is
present. GPIO0 retains pull-up-only input behavior and the required ROM strap
comment. No hardware was accessed or flashed.
