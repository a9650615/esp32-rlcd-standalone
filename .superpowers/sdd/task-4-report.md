# Task 4 report: the two state machines

## What was built

- `components/app_core/include/now_playing_controller.hpp` (new, header-only):
  - `app_core::kNowPlayingSeizeSeconds` (60) and `app_core::kVolumeOverlayMs` (2000) constants.
  - `app_core::SeizeState` + `app_core::seize_tick(state, session_open, title, now_ms)`: decides when the
    now-playing page seizes the screen away from the carousel (on session open / new track) and when it
    releases it (60 s hold, or session close).
  - `app_core::VolumeOverlayState` + `app_core::volume_overlay_tick(state, volume, page_on_screen, now_ms)`:
    decides when the volume overlay pops up (on a genuine change, once a baseline is established) and when
    it times out (2 s) or is force-closed by leaving the page.
  - Both are pure functions over plain structs, modelled directly on `carousel_controller.hpp`, matching the
    brief's code verbatim (including its comments).
- `tests/host/test_now_playing.cpp`: added `#include "now_playing_controller.hpp"` to the include block
  (kept alphabetical between `media_registry.hpp` and `page_registry.hpp`), and appended the seven
  `HOST_TEST` cases from the brief verbatim at the end of the file, after the existing eleven. Nothing
  existing was moved or duplicated.

No `CMakeLists.txt` change — the header is not wired into any translation unit yet (that's later-task work);
it only needed to exist for the test file to include it.

## TDD sequence (as required)

1. Added the `#include` and the seven test cases first, header not yet created.
2. Ran `cmake -S tests/host -B build-host && cmake --build build-host --parallel` — confirmed it FAILED:
   ```
   tests/host/test_now_playing.cpp:4:10: fatal error: 'now_playing_controller.hpp' file not found
   ```
3. Wrote `now_playing_controller.hpp` verbatim per the brief.
4. Rebuilt and ran.

## Exact host-test output

Build: `[100%] Built target host_tests` (no warnings/errors).

Run (`./build-host/host_tests`), tail plus the seven new cases isolated:
```
PASS seize_takes_the_screen_when_a_session_opens
PASS seize_releases_after_the_hold_and_stays_released
PASS seize_restarts_on_a_new_title_but_not_on_a_republished_one
PASS seize_lets_go_the_moment_the_session_closes
PASS volume_overlay_opens_on_a_change_and_closes_on_a_timer
PASS volume_overlay_stays_shut_when_the_page_is_not_on_screen
PASS volume_overlay_ignores_a_source_with_no_level_to_report
...
282 cases, 0 failures
```
Exit code 0. 275 -> 282 as expected (7 new cases).

## Exact firmware-build result

`./scripts/idf.sh build` ended with:
```
layout_carousel.bin binary size 0x187270 bytes. Smallest app partition is 0x300000 bytes. 0x178d90 bytes (49%) free.

Project build complete. To flash, run:
 idf.py flash
...
```
No warnings/errors. The new header is not yet `#include`d from any firmware source, so this build did not
newly compile it under xtensa — it only confirms the header's mere presence doesn't disturb anything, and
that no other regression crept in. The two historical failure modes named in the constraints (a `switch`
over `PageId` firmware-only, and a `%u`/`uint32_t` format mismatch) don't apply here: this change adds no
`switch` over an enum and no `printf`-family call.

## Self-review

- Grepped the new header for `modules/` — no hits; only `<cstdint>` and `<string>` are included. No
  protocol-specific knowledge (wire formats, dB values, protocol event names) is encoded in the struct
  fields or constants — they're generic (`owns_screen`, `title`, `visible`, `last_volume`).
- Confirmed `git diff` of the new file matches the brief's code byte-for-byte, including the long "why"
  comments (kept verbatim per house style).
- Confirmed the test-file diff only appends; the two existing anonymous-namespace helper blocks were left
  untouched and not duplicated.
- Confirmed C++17 compatibility: `SeizeState{true, now_ms, title}` is aggregate-initializing a struct with
  default member initializers, which is valid aggregate init since C++14/17 (and it compiled cleanly on
  host).
- Verified the commit contains only the two intended files (`now_playing_controller.hpp` and
  `test_now_playing.cpp`); an unrelated pre-existing modification to `task-3-report.md` in the working tree
  was left out of the commit since it isn't part of this task.
- No `switch` over an enum and no `printf`-family call were introduced, so the two named historical failure
  modes (`-Werror=switch`, `-Werror=format`) don't apply to this change.

## Open items the brief didn't specify

- Nothing required a judgment call: the brief supplied the header, the tests, and the commit message
  verbatim, and all of it applied cleanly with no adaptation needed.

## Fix pass

Code review of the initial Task 4 landing (`11f15e8`) found five defects, all present in the
reference code the file was copied from. Followed TDD for the three behavioural ones: added a
failing `HOST_TEST`, ran the suite and watched it fail, then fixed the logic and watched it pass.

### 1. CRITICAL — `seize_tick()` never seized on an empty-title open

`SeizeState{}` defaults `title` to `""`, and a session that opens before any metadata arrives
(`session_open = true`, title still `""`) compared `"" != ""` as false, so the page never took the
screen. The header's own comment ("at the start state.title is empty and any real title differs
from it") was asserting something false in exactly that case.

Fix: added `bool was_open = false;` to `SeizeState`. The seize condition is now
`!state.was_open || title != state.title` — the first tick of an open session always seizes,
whatever the title is; a later tick still seizes only on a genuine title change. `was_open` is
carried correctly through the release path (unchanged struct) and reset by the session-close path,
which already returns `SeizeState{}`. Rewrote the now-inaccurate comment.

New test: `seize_takes_the_screen_when_a_session_opens_with_no_title_yet` — opens a session with
`title = ""` and asserts `owns_screen`. Failed against the old code (`expected true:
state.owns_screen`), passed after the fix.

### 2. IMPORTANT — no backward-clock guard in either machine

Both `now_ms - state.seized_ms >= ...` and `now_ms - state.shown_ms >= ...` were unguarded unsigned
subtractions. A `now_ms` less than the stored timestamp underflows to near `UINT64_MAX`, which
satisfies `>=` any threshold, so the hold released or the overlay closed immediately and wrongly.

Followed the house idiom from `carousel_controller.cpp:37-38`, adapted to this file's polarity (the
carousel's guard *skips* an action when the clock hasn't caught up; here the action is a release/
close, so the guard is the direct condition: only release/close when the clock is *not* behind the
stored timestamp *and* the interval has actually elapsed):

```c++
if (state.owns_screen && now_ms >= state.seized_ms &&
    now_ms - state.seized_ms >= kNowPlayingSeizeSeconds * 1000) {
  state.owns_screen = false;
}
```
and the equivalent for `state.shown_ms` / `kVolumeOverlayMs` in `volume_overlay_tick()`.

(First attempt mirrored the carousel's exact `||` shape — `now_ms < seized_ms || now_ms -
seized_ms >= threshold` — as the release condition, which is wrong: that reads a backward clock as
"release immediately," the opposite of the intended guard. Caught by the new backward-clock tests
before commit; corrected to the `&&` form above.)

New tests: `seize_ignores_a_clock_that_moves_backward` and
`volume_overlay_ignores_a_clock_that_moves_backward` — seize/open at a later timestamp, then feed an
earlier `now_ms`, and assert the hold/overlay is still active. Both failed against the unguarded
code, passed after the fix.

### 3. IMPORTANT — volume overlay timeout unreachable while volume is unknown

`if (volume < 0.0f) return state;` sat before the timeout check, so once the overlay was open, a
run of negative readings kept it open past `kVolumeOverlayMs` until a valid reading resumed.

Fix: restructured so the timeout check always runs. A negative reading now skips only the
open/baseline logic (`if (volume >= 0.0f) { ... }`) and falls through to the close check
unconditionally, so it can no longer open the overlay or move the baseline, but also can no longer
block the close.

New test: `volume_overlay_closes_on_timeout_even_while_volume_is_unknown` — opens the overlay, then
feeds a negative reading at exactly the timeout instant, asserts `!visible`. Failed against the old
code (stuck visible), passed after the fix.

### 4. MINOR — 88-column line

`if (state.owns_screen && now_ms - state.seized_ms >= kNowPlayingSeizeSeconds * 1000) {` wrapped
naturally as part of fixing #2 (the added backward-clock guard forced a multi-clause condition,
wrapped the same way `carousel_controller.cpp:37-38` wraps its own). Confirmed no line in the file
exceeds 80 columns with `awk '{ if (length($0) > 80) print }'`.

### 5. MINOR — undocumented exact-float-equality assumption

Added a comment directly above `if (volume != state.last_volume)` recording that exact equality is
safe only because `volume` is today a direct passthrough from the publishing module with no local
arithmetic, and that a future source deriving it (averaging, scaling) would need an epsilon compare.

### Files changed

- `components/app_core/include/now_playing_controller.hpp` — all five fixes.
- `tests/host/test_now_playing.cpp` — four new `HOST_TEST` cases reproducing findings 1–3 (two for
  finding 2, one per machine).
- `docs/design/plans/2026-08-19-now-playing-page.md` — Task 4's Step 1 test block and Step 3 header
  code block replaced with the shipped versions (all four new tests, all five fixes), and Step 4's
  "seven new cases" corrected to "eleven new cases".

### Host-test output (exact)

```
$ cmake --build build-host --parallel && ./build-host/host_tests
[100%] Built target host_tests
...
286 cases, 0 failures
```

Baseline was 282 cases; 4 new cases added (282 + 4 = 286). Before the fix, `./build-host/host_tests`
reported:

```
FAIL seize_takes_the_screen_when_a_session_opens_with_no_title_yet: expected true: state.owns_screen
FAIL seize_ignores_a_clock_that_moves_backward: expected true: state.owns_screen
FAIL volume_overlay_ignores_a_clock_that_moves_backward: expected true: state.visible
FAIL volume_overlay_closes_on_timeout_even_while_volume_is_unknown: expected true: !state.visible
286 cases, 4 failures
```

After the header fix, the backward-clock tests still failed once (first-attempt guard shape, see
finding 2 above); after correcting the guard to the `&&` form, all 286 cases passed with exit 0.

### Firmware-build output (exact, tail)

```
$ ./scripts/idf.sh build
...
[2/5] Checking CJK glyph coverage against ui_strings.cpp
cjk font coverage: 121 characters, all present in 3 sizes
...
layout_carousel.bin binary size 0x187270 bytes. Smallest app partition is 0x300000 bytes. 0x178d90 bytes (49%) free.

Project build complete. To flash, run:
 idf.py flash
```

`now_playing_controller.hpp` is not yet included by any firmware source file — Task 6 (not part of
this fix pass) wires it into `ui_app.cpp`. The firmware build therefore does not itself compile the
changed header; it is exercised only by the host tests. Confirmed via `grep -rl
"now_playing_controller.hpp" components main` returning no hits, so this pass carries no risk of a
compiled-firmware regression from these edits, consistent with the plan's own Task 4/Task 6 split.
