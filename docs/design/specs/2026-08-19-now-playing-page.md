# Now Playing page

Status: proposal. Nothing here is implemented.

A page that shows what is playing while a media session is open, and a
full-content-area overlay that shows the volume while it is being changed.
AirPlay is the first source to feed it, and the whole design is driven by what
RAOP actually provides — but the page, the layouts, and the core-side API
below name no protocol.

Mockups of all three screens (artwork, no artwork, volume) are at
<https://claude.ai/code/artifact/42a83c8a-1f87-4485-bd14-f4de8917883e>.

## Why the page is not called AirPlay

`modules/README.md` rule 4: dependencies point one way, module → core. No core
component may include a module header, and a module that wants something on
the panel gets a registration API from core rather than a place in core's own
types. `tray_registry.hpp` records what happens when that is ignored — an
`app_core::TrayActivity` enum with a `Speaker` value existed, and was removed
for exactly this reason.

So there is no `PageId::AirPlay` and no `AppSnapshot::airplay`. Core gains a
media-session registry that knows about *a source that is playing something*,
in the same shape the tray registry already uses for icons. `modules/airplay`
registers itself with it and publishes into it. Core never learns the word
RAOP, and a second source later — Bluetooth A2DP, a local player — reuses the
page instead of adding a second one.

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

The state line is the only text that changes with playback. The page draws
`MediaState`; the module maps its own events onto it. Nothing is inferred from
timing.

| Screen | `MediaState` | AirPlay's mapping | Progress bar |
| --- | --- | --- | --- |
| `▶ PLAY` | `Playing` | `RAOP_EVENT_PLAYING` | tracks progress |
| `❙❙ PAUSE` | `Paused` | `RAOP_EVENT_PAUSED` | frozen |
| `⋯ BUFFER` | `Buffering` | `RAOP_EVENT_BUFFERING` | empty |
| `⚠ STALL` | `Stalled` | `RAOP_EVENT_STALLED` | frozen |
| `■ STOP` | `Stopped` | `RAOP_EVENT_STOPPED` | zeroed |

`MediaState::Idle` never reaches the page: it only occurs alongside
`session_open == false`, and then the page is not in rotation at all.

A paused session keeps the page. Pausing is not leaving.

## Lifecycle: seize, then release

A session opening takes the screen immediately, the way OTA and Setup do, and
then hands control back. The trigger is core-side — `session_open` going true
— so the rule reads the same for any future source. AirPlay drives it from
`RAOP_EVENT_CONNECTED`.

1. **Session opens.** The page replaces whatever is showing. The carousel does
   not get a vote.
2. **Hold for 60 s.** Rotation is suspended so there is time to read what is
   playing. **60 is a starting value to tune on the panel, not a derived
   number.** It lives next to the other dwell constants, not buried in a
   render function.
3. **Release.** Rotation resumes and Now Playing joins it as one more page.
   Its `PageDescriptor::dwell_seconds` starts at whatever the other data pages
   use — this is a page in the rotation, not a privileged one, and the 60 s
   above is the entire extent of its privilege.
4. **New track re-seizes.** A published `title` different from the current one
   restarts step 1. A republish of the same title does not.
5. **Session closes.** `session_open` going false drops the page out of
   rotation and the carousel returns to its normal order. AirPlay drives this
   from `RAOP_EVENT_DISCONNECTED`.
6. **The overlay is independent.** Volume changes open the overlay only while
   Now Playing is the page on screen, for `kNavigationOverlayDurationMs`
   (2 s, already defined). While the carousel is on another page a volume
   change does nothing visible.

Nothing here uses `next_relevant_auto_index` to force the page. That function
steers away from pages with nothing to show; it is not a mechanism for one
page to demand attention, and overloading it would make the two behaviours
impossible to reason about separately.

## The core-side API

New file `components/app_core/include/media_registry.hpp`, deliberately
modelled on `tray_registry.hpp` — same ownership, same "core reserves a slot,
the module fills it" shape, same value-only types so host tests need no
ESP-IDF.

```c++
namespace app_core {

enum class MediaState { Idle, Buffering, Playing, Paused, Stalled, Stopped };

// A 1-bit, tightly-packed, row-major MSB-first bitmap - byte for byte the
// same layout TrayIndicatorBitmap already documents, so nothing new has to
// be explained about it. Owned by the publisher, which must keep it alive
// until it publishes a different one or clears the session.
struct MediaArtwork {
  const uint8_t* bits = nullptr;
  uint16_t width = 0;
  uint16_t height = 0;
};

struct NowPlaying {
  bool session_open = false;
  MediaState state = MediaState::Idle;
  // Whatever the source calls itself: a device name if it knows one, its own
  // protocol name if not. Core does not supply a default and does not care
  // which it is.
  std::string source;
  std::string title;
  std::string subtitle;   // artist
  std::string detail;     // album
  uint32_t elapsed_ms = 0;
  uint32_t total_ms = 0;  // 0 = unknown length (live stream)
  // -1 = the source has no volume to report. 0.0-1.0 otherwise. Mute is a
  // real 0.0, not a missing value: see the overlay section.
  float volume = -1.0f;
  bool muted = false;
  MediaArtwork artwork;
};

MediaSourceHandle register_media_source();
void publish_now_playing(MediaSourceHandle handle, const NowPlaying& state);
void clear_media_session(MediaSourceHandle handle);
// What the UI reads. Returns the one open session, or a default-constructed
// NowPlaying (session_open == false) when nothing is playing.
NowPlaying now_playing();

}  // namespace app_core
```

One slot, not an array. Two simultaneous sources is not a real situation on
this board — a second registration returns an invalid handle and logs it,
which is the same thing a full tray registry does today.

This is not part of `AppSnapshot`. The snapshot is republished wholesale by
several tasks on their own cadences, and the field-overwrite bug documented on
`AppSnapshot::battery_runtime` is what that costs. A registry the publisher
writes directly avoids the whole class of problem, exactly as the tray
registry does.

## Artwork pipeline

**The module does all of it.** Core is handed finished 1-bit pixels and never
learns what JPEG is. Four steps, each with a known output size:

1. `RAOP_EVENT_ARTWORK` hands over a JPEG buffer. **Its size is unmeasured.**
2. Decode to 8-bit greyscale at 176 × 176 — 30.3 KB, PSRAM.
3. Dither to 1-bit with the existing `ui::dither_bayer4x4_dark(x, y, level)`.
   It is already per-pixel and already takes a 0–16 level, so nothing new is
   written: convert luminance to that scale and call it. 176 is far above
   `kMinDitherDimensionPx` (16), so the pattern is in its measured range.
   Including `ui/dither.hpp` from the module is the allowed direction
   (module → core); it is a header of pure inline functions with no LVGL in
   it.
4. Publish the packed buffer as `MediaArtwork` — 176 × 22 bytes/row = 3.9 KB,
   the same tightly-packed layout the tray indicator bitmaps already use.

Exactly one decoded artwork is held at a time, owned by the module. A new one
replaces it; there is no cache. Any failure — decoder error, allocation
failure, no artwork sent — leaves `artwork.bits` null, and the page falls
through to Layout B. That is why B is a real layout and not a degraded A.

The decoder is not chosen here. Whatever is used must decode from a buffer to
a scaled greyscale output without materialising a full-size RGB frame; that
constraint, not a library name, is what the implementation has to satisfy.

## Integration points

- `app_core::PageId` gains `NowPlaying`. A page identity, not a protocol.
- `components/app_core/media_registry.{hpp,cpp}` is the new API above.
- `page_registry` adds a descriptor whose `is_available` reads
  `now_playing().session_open`. It ignores its `AppSnapshot` parameter, which
  the existing `PageDescriptor` signature already allows — no signature
  change.
- `components/ui/render_now_playing.cpp` renders it, reached through the
  existing `switch` in `render_shared.cpp`.
- `modules/airplay/airplay.cpp` registers a media source next to its existing
  tray-indicator registration, and publishes from the event handler it already
  has. This becomes a fourth entry in that module's README touch-point list,
  which rule 5 requires.
- `main/app_main.cpp` and `components/wifi_provision` are untouched. Nothing
  new goes through the snapshot publish path.

## Verification

Host tests, no board required, in the existing geometry-test style:

- Every rect in all three layouts sits inside
  `content_bounds(…, PageId::NowPlaying)`.
- `register_media_source()` twice returns an invalid second handle.
- `publish_now_playing()` then `clear_media_session()` leaves
  `now_playing().session_open` false.
- A publish from an invalid handle changes nothing.
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

## What the hardware said, 2026-08-20

Tasks 1-7 are implemented and running on a board. Three things this document
asserted turned out to be wrong or unproven, recorded here so the next reader
does not rebuild on them.

**The artwork pipeline above cannot work as written.** `esp-raop-receiver`'s
`util.c` drops any HTTP body over 8192 bytes - it reads the remainder to
nowhere and sets `*body = NULL, *len = 0`. AirPlay artwork JPEGs are far
larger, so `raop.c`'s artwork branch sees a null body and never calls
`cmd_cb`, and `RAOP_EVENT_ARTWORK` has therefore never fired once. The
"Artwork pipeline" section starts from an event that does not arrive, and the
plan's Tasks 8-9 both need rewriting: the first thing to fix is that ceiling,
not the decoder. Note that `util.c` mallocs the body from internal RAM, which
measured 29,807 bytes free during a TLS fetch; four separate defects in this
codebase have been "free memory was ample, the largest contiguous block was
not", so that buffer wants to come from PSRAM rather than a raised internal
ceiling.

**No sender has yet delivered title, artist or album.** Two were tried, an
iPhone via YouTube Music and an Apple TV. Both give a live progress bar and
no text metadata at all - and crucially the library's own
`Unhandled SET PARAMETER` fallback does not fire either, so the DMAP request
is not merely being rejected by a branch condition, it is not reaching the
handler. An Apple TV omitting metadata is not plausible, so this reads as
receiver-side. A diagnostic line at the `SET_PARAMETER` entry (printing
`Content-Type` and whether a body survived) is deployed but has not yet caught
a session. **The cause is unknown; do not assume it is the sender.**

**The progress bar does not advance during the 60 s hold.** `ui_app.cpp`'s
seize block repaints on first show, track change, and overlay toggle only - a
progress update triggers nothing, so the bar and the times freeze for the
whole hold. This contradicts the Verification section above. The fix is to
repaint when `elapsed_ms / 1000` changes, which is the smallest interval that
alters either the `m:ss` text or the bar by a visible pixel; if one repaint
per second proves to starve the audio path, the upgrade is label-only updates
through the existing `update_visible_fields` mechanism rather than a full page
rebuild.

**One assumption that did hold**, having been tested destructively: the
layout's rects are now proven origin-invariant. The renderer originally
discarded its `bounds` and used absolute coordinates, drawing the page 6 px
right and clipping its right-hand column - caught by the board's own
`ui_geometry` check, not by the tests, which compared absolute coordinates
against an absolute box and passed throughout. The replacement tests were
verified by sabotaging the translation and confirming they fail.
