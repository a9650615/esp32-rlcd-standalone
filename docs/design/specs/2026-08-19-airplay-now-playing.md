# AirPlay now-playing page

Status: proposal. Nothing here is implemented.

A page that shows what is playing while an AirPlay session is open, and a
full-content-area overlay that shows the volume while it is being changed.
Mockups of all three screens (artwork, no artwork, volume) are at
<https://claude.ai/code/artifact/42a83c8a-1f87-4485-bd14-f4de8917883e>.

## What the RAOP library already gives us

`modules/airplay/esp-raop-receiver` already emits everything this page needs.
Nothing in the vendored library has to change.

| Field on screen | Source | Available today |
| --- | --- | --- |
| Title / artist / album | `RAOP_EVENT_METADATA` → `raop_metadata_t` | yes |
| Elapsed / total time | `RAOP_EVENT_PROGRESS` → `raop_progress_t` | yes |
| Transport state | `RAOP_EVENT_{PLAYING,PAUSED,BUFFERING,STALLED,STOPPED}` | yes |
| Album artwork | `RAOP_EVENT_ARTWORK` → `raop_artwork_t` (JPEG bytes) | yes, needs a decoder |
| Volume | `raop_get_volume()` | yes |
| Source device name | — | **no API for it** |

Two of those need saying out loud.

**Volume does not need HARDWARE mode.** `airplay.cpp` sets
`RAOP_VOLUME_SOFTWARE`, and `RAOP_EVENT_VOLUME` only fires in HARDWARE mode.
That looks like a blocker and is not one: `raop_get_volume()` returns the
current level in both modes. The UI tick already runs every ~100 ms, so it
polls that value and compares it against the last one it saw. A change opens
the overlay. This keeps the PCM path, the volume mode, and the vendored
library exactly as they are.

`ui::VolumePreset` in `settings_menu.hpp` is **not** involved. That preset
scales locally generated sound only, and its own header says so: the phone
owns AirPlay volume and multiplying by a second local scale would be wrong.

**The device name has no source.** "Birdyo's iPhone" in the mockup is
aspirational. If the library cannot be made to surface it without a fork, the
whole line is simply not drawn. Every other rect keeps its coordinates — the
row becomes empty space rather than shifting the block up, so the two cases
cannot drift into two different layouts. Do not invent a name from the RTSP
session.

## Geometry

Everything below is in the existing coordinate system. No new constants beyond
the page's own layout numbers.

| | |
| --- | --- |
| Canvas | 400 × 300 |
| Safe canvas | `{6, 6, 388, 288}` |
| Tray reserved | 36 (28 band + 1 separator + 7 gap) |
| Content area | `{6, 42, 388, 240}` |
| Page dots band | y 289, height 5 |

The page carries the tray (`page_shows_tray`) and the dots
(`page_shows_dots`) like every carousel page. Neither predicate needs
changing — both are exclusion lists that a new `PageId` falls outside of by
default.

### Layout A — artwork present

| Element | Rect | Font | Notes |
| --- | --- | --- | --- |
| Artwork | `{6, 46, 176, 176}` | — | 1-bit, see below |
| Source | `{194, 46, 200, 16}` | 14 | dropped if unavailable |
| Title | `{194, 66, 200, 68}` | 28 | 2 lines, then ellipsis |
| Artist | `{194, 140, 200, 22}` | 20 | 1 line, then ellipsis |
| Album | `{194, 166, 200, 16}` | 14 | 1 line, then ellipsis |
| Transport state | `{6, 244, 120, 16}` | 14 | left of the bar |
| Time | right-aligned at x 394, y 244 | 14 | `1:42 / 3:58` |
| Progress outline | `{6, 266, 388, 10}` | — | 1px |
| Progress fill | `{8, 268, w, 6}` | — | `w = 384 × elapsed/total` |

176 rather than a larger square is not a memory decision: the right column
needs 200px to fit two lines of 28px type, and 176 + 12 + 200 = 388 exactly.

### Layout B — no artwork

Same transport row, byte for byte, so switching between A and B never moves
the bottom of the screen. The text block goes full width and centres:

| Element | Rect | Font |
| --- | --- | --- |
| Source | `{6, 60, 388, 16}` | 14 |
| Title | `{6, 92, 388, 68}` | 28 |
| Artist | `{6, 168, 388, 22}` | 20 |
| Album | `{6, 196, 388, 16}` | 14 |

### Layout C — volume overlay

Covers the content area only. The tray stays, so the clock and battery do not
blink out and the repaint area stays at 388 × 240.

| Element | Rect | Font |
| --- | --- | --- |
| `VOLUME` label | `{6, 50, 120, 16}` | 14 |
| Source | right-aligned at x 394, y 50 | 14 |
| Percentage | `{6, 74, 388, 130}` centred | 128 digits + 28 for `%` |
| Bar outline | `{6, 232, 388, 36}` | — |
| Bar fill | `{9, 235, w, 30}` | — |

The 128px face is `rlcd_digits_128.c`, which is digits only — the `%` renders
in the 28px face beside it.

Two values are not percentages and must not be drawn as one:

- **Mute.** AirPlay signals mute as `-144 dB`, which is a distinct state, not
  0%. Show `MUTE` with an empty bar.
- **No level yet.** Before the first `SET_PARAMETER` the receiver has no
  volume to report. The overlay simply does not open.

## Transport states

The state line is the only text that changes with playback. Each value maps to
exactly one event — nothing is inferred from timing.

| Screen | Event | Progress bar |
| --- | --- | --- |
| `▶ PLAY` | `RAOP_EVENT_PLAYING` | tracks progress |
| `❙❙ PAUSE` | `RAOP_EVENT_PAUSED` | frozen |
| `⋯ BUFFER` | `RAOP_EVENT_BUFFERING` | empty |
| `⚠ STALL` | `RAOP_EVENT_STALLED` | frozen |
| `■ STOP` | `RAOP_EVENT_STOPPED` | zeroed |

A paused session keeps the page. Pausing is not leaving.

## Lifecycle: seize, then release

`RAOP_EVENT_CONNECTED` takes the screen immediately, the way OTA and Setup do,
and then hands control back:

1. **Connect.** The page replaces whatever is showing. The carousel does not
   get a vote.
2. **Hold for 60 s.** Rotation is suspended so there is time to read what is
   playing. **60 is a starting value to tune on the panel, not a derived
   number.** It lives next to the other dwell constants, not buried in a
   render function.
3. **Release.** Rotation resumes and AirPlay joins it as one more page. Its
   `PageDescriptor::dwell_seconds` starts at whatever the other data pages
   use — this is a page in the rotation, not a privileged one, and the 60 s
   above is the entire extent of its privilege.
4. **New track re-seizes.** A `RAOP_EVENT_METADATA` carrying a different title
   restarts step 1. Metadata that repeats the current track does not.
5. **Disconnect.** `RAOP_EVENT_DISCONNECTED` drops the page out of rotation
   and the carousel returns to its normal order.
6. **The overlay is independent.** Volume changes open the overlay only while
   the AirPlay page is the one on screen, for
   `kNavigationOverlayDurationMs` (2 s, already defined). While the carousel
   is on another page a volume change does nothing visible.

Nothing here uses `next_relevant_auto_index` to force the page. That function
steers away from pages with nothing to show; it is not a mechanism for one
page to demand attention, and overloading it would make the two behaviours
impossible to reason about separately.

## Artwork pipeline

Four steps, each with a known output size:

1. `RAOP_EVENT_ARTWORK` hands over a JPEG buffer. **Its size is unmeasured.**
2. Decode to 8-bit greyscale at 176 × 176 — 30.3 KB, PSRAM.
3. Dither to 1-bit with the existing `ui::dither_bayer4x4_dark(x, y, level)`.
   It is already per-pixel and already takes a 0–16 level, so nothing new is
   written: convert luminance to that scale and call it. 176 is far above
   `kMinDitherDimensionPx` (16), so the pattern is in its measured range.
4. Hand the packed I1 buffer to LVGL — 176 × 22 bytes/row = 3.9 KB, the same
   tightly-packed layout the tray indicator bitmaps already use.

Exactly one decoded artwork is held at a time. A new one replaces it; there is
no cache. Any failure — decoder error, allocation failure, no artwork sent —
falls through to Layout B, which is why B is a real layout and not a
degraded A.

The decoder is not chosen here. Whatever is used must decode from a buffer to
a scaled greyscale output without materialising a full-size RGB frame; that
constraint, not a library name, is what the implementation has to satisfy.

## Integration points

- `app_core::PageId` gains `AirPlay`.
- `AppSnapshot` gains an `AirPlayData` (title, artist, album, elapsed_ms,
  total_ms, state, volume, artwork handle, valid). Value-only, like every
  other snapshot struct, so host tests can exercise the layout with no ESP-IDF.
- `modules/airplay` publishes into it from its existing event handler. It
  already owns a tray indicator; the page is the same pattern, one level up.
- `components/ui/render_airplay.cpp` renders it, reached through the existing
  `switch` in `render_shared.cpp`.
- `page_registry` adds a descriptor whose `is_available` is "a session is
  open", which keeps the page out of rotation the rest of the time without
  any special-casing.

## Verification

Host tests, no board required, in the existing geometry-test style:

- Every rect in all three layouts sits inside `content_bounds(…, AirPlay)`.
- Layouts A and B produce byte-identical transport rows.
- Progress fill width is 0 at 0 ms, 384 at `elapsed == total`, and never
  exceeds 384 when `elapsed > total` (a stream that overruns its declared
  length must not draw past the outline).
- `total_ms == 0` (live stream) draws elapsed time and no bar.
- Volume 0.0 → `0%`, 1.0 → `100%`, mute → `MUTE`.

## What this deliberately does not do

- **No playback control.** The buttons stay on carousel duty. AirPlay is a
  receiver; the phone is the remote.
- **No marquee scrolling for long titles.** The panel updates too slowly to
  scroll text legibly. Two lines and an ellipsis.
- **No artwork cache.** One image, replaced in place.
- **No per-track history or queue view.** RAOP does not reliably provide it.
