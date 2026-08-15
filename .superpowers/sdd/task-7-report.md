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

Commit: `0fd47b8` (base integration checkpoint); review-fix commit follows.

## Review fixes

- Initial UI host/context creation, registry setup, and first Home render now run synchronously under the LVGL lock in `ui::start`; `false` propagates through `app_main` to `fatal_loop` before the 100 ms timer is created. The map call path retains `app_main -> ui::start -> initialize_runtime -> render_page`.
- `UiContext` now owns a staged/current clock-label pointer. Home and data mast renderers register it during replacement staging, root deletion/reset clears it, and minute refresh calls only `update_visible_clock`/`lv_label_set_text`; the minute branch contains no `render_page`, `lv_obj_create`, or replacement deletion.
- Calendar advancement is centralized in portable `app_core::advance_rtc_datetime`/`days_in_month`, covered by a leap-day/midnight host test. The cycle diagnostic is emitted only after synchronous cycle-1 registry construction succeeds.
- Review RED: the new host test initially failed to compile without `advance_rtc_datetime`; GREEN: host tests pass 32/32. Final firmware build passes with app binary `0xaede0` bytes; backup verifier and `git diff --check` remain green.
- Final map/static evidence: `app_main.cpp.obj` references `_ZN2ui5start...`; linked map retains `ui::start` at `0x4200b1ec`, `initialize_runtime`, and `fatal_loop`. The minute branch calls only `update_visible_clock`; `render_page`/`lv_obj_create` occur only in startup/transition paths, while `update_visible_clock` links to `lv_label_set_text`.
