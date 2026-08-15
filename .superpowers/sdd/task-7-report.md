# Task 7 report

Status: implemented and verified. The real startup lifecycle now checks PSRAM, initializes the display and LVGL, performs a read-only PCF85063 read on GPIO13/GPIO14, starts buttons only after mandatory startup, and schedules the retained UI runtime on one 100 ms LVGL timer. Manual KEY/BOOT navigation, 60 s pause/timeout, 30/12 s dwell, cycle-boundary registry rebuilds, minute clock refresh, renderer failure skipping, fallback clock, structured transitions, and startup diagnostics are wired.

TDD evidence:

- RED: `cmake --build build-host-task7-red` failed because `RtcDateTime`/`decode_pcf85063` were not yet implemented.
- GREEN: `ctest --test-dir build-host-task7-calendar --output-on-failure` passed 31/31 after BCD, range, and calendar validation.

Build and safety evidence:

- `./scripts/idf.sh build` passed; app binary `0xaec20` bytes with 77% partition free.
- `./scripts/verify-factory-backup.sh` passed: 16 MiB, SHA-256 `68db31b92d8a37bd321101d9ffb093bf2f3213d3e0bf111368e9a8f59919650f`.
- `git diff --cached --check` passed. Static audit found no PWR use and no GPIO0/GPIO18 output configuration; the RTC path only uses `i2c_master_transmit_receive` for register-pointer readback.
- `build/layout_carousel.map` retains `app_main -> ui::start -> lv_timer_create`, `ui_app.cpp` references `render_page`/carousel/registry, and the linked ELF contains the UI call path.

Commit: pending checkpoint commit in the parent task.
