# Task 5 report: Render the page

## What was built

Followed the brief exactly (all code blocks are verbatim, per the brief's
own instruction not to improvise, and each was cross-checked against the
real interfaces before writing):

1. **`components/ui/render_now_playing.cpp`** (new). `render_now_playing()`
   reads `app_core::now_playing()` directly, draws the volume overlay (via
   `render_volume_overlay`) when `context->volume_overlay_visible` is set
   and returns early, otherwise draws the artwork/text/transport layout from
   `ui::now_playing_layout(has_artwork)`. `repack_artwork()` repacks the
   registry's tight-packed I1 bitmap into LVGL's padded-stride canvas
   format using `i1_canvas_storage_bytes()`, `i1_canvas_stride()`, and
   `i1_canvas_pixel_offset()` (never a hand-rolled `(width+7)/8`), backed
   by a persistent module-level buffer sized generously
   (`((176/8)+8)*176+64` = 5344 bytes) because LVGL keeps the canvas
   pointer rather than copying.
2. **`components/ui/include/ui_app.hpp`**: declared `render_now_playing`
   after `render_settings`, and added `UiContext::volume_overlay_visible`
   next to `settings_focus`.
3. **`components/ui/include/ui_fonts.hpp`**: widened the `font_hero()`
   comment to name the volume readout as its second legitimate caller,
   leaving the rest of the comment (the OTA/sensor-page failure history)
   untouched.
4. **`components/ui/render_shared.cpp`**: added the
   `case app_core::PageId::NowPlaying:` arm beside `Settings` in
   `render_page`'s switch, calling `render_now_playing`. The `default:`
   fallthrough to `render_home` is left in place, as instructed — not this
   task's decision to remove.
5. **`components/ui/CMakeLists.txt`**: added `"render_now_playing.cpp"` to
   `SRCS`, after `"render_settings.cpp"`.

## Verification

Host tests:
```
cmake --build build-host --parallel && ./build-host/host_tests
...
286 cases, 0 failures
```
Exit 0. (This file has no host coverage — `tests/host` does not compile
LVGL — so this run only confirms nothing else regressed.)

Firmware build:
```
./scripts/idf.sh build
...
[10/28] Building CXX object esp-idf/ui/CMakeFiles/__idf_ui.dir/render_now_playing.cpp.obj
...
Project build complete. To flash, run: ...
```
`render_now_playing.cpp` compiled with no warnings (the one warning in the
full build log is pre-existing and unrelated, in
`components/wifi_provision/portal.cpp`/`dns_server.h`).

## Self-review

Before writing, read `render_weather.cpp` and `render_ota.cpp` for house
pattern (parameter list, `(void)` casts, `apply_surface(parent)` first,
`label()` usage), and read the `i1_canvas_*` comment block plus
`tray_indicator_icon()`/`repack_i1_bits()` in `ui_theme.hpp`/`.cpp` before
accepting the brief's `repack_artwork()`.

Checked against real interfaces rather than trusting the brief blindly:
- `NowPlayingLayout`, `VolumeOverlayLayout`, `now_playing_progress_fill_width`,
  `volume_overlay_fill_width`, `format_track_time`, `volume_percent_text`,
  `media_state_label`, `kNowPlayingArtworkSize` in `ui_data.hpp` — field
  names, types, and the exact `Rect` layouts the brief's code indexes all
  matched what Task 3 actually produced.
- `bind_i1_canvas`, `i1_canvas_stride`, `i1_canvas_storage_bytes`,
  `i1_canvas_pixel_offset` signatures in `ui_theme.hpp` matched the call the
  brief writes, argument for argument (including the `background`/
  `background_opa`/`ink` ordering).
- `app_core::MediaArtwork::width`/`height` are `uint16_t`; passed into
  `int`-typed parameters, which is a safe widening conversion (confirmed by
  the clean firmware build, since `-Werror=all` would flag anything
  narrowing).
- `app_core::PageId::NowPlaying` already exists in `app_snapshot.hpp` (Task
  2), so the new `switch` case is exhaustive — no `-Werror=switch` risk.
- `media_registry.hpp` lives under `components/app_core/include/`, not
  `modules/`, so including it from `components/ui/render_now_playing.cpp`
  does not violate the module-contract rule (`modules/README.md` rule 4);
  nothing in the new file encodes protocol-specific knowledge.
- Confirmed `Rect{x, y, width, height}` field order for the two inline
  brace-initialized rects in the volume overlay's `%` label.

No defects found; nothing in the brief's code needed correction. The diff
matches the brief's Steps 1–5 verbatim.

## Left open / decisions

- The brief's `render_now_playing` declaration omits the
  `UiContext* context = nullptr` default every other renderer in
  `ui_app.hpp` has. Brief's own "Interfaces" line specifies the signature
  without a default, and the only call site (`render_shared.cpp`) always
  passes `&context`, so this was kept exactly as given rather than
  "fixed" to match the sibling declarations — it's a one-line
  inconsistency the brief author evidently intended (or overlooked)
  without functional effect, and not something this task's brief asked to
  reconcile.
- Left `render_shared.cpp`'s `default:` fallthrough to `render_home` in
  place, as the brief explicitly says the removal is a separate decision
  for a later review.

## What could NOT be verified without hardware

This is the important list, since there is no host coverage for this file:

- **Pixel-accurate layout on the panel**: whether the artwork canvas, text
  columns, transport bar, and volume overlay actually land where the
  geometry literals in `ui_data.hpp` say they should on the real 400x300
  1-bit LCD. Task 3's tests cover the arithmetic (fill widths, layout
  rects as values) but not what LVGL actually paints from them.
- **The I1 canvas repack round-trip on target**: whether
  `repack_artwork()` correctly reproduces a real artwork bitmap through
  LVGL's actual `lv_draw_buf_width_to_stride()` on this SoC/LVGL build —
  the two traps documented in `ui_theme.hpp` (palette-in-buffer, padded
  stride) were each only caught by looking at the physical panel before.
- **`font_hero()` rendering "0"–"100" and the `%` label's position**
  relative to the digits at runtime — verified only that the font/label
  calls compile and reference real API, not that the glyphs and the
  separately-positioned `%` box actually align visually.
- **The volume overlay / now-playing page reachability end-to-end**: this
  page cannot be reached yet (Task 6 wires the tick that flips
  `volume_overlay_visible` and drives the carousel into `NowPlaying`, Task
  7 publishes real data), so nothing in this renderer has been exercised
  by an actual media session, muted or otherwise.
- **The "MUTE" vs. digit-only font switch under a real muted session** —
  logic was read and matches `volume_percent_text()`'s contract, but only
  a real `NowPlaying{muted: true}` on the panel proves the large-font
  fallback actually avoids the empty-box failure mode the comment warns
  about.
- **Memory/perf**: whether the 5344-byte static `g_artwork_storage` buffer
  fits comfortably in the firmware's actual RAM budget at runtime (the
  build only confirms it links; DRAM headroom is not something the build
  log directly reports for a single static array).

Per the brief, Step 6 ("check it on the panel") and full pixel
verification are deferred until the end of Task 7, the first point real
data reaches this page.

## Fix pass

Four review findings fixed against `components/ui/render_now_playing.cpp` (commit `f0682d0`), plus the matching reference code in `docs/design/plans/2026-08-19-now-playing-page.md` Task 5.

### 1. IMPORTANT — `repack_artwork()` reimplemented `repack_i1_bits()`

Confirmed `ui::repack_i1_bits()` (`components/ui/include/ui_theme.hpp`) is genuinely the shared building block its own comment says it is: every dimension is a caller-supplied parameter, it owns no storage, and it already performs the same row-by-row copy from a tight `(width+7)/8`-packed source into an LVGL-padded-stride destination, plus its own bounds checks. The old comment on `repack_artwork()` claiming it was "not shared with it because that one is sized for tray-scale bitmaps and owns its own per-slot storage" was false — `repack_i1_bits()` has no size assumption and owns no storage at all.

Fix: `repack_artwork()` now calls `repack_i1_bits(artwork.bits, g_artwork_storage, sizeof(g_artwork_storage), artwork.width, artwork.height, stride, i1_canvas_pixel_offset())` and the hand-rolled `memcpy` loop is deleted. Because `repack_i1_bits()` returns `void` and silently no-ops when its arguments don't fit, `repack_artwork()` still computes `needed = i1_canvas_storage_bytes(...)` itself and checks it against `sizeof(g_artwork_storage)` *before* calling, so the `bool` it returns is still an honest "did this actually get repacked" answer, not a guess. Comment rewritten to state plainly why the call is safe to share (no storage, all dimensions parameterized) rather than the false claim it replaced.

### 2. IMPORTANT — artwork not clamped to the reserved 176x176 slot

Added `ui::now_playing_artwork_fits_slot(int width, int height)` as a pure `constexpr` function in `components/ui/include/ui_data.hpp` (next to `kNowPlayingArtworkSize`/`kNowPlayingTextColumnWidth`), accepting `0 < width <= 176` and `0 < height <= 176` — up to and including exactly 176x176, not only exactly that size, since a smaller image still sits inside its slot. `repack_artwork()` now calls it as the first gate (alongside the `bits != nullptr` check) and returns `false` on a miss, which makes `render_now_playing()` fall through to the existing no-artwork layout (`now_playing_layout(false)`) — a first-class layout, not a degraded one. This closes the gap where a publisher reporting artwork wider/taller than the reserved slot would draw straight over the text column at `kNowPlayingTextColumnX` (194), which `assert_tree_in_safe_canvas()` cannot catch since it only proves objects stay inside the whole 400x300 canvas, not that siblings don't overlap.

Placed in `ui_data.hpp` (not inline in the renderer) specifically because that header is compiled LVGL-free for host tests, so this rule — unlike the renderer itself — is testable.

### 3. MINOR — missing `UiContext* context = nullptr` default

`components/ui/include/ui_app.hpp`'s `render_now_playing()` declaration now reads `..., UiContext* context = nullptr);`, matching every sibling renderer declaration in the same file. Harmless previously (the one call site in `render_shared.cpp` always passes `&context`), purely a consistency fix.

### 4. MINOR — inconsistent emptiness guarding

`if (!media.source.empty())` around the `source` label was removed; `source` is now drawn unconditionally like `title`/`subtitle`/`detail`, with a comment explaining why unconditional is the right call: an empty string paints nothing on this panel (no glyphs drawn, not a visible blank rectangle), so the guard only ever skipped one `label()` call and never changed what appeared on screen — four fields that behave identically now read as four fields that behave identically. (`render_volume_overlay()`'s own, separate `source` guard was left untouched — the finding named the four fields in the main body specifically.)

### New tests

Added five `HOST_TEST` cases to `tests/host/test_now_playing.cpp`, immediately before `progress_fill_width_covers_its_whole_range`:
- `artwork_fits_slot_accepts_exactly_the_reserved_size` — 176x176 accepted
- `artwork_fits_slot_accepts_smaller_than_the_reserved_size` — 1x1 and 175x175 accepted
- `artwork_fits_slot_rejects_wider_than_the_reserved_size` — 177x176 rejected
- `artwork_fits_slot_rejects_taller_than_the_reserved_size` — 176x177 rejected
- `artwork_fits_slot_rejects_zero_dimensions` — (0,176), (176,0), (0,0) all rejected

### Verification

`cmake --build build-host --parallel && ./build-host/host_tests`:
```
...
PASS drain_ms_for_rate_is_shorter_at_the_streaming_rate
PASS drain_ms_for_rate_rounds_up_rather_than_truncating
291 cases, 0 failures
```
Exit code 0. 291 = 286 baseline + 5 new artwork-fit cases.

`./scripts/idf.sh build`:
```
...
[13/26] Building CXX object esp-idf/ui/CMakeFiles/__idf_ui.dir/render_now_playing.cpp.obj
...
layout_carousel.bin binary size 0x187a90 bytes. Smallest app partition is 0x300000 bytes. 0x178570 bytes (49%) free.

Project build complete. To flash, run:
...
```
(One pre-existing, unrelated warning in `components/wifi_provision/portal.cpp`/`dns_server.h` about a missing designated-initializer field — untouched by this change, present before it too.)

### Files touched
- `components/ui/render_now_playing.cpp`
- `components/ui/include/ui_app.hpp`
- `components/ui/include/ui_data.hpp`
- `tests/host/test_now_playing.cpp`
- `docs/design/plans/2026-08-19-now-playing-page.md`
