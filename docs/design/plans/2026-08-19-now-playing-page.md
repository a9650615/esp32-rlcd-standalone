# Now Playing Page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show what is playing on a media session, with a volume overlay that appears while the volume is being changed, fed by `modules/airplay`.

**Architecture:** Core gains a media-session registry shaped exactly like the existing `tray_registry` — one slot, written by whoever registered it, read by the UI. The page is `PageId::NowPlaying` and names no protocol. `modules/airplay` registers a source and publishes into it, and does its own JPEG decoding and dithering so core is handed finished 1-bit pixels. All layout maths and both state machines are pure functions in host-testable headers; only rendering and wiring touch LVGL.

**Tech Stack:** C++17, ESP-IDF 5.x, LVGL 9, the project's own host-test harness (`tests/host/test_support.hpp` — `HOST_TEST` / `EXPECT_EQ` / `EXPECT_TRUE`, no gtest).

**Spec:** `docs/design/specs/2026-08-19-now-playing-page.md`. Every coordinate in this plan comes from its Geometry section.

## Global Constraints

- **Module contract, `modules/README.md` rule 4:** dependencies point one way, module → core. No file under `components/` may include anything from `modules/`. Anything core needs from a module arrives through a registration API.
- **C++17.** `set(CMAKE_CXX_STANDARD 17)` in `tests/host/CMakeLists.txt`; the firmware matches.
- **Host tests must build without ESP-IDF.** Anything a host test includes is either LVGL-free or guarded by `UI_THEME_GEOMETRY_ONLY`. Geometry lives in `ui_data.hpp`/`ui_theme.hpp` behind that guard; rendering lives in `.cpp` files that host tests never compile.
- **Host tests need `.tools/esp-idf` to exist** — `tests/host/CMakeLists.txt` compiles `../../.tools/esp-idf/components/json/cJSON/cJSON.c`. This worktree has no `.tools`. Create the symlink once before Task 1: `ln -s "$(git -C /Users/birdyo/orca/workspaces/esp32-s3-rlcd-4.2 rev-parse --show-toplevel 2>/dev/null || echo ~/esp)/.tools" .tools` — or point it at wherever `scripts/bootstrap-idf.sh` installed ESP-IDF. `.gitignore` already covers it.
- **Host test commands, used unchanged in every task:**
  ```bash
  cmake -S tests/host -B build-host
  cmake --build build-host --parallel
  ./build-host/host_tests
  ```
  Output ends with `N cases, 0 failures` and exits 0.
- **Firmware build:** `./scripts/idf.sh build`. `CONFIG_AIRPLAY_ENABLE=y` additionally requires `modules/airplay/secrets/raop_private_key.pem` to exist or CMake fails at configure time by design.
- **Panel geometry, fixed:** canvas 400×300, safe canvas `{6, 6, 388, 288}`, content area `{6, 42, 388, 240}`.
- **Commit after every task.** Message style: lower-case type prefix, imperative subject, and the `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>` trailer this repo uses.

## File Structure

| File | Responsibility |
| --- | --- |
| `components/app_core/include/media_registry.hpp` | The core-side media-session API. Value types only. |
| `components/app_core/media_registry.cpp` | Its one-slot implementation, mutex-guarded. |
| `components/app_core/include/app_snapshot.hpp` | `PageId::NowPlaying` added to the enum. |
| `components/app_core/page_registry.cpp` | The page's descriptor and availability. |
| `components/app_core/include/now_playing_controller.hpp` | Two pure state machines: seize-then-release, and the volume overlay. |
| `components/ui/include/ui_data.hpp` | All three layouts and the text formatters, geometry-only. |
| `components/ui/render_now_playing.cpp` | Draws the layouts. LVGL. |
| `components/ui/include/ui_app.hpp` | `render_now_playing()` declaration. |
| `components/ui/render_shared.cpp` | One `switch` case. |
| `components/ui/ui_app.cpp` | Runs both state machines on the LVGL tick. |
| `modules/airplay/airplay.cpp` | Registers the source, publishes, decodes artwork. |
| `tests/host/test_media_registry.cpp` | Registry behaviour. |
| `tests/host/test_now_playing.cpp` | Layout, formatters, both state machines. |

---

### Task 1: The media-session registry

**Files:**
- Create: `components/app_core/include/media_registry.hpp`
- Create: `components/app_core/media_registry.cpp`
- Create: `tests/host/test_media_registry.cpp`
- Modify: `components/app_core/CMakeLists.txt`, `tests/host/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `app_core::MediaState`, `app_core::MediaArtwork`, `app_core::NowPlaying`, `app_core::MediaSourceHandle`, `register_media_source()`, `publish_now_playing()`, `clear_media_session()`, `now_playing()`, `reset_media_registry_for_test()`.

- [ ] **Step 1: Write the failing test**

Create `tests/host/test_media_registry.cpp`:

```c++
#include "media_registry.hpp"

#include "test_support.hpp"

HOST_TEST(media_registry_starts_with_no_session) {
  app_core::reset_media_registry_for_test();
  EXPECT_TRUE(!app_core::now_playing().session_open);
}

HOST_TEST(media_registry_accepts_one_source_and_rejects_a_second) {
  app_core::reset_media_registry_for_test();
  const app_core::MediaSourceHandle first = app_core::register_media_source();
  const app_core::MediaSourceHandle second = app_core::register_media_source();
  EXPECT_TRUE(first.valid());
  EXPECT_TRUE(!second.valid());
}

HOST_TEST(media_registry_publishes_and_reads_back) {
  app_core::reset_media_registry_for_test();
  const app_core::MediaSourceHandle handle = app_core::register_media_source();

  app_core::NowPlaying state;
  state.session_open = true;
  state.state = app_core::MediaState::Playing;
  state.source = "AIRPLAY";
  state.title = "Midnight City";
  state.subtitle = "M83";
  state.detail = "Hurry Up, We're Dreaming";
  state.elapsed_ms = 102'000;
  state.total_ms = 238'000;
  state.volume = 0.6f;
  app_core::publish_now_playing(handle, state);

  const app_core::NowPlaying read = app_core::now_playing();
  EXPECT_TRUE(read.session_open);
  EXPECT_TRUE(read.title == "Midnight City");
  EXPECT_EQ(read.elapsed_ms, 102'000u);
  EXPECT_TRUE(read.state == app_core::MediaState::Playing);
}

HOST_TEST(media_registry_ignores_publishes_from_an_invalid_handle) {
  app_core::reset_media_registry_for_test();
  app_core::NowPlaying state;
  state.session_open = true;
  state.title = "should not appear";
  app_core::publish_now_playing(app_core::MediaSourceHandle{}, state);
  EXPECT_TRUE(!app_core::now_playing().session_open);
}

HOST_TEST(media_registry_clear_closes_the_session) {
  app_core::reset_media_registry_for_test();
  const app_core::MediaSourceHandle handle = app_core::register_media_source();
  app_core::NowPlaying state;
  state.session_open = true;
  state.title = "Midnight City";
  app_core::publish_now_playing(handle, state);
  app_core::clear_media_session(handle);

  const app_core::NowPlaying read = app_core::now_playing();
  EXPECT_TRUE(!read.session_open);
  EXPECT_TRUE(read.title.empty());
}
```

Add both new files to `tests/host/CMakeLists.txt` — `test_media_registry.cpp` in the `add_executable` source list right after `test_page_registry.cpp`, and `../../components/app_core/media_registry.cpp` right after the `tray_registry.cpp` line.

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cmake -S tests/host -B build-host && cmake --build build-host --parallel
```
Expected: FAIL at compile time — `media_registry.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `components/app_core/include/media_registry.hpp`:

```c++
#pragma once

#include <cstdint>
#include <string>

// One media session, registered by a module and rendered by ui::. Shaped
// deliberately like tray_registry.hpp, for the same reason: modules/README.md
// rule 4 forbids core from knowing which module is playing something, so core
// reserves the slot and the module fills it. Nothing here names a protocol -
// AirPlay is the first source to use it, and a second one (Bluetooth A2DP, a
// local player) reuses the page rather than adding another.
//
// Not part of AppSnapshot on purpose: that struct is republished wholesale by
// several tasks on their own cadences, which is exactly how the field-
// overwrite bug documented on AppSnapshot::battery_runtime happened. A
// registry the publisher writes directly has no shared struct to be
// clobbered through.
namespace app_core {

enum class MediaState { Idle, Buffering, Playing, Paused, Stalled, Stopped };

// A 1-bit, tightly-packed, row-major MSB-first bitmap - byte for byte the
// layout TrayIndicatorBitmap already documents, so nothing new about it
// needs explaining. Owned by the publisher, which must keep it alive until
// it publishes a different one or clears the session. Core blits the bytes
// and never interprets them.
struct MediaArtwork {
  const uint8_t* bits = nullptr;
  uint16_t width = 0;
  uint16_t height = 0;
};

struct NowPlaying {
  bool session_open = false;
  MediaState state = MediaState::Idle;
  // Whatever the source calls itself: a device name if it knows one, its own
  // protocol name if not. Core supplies no default and does not care which.
  std::string source;
  std::string title;
  std::string subtitle;  // artist
  std::string detail;    // album
  uint32_t elapsed_ms = 0;
  // 0 means unknown length (a live stream), not zero-length. The page draws
  // elapsed time and no progress bar in that case.
  uint32_t total_ms = 0;
  // Negative means the source has no volume to report yet, which is distinct
  // from silence: the overlay does not open on it. 0.0-1.0 otherwise.
  float volume = -1.0f;
  // Mute is its own state, not volume 0.0 - AirPlay signals it as -144 dB.
  bool muted = false;
  MediaArtwork artwork;
};

// Returned by register_media_source(). An invalid handle (see valid()) means
// a source was already registered; callers must check rather than assume.
struct MediaSourceHandle {
  int8_t slot = -1;
  constexpr bool valid() const { return slot >= 0; }
};

// One slot, not an array: two simultaneous sources is not a real situation on
// this board. A second registration returns an invalid handle, the same way a
// full tray registry does.
MediaSourceHandle register_media_source();

// Safe to call from any task. A no-op for an invalid handle, logged loudly on
// target - an unwired handle reaching here is a caller bug and must not look
// like a successful publish in the log.
void publish_now_playing(MediaSourceHandle handle, const NowPlaying& state);

// Ends the session: everything back to defaults, so no stale title survives
// into the next one. A no-op for an invalid handle.
void clear_media_session(MediaSourceHandle handle);

// What the UI reads, every tick. A default-constructed NowPlaying
// (session_open == false) when nothing is playing.
NowPlaying now_playing();

// Test-only, same purpose as reset_tray_registry_for_test(): host tests
// register a source repeatedly across independent cases and must not leak
// state between them.
void reset_media_registry_for_test();

}  // namespace app_core
```

- [ ] **Step 4: Write the implementation**

Create `components/app_core/media_registry.cpp`:

```c++
#include "media_registry.hpp"

#include <mutex>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

namespace app_core {
namespace {

#ifdef ESP_PLATFORM
constexpr char kTag[] = "media_registry";
#endif

// A mutex rather than tray_registry.cpp's atomics: NowPlaying carries
// std::strings, and a torn read of one is a crash, not a stale value. The
// same reasoning ui_app.cpp's own publish mutex already documents. std::mutex
// and not a FreeRTOS semaphore so this file builds for host tests unchanged.
std::mutex g_mutex;
bool g_registered = false;
NowPlaying g_state;

}  // namespace

MediaSourceHandle register_media_source() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_registered) return MediaSourceHandle{};
  g_registered = true;
  return MediaSourceHandle{0};
}

void publish_now_playing(MediaSourceHandle handle, const NowPlaying& state) {
  if (!handle.valid()) {
#ifdef ESP_PLATFORM
    ESP_LOGW(kTag, "publish_now_playing ignored: invalid handle (slot=%d)",
             static_cast<int>(handle.slot));
#endif
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_state = state;
}

void clear_media_session(MediaSourceHandle handle) {
  if (!handle.valid()) {
#ifdef ESP_PLATFORM
    ESP_LOGW(kTag, "clear_media_session ignored: invalid handle (slot=%d)",
             static_cast<int>(handle.slot));
#endif
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_state = NowPlaying{};
}

NowPlaying now_playing() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_state;
}

void reset_media_registry_for_test() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_registered = false;
  g_state = NowPlaying{};
}

}  // namespace app_core
```

Add `"media_registry.cpp"` to the `SRCS` list in `components/app_core/CMakeLists.txt`, after `"tray_registry.cpp"`.

- [ ] **Step 5: Run the tests to verify they pass**

Run:
```bash
cmake -S tests/host -B build-host && cmake --build build-host --parallel && ./build-host/host_tests
```
Expected: the five `media_registry_*` cases print `PASS`, and the run ends `N cases, 0 failures`.

- [ ] **Step 6: Commit**

```bash
git add components/app_core/include/media_registry.hpp \
        components/app_core/media_registry.cpp \
        components/app_core/CMakeLists.txt \
        tests/host/test_media_registry.cpp \
        tests/host/CMakeLists.txt
git commit -m "feat: add a core media-session registry for modules to publish into

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: The page enters the rotation

**Files:**
- Modify: `components/app_core/include/app_snapshot.hpp:16`
- Modify: `components/app_core/page_registry.cpp`
- Modify: `components/ui/include/ui_data.hpp` (the two `page_shows_*` predicates)
- Create: `tests/host/test_now_playing.cpp`
- Modify: `tests/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `app_core::now_playing()`, `app_core::register_media_source()`, `app_core::publish_now_playing()`, `app_core::reset_media_registry_for_test()` (Task 1).
- Produces: `app_core::PageId::NowPlaying`.

- [ ] **Step 1: Write the failing test**

Create `tests/host/test_now_playing.cpp`. Later tasks append to this file:

```c++
#define UI_THEME_GEOMETRY_ONLY
#include "app_snapshot.hpp"
#include "media_registry.hpp"
#include "page_registry.hpp"
#include "ui_data.hpp"

#include "test_support.hpp"

#include <algorithm>

namespace {

// Opens a session with enough fields set that a layout can be built from it.
app_core::MediaSourceHandle open_test_session() {
  app_core::reset_media_registry_for_test();
  const app_core::MediaSourceHandle handle = app_core::register_media_source();
  app_core::NowPlaying state;
  state.session_open = true;
  state.state = app_core::MediaState::Playing;
  state.source = "AIRPLAY";
  state.title = "Midnight City";
  state.subtitle = "M83";
  state.elapsed_ms = 102'000;
  state.total_ms = 238'000;
  state.volume = 0.6f;
  app_core::publish_now_playing(handle, state);
  return handle;
}

bool cycle_contains(const std::vector<app_core::PageId>& pages,
                    app_core::PageId page) {
  return std::find(pages.begin(), pages.end(), page) != pages.end();
}

}  // namespace

HOST_TEST(now_playing_page_is_absent_with_no_session) {
  app_core::reset_media_registry_for_test();
  app_core::PageRegistry registry;
  registry.begin_cycle(app_core::make_mock_snapshot(
      app_core::DemoScenario::TaiwanSession));
  EXPECT_TRUE(!cycle_contains(registry.page_ids(), app_core::PageId::NowPlaying));
}

HOST_TEST(now_playing_page_joins_the_cycle_while_a_session_is_open) {
  open_test_session();
  app_core::PageRegistry registry;
  registry.begin_cycle(app_core::make_mock_snapshot(
      app_core::DemoScenario::TaiwanSession));
  EXPECT_TRUE(cycle_contains(registry.page_ids(), app_core::PageId::NowPlaying));
  app_core::reset_media_registry_for_test();
}

HOST_TEST(now_playing_page_carries_the_tray_and_the_dots) {
  EXPECT_TRUE(ui::page_shows_tray(app_core::PageId::NowPlaying));
  EXPECT_TRUE(ui::page_shows_dots(app_core::PageId::NowPlaying));
}
```

Add `test_now_playing.cpp` to `tests/host/CMakeLists.txt`, after `test_media_registry.cpp`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-host --parallel`
Expected: FAIL at compile time — `'NowPlaying' is not a member of 'app_core::PageId'`.

- [ ] **Step 3: Add the page id**

In `components/app_core/include/app_snapshot.hpp`, line 16, extend the enum. Append rather than insert — nothing persists a `PageId` today, but appending keeps it that way for free:

```c++
enum class PageId { Home, TaiwanMarket, UsMarket, Weather, Indoor, Setup, Settings, Ota, NowPlaying };
```

- [ ] **Step 4: Add the descriptor**

In `components/app_core/page_registry.cpp`, add the include at the top, next to the existing ones:

```c++
#include "media_registry.hpp"
```

In the anonymous namespace, beside `taiwan_available` and friends:

```c++
// Ignores the snapshot deliberately: this page's availability lives in the
// media registry, not in AppSnapshot, because a module - not a core provider
// task - is what fills it (modules/README.md rule 4). The PageDescriptor
// signature already permits this; no signature change is needed.
bool now_playing_available(const AppSnapshot&) {
  return now_playing().session_open;
}
```

Beside the other descriptors — 12 seconds, the same dwell every data page uses, because this page is in the rotation, not privileged within it:

```c++
const PageDescriptor kNowPlaying{PageId::NowPlaying, 12, now_playing_available};
```

In `begin_cycle`, extend the ordered array to five entries in every branch. Now Playing goes first among the optional pages, so a session that just started is the next thing rotation reaches:

```c++
  const PageDescriptor* ordered[] = {&kNowPlaying, &kTaiwan, &kUs, &kWeather, &kIndoor};
  switch (snapshot.scenario) {
    case DemoScenario::MorningAlert:
      ordered[1] = &kWeather;
      ordered[2] = &kTaiwan;
      ordered[3] = &kUs;
      ordered[4] = &kIndoor;
      break;
    case DemoScenario::TaiwanSession:
      break;
    case DemoScenario::NightSession:
      ordered[1] = &kUs;
      ordered[2] = &kWeather;
      ordered[3] = &kTaiwan;
      ordered[4] = &kIndoor;
      break;
  }
```

In `page_data_valid` further down the same file, add the new page to the list that has no snapshot-backed validity:

```c++
    case PageId::NowPlaying:
```

Place it with `PageId::Home`, `PageId::Setup` and the other cases that already fall through to `return true`.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build-host --parallel && ./build-host/host_tests`
Expected: the three `now_playing_page_*` cases print `PASS`, `N cases, 0 failures`.

Both `page_shows_tray` and `page_shows_dots` are exclusion lists (`page != PageId::Ota`, etc.), so the new page satisfies them with no edit. The third test exists to keep that true if either predicate is ever rewritten as an inclusion list.

- [ ] **Step 6: Commit**

```bash
git add components/app_core/include/app_snapshot.hpp \
        components/app_core/page_registry.cpp \
        tests/host/test_now_playing.cpp tests/host/CMakeLists.txt
git commit -m "feat: put the now-playing page in the carousel while a session is open

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: Layout geometry and text formatters

**Files:**
- Modify: `components/ui/include/ui_data.hpp` (append before the closing `}  // namespace ui`)
- Modify: `tests/host/test_now_playing.cpp` (append)

**Interfaces:**
- Consumes: `app_core::NowPlaying`, `app_core::MediaState` (Task 1); `ui::Rect`, `ui::content_bounds()`, `ui::safe_canvas()` (existing).
- Produces: `ui::NowPlayingLayout`, `ui::now_playing_layout()`, `ui::VolumeOverlayLayout`, `ui::volume_overlay_layout()`, `ui::now_playing_progress_fill_width()`, `ui::format_track_time()`, `ui::volume_percent_text()`, `ui::media_state_label()`.

- [ ] **Step 1: Write the failing test**

Append to `tests/host/test_now_playing.cpp`:

```c++
namespace {

bool inside(const ui::Rect& inner, const ui::Rect& outer) {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.right() <= outer.right() && inner.bottom() <= outer.bottom();
}

// The real content rect for this page, in the same absolute safe_canvas()
// frame every other page's own layout_fits check already uses (see
// setup_layout_fits and friends in ui_data.hpp). now_playing_layout() and
// volume_overlay_layout() both take the content rect they lay out into and
// return rects positioned relative to it - render_now_playing.cpp passes
// its own `bounds` straight through with no arithmetic of its own - so this
// is one legitimate origin to call them with; the renderer's own (zero-
// offset, page-root-relative) origin is a different one, exercised by
// now_playing_layout_shape_is_invariant_to_its_origin below.
ui::Rect now_playing_content() {
  return ui::content_bounds(ui::safe_canvas(), app_core::PageId::NowPlaying);
}

}  // namespace

HOST_TEST(now_playing_layout_stays_inside_the_content_area) {
  const ui::Rect content = now_playing_content();

  for (const bool has_artwork : {false, true}) {
    const ui::NowPlayingLayout layout =
        ui::now_playing_layout(content, has_artwork);
    EXPECT_TRUE(inside(layout.title, content));
    EXPECT_TRUE(inside(layout.subtitle, content));
    EXPECT_TRUE(inside(layout.detail, content));
    EXPECT_TRUE(inside(layout.source, content));
    EXPECT_TRUE(inside(layout.state, content));
    EXPECT_TRUE(inside(layout.time, content));
    EXPECT_TRUE(inside(layout.progress_outline, content));
    if (has_artwork) EXPECT_TRUE(inside(layout.artwork, content));
  }
}

// This is the test that would have caught the real bug: an earlier version
// of this plan (and the code it produced) had now_playing_layout() build
// every rect from absolute canvas literals with no `content` parameter at
// all, correct only by coincidence when the caller's own origin happened to
// be (0, 0). now_playing_layout_stays_inside_the_content_area above cannot
// see that - it only ever exercises one fixed absolute box, and a
// double-counted offset still landed inside a content box roomy enough to
// absorb it. Proving the layout is pure arithmetic on the rect it is
// handed - shift the origin by an arbitrary amount and check every rect
// moves by exactly that amount, same size, same position relative to every
// other rect - is what actually exercises the property the bug broke.
HOST_TEST(now_playing_layout_shape_is_invariant_to_its_origin) {
  constexpr int dx = 37;
  constexpr int dy = -19;
  const ui::Rect origin_a{0, 0, 388, 300};
  const ui::Rect origin_b{origin_a.x + dx, origin_a.y + dy, 388, 300};

  for (const bool has_artwork : {false, true}) {
    const ui::NowPlayingLayout a =
        ui::now_playing_layout(origin_a, has_artwork);
    const ui::NowPlayingLayout b =
        ui::now_playing_layout(origin_b, has_artwork);
    const ui::Rect* rects_a[] = {&a.artwork, &a.source,  &a.title,
                                 &a.subtitle, &a.detail, &a.state,
                                 &a.time,     &a.progress_outline};
    const ui::Rect* rects_b[] = {&b.artwork, &b.source,  &b.title,
                                 &b.subtitle, &b.detail, &b.state,
                                 &b.time,     &b.progress_outline};
    for (std::size_t i = 0; i < sizeof(rects_a) / sizeof(rects_a[0]); ++i) {
      EXPECT_EQ(rects_a[i]->width, rects_b[i]->width);
      EXPECT_EQ(rects_a[i]->height, rects_b[i]->height);
      // The absent-artwork rect is the zero rect at both origins.
      if (rects_a[i]->width == 0 && rects_a[i]->height == 0) continue;
      EXPECT_EQ(rects_a[i]->x + dx, rects_b[i]->x);
      EXPECT_EQ(rects_a[i]->y + dy, rects_b[i]->y);
    }
  }
}

HOST_TEST(now_playing_layouts_share_an_identical_transport_row) {
  const ui::Rect content = now_playing_content();
  const ui::NowPlayingLayout with = ui::now_playing_layout(content, true);
  const ui::NowPlayingLayout without = ui::now_playing_layout(content, false);
  EXPECT_EQ(with.state.y, without.state.y);
  EXPECT_EQ(with.state.x, without.state.x);
  EXPECT_EQ(with.time.y, without.time.y);
  EXPECT_EQ(with.progress_outline.y, without.progress_outline.y);
  EXPECT_EQ(with.progress_outline.height, without.progress_outline.height);
  EXPECT_EQ(with.progress_outline.width, without.progress_outline.width);
}

HOST_TEST(now_playing_artwork_is_square_and_absent_without_one) {
  const ui::Rect content = now_playing_content();
  const ui::NowPlayingLayout with = ui::now_playing_layout(content, true);
  EXPECT_EQ(with.artwork.width, ui::kNowPlayingArtworkSize);
  EXPECT_EQ(with.artwork.height, ui::kNowPlayingArtworkSize);
  EXPECT_EQ(ui::now_playing_layout(content, false).artwork.width, 0);
}

HOST_TEST(progress_fill_width_covers_its_whole_range) {
  // Derived independently, from an actual layout built at a real content
  // rect, rather than trusting now_playing_progress_fill_width()'s own
  // internal span - this is what "the fill width still agrees with the
  // rects" means.
  const ui::NowPlayingLayout layout =
      ui::now_playing_layout(now_playing_content(), true);
  const int full = layout.progress_outline.width - 4;
  EXPECT_EQ(ui::now_playing_progress_fill_width(0, 238'000), 0);
  EXPECT_EQ(ui::now_playing_progress_fill_width(238'000, 238'000), full);
  // A stream that overruns its declared length must not draw past the outline.
  EXPECT_EQ(ui::now_playing_progress_fill_width(400'000, 238'000), full);
  // Unknown length (a live stream): no bar at all.
  EXPECT_EQ(ui::now_playing_progress_fill_width(102'000, 0), 0);
}

HOST_TEST(track_time_formats_as_minutes_and_seconds) {
  EXPECT_TRUE(ui::format_track_time(0) == "0:00");
  EXPECT_TRUE(ui::format_track_time(102'000) == "1:42");
  EXPECT_TRUE(ui::format_track_time(238'000) == "3:58");
  EXPECT_TRUE(ui::format_track_time(3'661'000) == "61:01");
  // Truncates, never rounds up: 1:59.9 is still 1:59.
  EXPECT_TRUE(ui::format_track_time(119'900) == "1:59");
}

HOST_TEST(volume_text_distinguishes_mute_from_silence_and_from_absence) {
  EXPECT_TRUE(ui::volume_percent_text(0.6f, false) == "60");
  EXPECT_TRUE(ui::volume_percent_text(0.0f, false) == "0");
  EXPECT_TRUE(ui::volume_percent_text(1.0f, false) == "100");
  EXPECT_TRUE(ui::volume_percent_text(0.6f, true) == "MUTE");
  EXPECT_TRUE(ui::volume_percent_text(-1.0f, false).empty());
}

HOST_TEST(media_state_labels_are_printable_ascii_and_distinct) {
  const app_core::MediaState states[] = {
      app_core::MediaState::Playing, app_core::MediaState::Paused,
      app_core::MediaState::Buffering, app_core::MediaState::Stalled,
      app_core::MediaState::Stopped};
  for (const app_core::MediaState state : states) {
    const char* text = ui::media_state_label(state);
    EXPECT_TRUE(text != nullptr && text[0] != '\0');
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
      EXPECT_TRUE(*cursor >= 0x20 && *cursor <= 0x7e);
    }
  }
  EXPECT_TRUE(ui::media_state_label(app_core::MediaState::Playing) !=
              ui::media_state_label(app_core::MediaState::Paused));
}

HOST_TEST(volume_overlay_fits_the_content_area) {
  const ui::Rect content = now_playing_content();
  const ui::VolumeOverlayLayout layout = ui::volume_overlay_layout(content);
  EXPECT_TRUE(inside(layout.label, content));
  EXPECT_TRUE(inside(layout.source, content));
  EXPECT_TRUE(inside(layout.value, content));
  EXPECT_TRUE(inside(layout.bar_outline, content));
  EXPECT_EQ(ui::volume_overlay_fill_width(0.0f), 0);
  EXPECT_EQ(ui::volume_overlay_fill_width(1.0f), layout.bar_outline.width - 6);
}

// Same bug, same proof, on the overlay: shifting the content rect's origin
// must shift every overlay rect by exactly the same amount and change
// nothing else.
HOST_TEST(volume_overlay_layout_shape_is_invariant_to_its_origin) {
  constexpr int dx = 37;
  constexpr int dy = -19;
  const ui::Rect origin_a{0, 0, 388, 300};
  const ui::Rect origin_b{origin_a.x + dx, origin_a.y + dy, 388, 300};

  const ui::VolumeOverlayLayout a = ui::volume_overlay_layout(origin_a);
  const ui::VolumeOverlayLayout b = ui::volume_overlay_layout(origin_b);
  const ui::Rect* rects_a[] = {&a.label, &a.source, &a.value, &a.bar_outline};
  const ui::Rect* rects_b[] = {&b.label, &b.source, &b.value, &b.bar_outline};
  for (std::size_t i = 0; i < sizeof(rects_a) / sizeof(rects_a[0]); ++i) {
    EXPECT_EQ(rects_a[i]->width, rects_b[i]->width);
    EXPECT_EQ(rects_a[i]->height, rects_b[i]->height);
    EXPECT_EQ(rects_a[i]->x + dx, rects_b[i]->x);
    EXPECT_EQ(rects_a[i]->y + dy, rects_b[i]->y);
  }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-host --parallel`
Expected: FAIL at compile time — `'now_playing_layout' is not a member of 'ui'`.

- [ ] **Step 3: Write the geometry and formatters**

Append to `components/ui/include/ui_data.hpp`, immediately before its closing `}  // namespace ui`. It already includes `<string>` and `app_snapshot.hpp`; add `#include "media_registry.hpp"` and `#include <cstdio>` to its include block at the top of the file.

```c++
// ---------------------------------------------------------------------------
// Now Playing page
//
// Every rect below is a literal from the spec's Geometry section
// (docs/design/specs/2026-08-19-now-playing-page.md), not a derived value, so
// the numbers here and the numbers in the mockup are checkable against each
// other by eye. What is derived - and must stay derived - is the progress
// fill width, which is the only thing on the page that moves.

inline constexpr int kNowPlayingArtworkSize = 176;
// 6 + 176 + 12 = 194: the artwork's right edge plus a 12px gutter. The
// remaining 200px is what two lines of 28px type need, which is what fixed
// the artwork at 176 rather than a rounder number.
//
// This is written as an absolute safe-canvas x - the same coordinate space
// the mockup's own numbers are in, and the one every other literal in
// now_playing_layout() below uses too - not an offset from some caller's
// bounds. now_playing_layout() is what translates that fixed coordinate
// space onto whatever content rect it is actually given; see its own
// comment for why that translation, not a raw per-literal offset, is what
// this page needs.
inline constexpr int kNowPlayingTextColumnX = 194;
inline constexpr int kNowPlayingTextColumnWidth = 200;

struct NowPlayingLayout {
  Rect artwork;  // zero-width when there is no artwork
  Rect source;
  Rect title;
  Rect subtitle;
  Rect detail;
  Rect state;
  Rect time;
  Rect progress_outline;
};

// The content rect the spec's literals below (and volume_overlay_layout()'s
// own) were written against - what content_bounds(safe_canvas(), page)
// evaluates to for this page. now_playing_layout()/volume_overlay_layout()
// translate from this fixed baseline onto whatever content rect they are
// actually given: zero translation when the caller passes this rect (every
// host test above does), the real correction when render_now_playing.cpp
// passes the zero-offset local frame render_page() actually builds pages
// in.
inline constexpr Rect kNowPlayingSpecContent =
    content_bounds(safe_canvas(), app_core::PageId::NowPlaying);

// `content` is the content rect this page was handed - the same rect
// render_page() computes via content_bounds() and passes straight through
// to render_now_playing() as `bounds`. Every rect below is a spec literal
// (an absolute safe-canvas coordinate, against kNowPlayingSpecContent above)
// translated by content's own offset from that baseline (`dx`/`dy`), so the
// result is correct for whatever content rect the caller passes - the same
// "pure arithmetic on the caller's own rect" guarantee setup_layout,
// market_chart_rect and weather_forecast_rect already give their callers,
// just derived rather than written as small per-literal insets, because
// these literals are large absolute coordinates meant to be checked against
// the mockup by eye, not small margins.
//
// A first version of this function took no `content` at all and returned
// these same literals unadjusted - only correct when a caller's own content
// rect happens to be kNowPlayingSpecContent (dx == dy == 0).
// render_now_playing.cpp's `bounds` never is that: LVGL positions a page's
// children relative to the page root, and render_page() has already placed
// that root at the canvas origin, so it hands renderers the zero-offset
// local frame, not the absolute safe-canvas one these literals are written
// in. Passing that local frame through an untranslated function put every
// rect 6px right and however far down of where it belonged -
// render_shared.cpp's own local_bounds comment records the identical trap
// on the page-dots band. Nothing here caught it: a test that asserts each
// rect against an absolute content box stays true regardless of whether the
// offset applied is right, wrong, or doubled. The on-device ui_geometry
// warning log did - "object outside safe canvas... x1=12 ... safe x=6" -
// which is why now_playing_layout_shape_is_invariant_to_its_origin exists
// in Step 1 above: it is what actually exercises this function reacting to
// its origin, rather than one fixed absolute box.
constexpr NowPlayingLayout now_playing_layout(const Rect content,
                                              bool has_artwork) {
  const int dx = content.x - kNowPlayingSpecContent.x;
  const int dy = content.y - kNowPlayingSpecContent.y;

  // The transport row is identical in both layouts, byte for byte, so that
  // gaining or losing artwork mid-session never moves the bottom of the
  // screen.
  const Rect state_rect{6 + dx, 244 + dy, 120, 16};
  const Rect time_rect{254 + dx, 244 + dy, 140, 16};
  const Rect progress{6 + dx, 266 + dy, 388, 10};

  if (!has_artwork) {
    return {{0, 0, 0, 0},
            {6 + dx, 60 + dy, 388, 16},
            {6 + dx, 92 + dy, 388, 68},
            {6 + dx, 168 + dy, 388, 22},
            {6 + dx, 196 + dy, 388, 16},
            state_rect,
            time_rect,
            progress};
  }
  return {{6 + dx, 46 + dy, kNowPlayingArtworkSize, kNowPlayingArtworkSize},
          {kNowPlayingTextColumnX + dx, 46 + dy, kNowPlayingTextColumnWidth,
           16},
          {kNowPlayingTextColumnX + dx, 66 + dy, kNowPlayingTextColumnWidth,
           68},
          {kNowPlayingTextColumnX + dx, 140 + dy, kNowPlayingTextColumnWidth,
           22},
          {kNowPlayingTextColumnX + dx, 166 + dy, kNowPlayingTextColumnWidth,
           16},
          state_rect,
          time_rect,
          progress};
}

// The fill sits 2px inside the 1px outline on every edge, so its full width is
// the outline's width less 4.
//
// total_ms == 0 means "length unknown" (a live stream), not "zero length": no
// fraction can be computed, so nothing is drawn. elapsed > total is clamped
// rather than allowed to overrun - a source that overshoots its own declared
// length must not paint past the outline it was given. Called with
// kNowPlayingSpecContent itself (dx == dy == 0) - the outline's width does
// not move under translation, only x/y do, so this is just "the spec's own
// layout".
constexpr int now_playing_progress_fill_width(uint32_t elapsed_ms,
                                              uint32_t total_ms) {
  if (total_ms == 0) return 0;
  const int span =
      now_playing_layout(kNowPlayingSpecContent, true).progress_outline.width -
      4;
  if (elapsed_ms >= total_ms) return span;
  return static_cast<int>(static_cast<uint64_t>(elapsed_ms) * span / total_ms);
}

struct VolumeOverlayLayout {
  Rect label;
  Rect source;
  Rect value;
  Rect bar_outline;
};

// Same convention, translation and past mistake as now_playing_layout()
// above - see its comment and kNowPlayingSpecContent's.
constexpr VolumeOverlayLayout volume_overlay_layout(const Rect content) {
  const int dx = content.x - kNowPlayingSpecContent.x;
  const int dy = content.y - kNowPlayingSpecContent.y;
  return {{6 + dx, 50 + dy, 120, 16},
          {kNowPlayingTextColumnX + dx, 50 + dy, 200, 16},
          {6 + dx, 74 + dy, 388, 130},
          {6 + dx, 232 + dy, 388, 36}};
}

// 3px inset on every edge, matching the thicker bar's heavier outline.
constexpr int volume_overlay_fill_width(float volume) {
  const int span =
      volume_overlay_layout(kNowPlayingSpecContent).bar_outline.width - 6;
  if (volume <= 0.0f) return 0;
  if (volume >= 1.0f) return span;
  return static_cast<int>(volume * static_cast<float>(span));
}

// m:ss, or mm:ss past ten minutes, counting minutes rather than rolling over
// into hours - an hour-long track shows 61:01, which is unambiguous and needs
// no third field. Truncates: 1:59.9 is still 1:59, because a clock that
// reaches 2:00 before the track does reads as broken.
//
// The casts are load-bearing, not decoration: uint32_t is `unsigned int` on
// the host and `unsigned long` on xtensa, so a bare %u compiles clean under
// the host tests and fails the firmware build outright under -Werror=format.
inline std::string format_track_time(uint32_t milliseconds) {
  const uint32_t total_seconds = milliseconds / 1000;
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%u:%02u",
                static_cast<unsigned>(total_seconds / 60),
                static_cast<unsigned>(total_seconds % 60));
  return buffer;
}

// The digits only - the '%' is drawn separately, because the 128px face is
// rlcd_digits_128.c and has no glyph for it.
//
// Three distinct returns, deliberately: "MUTE" for a real muted source
// (AirPlay's -144 dB), "0" for a source turned all the way down but not
// muted, and an empty string for a source that has not reported a level at
// all. The overlay does not open on the empty case.
inline std::string volume_percent_text(float volume, bool muted) {
  if (volume < 0.0f) return {};
  if (muted) return "MUTE";
  const int percent = static_cast<int>(volume * 100.0f + 0.5f);
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "%d", percent < 0 ? 0
                                              : percent > 100 ? 100 : percent);
  return buffer;
}

// ASCII only, and not routed through ui_strings.hpp, for the same reason
// app_core::ota_phase_label() is not: these are five fixed transport words
// that read identically in both interface languages, and the arrow glyphs
// come from the interface font rather than the CJK one.
constexpr const char* media_state_label(app_core::MediaState state) {
  switch (state) {
    case app_core::MediaState::Playing: return "> PLAY";
    case app_core::MediaState::Paused: return "|| PAUSE";
    case app_core::MediaState::Buffering: return "... BUFFER";
    case app_core::MediaState::Stalled: return "! STALL";
    case app_core::MediaState::Stopped: return "# STOP";
    // Never reaches the page: Idle only occurs alongside session_open ==
    // false, and then the page is not in rotation at all.
    case app_core::MediaState::Idle: return "";
  }
  return "";
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build-host --parallel && ./build-host/host_tests`
Expected: the ten new cases print `PASS`, `N cases, 0 failures`.

- [ ] **Step 5: Commit**

```bash
git add components/ui/include/ui_data.hpp tests/host/test_now_playing.cpp
git commit -m "feat: add now-playing layout geometry and its text formatters

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: The two state machines

**Files:**
- Create: `components/app_core/include/now_playing_controller.hpp`
- Modify: `tests/host/test_now_playing.cpp` (append)

**Interfaces:**
- Consumes: `app_core::NowPlaying` (Task 1).
- Produces: `app_core::SeizeState`, `app_core::seize_tick()`, `app_core::kNowPlayingSeizeSeconds`, `app_core::VolumeOverlayState`, `app_core::volume_overlay_tick()`, `app_core::kVolumeOverlayMs`.

Header-only. Both are pure functions over plain structs, like `carousel_controller.hpp` next to them, so a host test can drive a whole session in a loop with no clock and no tasks.

- [ ] **Step 1: Write the failing test**

Append to `tests/host/test_now_playing.cpp`, and add `#include "now_playing_controller.hpp"` to its include block:

```c++
HOST_TEST(seize_takes_the_screen_when_a_session_opens) {
  app_core::SeizeState state;
  EXPECT_TRUE(!app_core::seize_tick(state, false, "", 1'000).owns_screen);

  const app_core::SeizeState opened =
      app_core::seize_tick(state, true, "Midnight City", 1'000);
  EXPECT_TRUE(opened.owns_screen);
}

HOST_TEST(seize_releases_after_the_hold_and_stays_released) {
  app_core::SeizeState state =
      app_core::seize_tick(app_core::SeizeState{}, true, "Midnight City", 1'000);
  const uint64_t hold_ms = app_core::kNowPlayingSeizeSeconds * 1000;

  state = app_core::seize_tick(state, true, "Midnight City", 1'000 + hold_ms - 1);
  EXPECT_TRUE(state.owns_screen);

  state = app_core::seize_tick(state, true, "Midnight City", 1'000 + hold_ms);
  EXPECT_TRUE(!state.owns_screen);

  state = app_core::seize_tick(state, true, "Midnight City", 9'999'999);
  EXPECT_TRUE(!state.owns_screen);
}

HOST_TEST(seize_restarts_on_a_new_title_but_not_on_a_republished_one) {
  const uint64_t hold_ms = app_core::kNowPlayingSeizeSeconds * 1000;
  app_core::SeizeState state =
      app_core::seize_tick(app_core::SeizeState{}, true, "Midnight City", 1'000);
  state = app_core::seize_tick(state, true, "Midnight City", 1'000 + hold_ms);
  EXPECT_TRUE(!state.owns_screen);

  // Same title again: no reason to grab the screen back.
  state = app_core::seize_tick(state, true, "Midnight City", 1'000 + hold_ms + 5);
  EXPECT_TRUE(!state.owns_screen);

  // A different one is a new track.
  state = app_core::seize_tick(state, true, "Reunion", 1'000 + hold_ms + 10);
  EXPECT_TRUE(state.owns_screen);
}

HOST_TEST(seize_lets_go_the_moment_the_session_closes) {
  app_core::SeizeState state =
      app_core::seize_tick(app_core::SeizeState{}, true, "Midnight City", 1'000);
  EXPECT_TRUE(state.owns_screen);
  state = app_core::seize_tick(state, false, "", 1'500);
  EXPECT_TRUE(!state.owns_screen);
}

HOST_TEST(volume_overlay_opens_on_a_change_and_closes_on_a_timer) {
  app_core::VolumeOverlayState state;
  // First observation only records the level; there is nothing to compare it
  // against yet, so restoring a volume at connect time must not flash the
  // overlay.
  state = app_core::volume_overlay_tick(state, 0.6f, true, 1'000);
  EXPECT_TRUE(!state.visible);

  state = app_core::volume_overlay_tick(state, 0.7f, true, 2'000);
  EXPECT_TRUE(state.visible);

  state = app_core::volume_overlay_tick(state, 0.7f, true,
                                        2'000 + app_core::kVolumeOverlayMs - 1);
  EXPECT_TRUE(state.visible);

  state = app_core::volume_overlay_tick(state, 0.7f, true,
                                        2'000 + app_core::kVolumeOverlayMs);
  EXPECT_TRUE(!state.visible);
}

HOST_TEST(volume_overlay_stays_shut_when_the_page_is_not_on_screen) {
  app_core::VolumeOverlayState state =
      app_core::volume_overlay_tick(app_core::VolumeOverlayState{}, 0.6f, false,
                                    1'000);
  state = app_core::volume_overlay_tick(state, 0.9f, false, 2'000);
  EXPECT_TRUE(!state.visible);

  // Leaving the page closes an overlay that was already up.
  app_core::VolumeOverlayState open =
      app_core::volume_overlay_tick(app_core::VolumeOverlayState{}, 0.6f, true,
                                    1'000);
  open = app_core::volume_overlay_tick(open, 0.9f, true, 2'000);
  EXPECT_TRUE(open.visible);
  open = app_core::volume_overlay_tick(open, 0.9f, false, 2'100);
  EXPECT_TRUE(!open.visible);
}

HOST_TEST(volume_overlay_ignores_a_source_with_no_level_to_report) {
  app_core::VolumeOverlayState state;
  state = app_core::volume_overlay_tick(state, -1.0f, true, 1'000);
  state = app_core::volume_overlay_tick(state, -1.0f, true, 2'000);
  EXPECT_TRUE(!state.visible);

  // The first real level after that is still only a baseline, not a change.
  state = app_core::volume_overlay_tick(state, 0.4f, true, 3'000);
  EXPECT_TRUE(!state.visible);
  state = app_core::volume_overlay_tick(state, 0.5f, true, 4'000);
  EXPECT_TRUE(state.visible);
}

HOST_TEST(seize_takes_the_screen_when_a_session_opens_with_no_title_yet) {
  // A session can open before any metadata has arrived: session_open true,
  // no title published yet. Comparing titles alone ("" != "") would never
  // seize in that case, and a source that never publishes a title at all
  // would never seize, ever.
  app_core::SeizeState state;
  state = app_core::seize_tick(state, true, "", 1'000);
  EXPECT_TRUE(state.owns_screen);
}

HOST_TEST(seize_ignores_a_clock_that_moves_backward) {
  app_core::SeizeState state = app_core::seize_tick(
      app_core::SeizeState{}, true, "Midnight City", 10'000);
  EXPECT_TRUE(state.owns_screen);

  // now_ms goes backward - an unguarded subtraction would underflow and
  // release the hold immediately.
  state = app_core::seize_tick(state, true, "Midnight City", 1'000);
  EXPECT_TRUE(state.owns_screen);
}

HOST_TEST(volume_overlay_ignores_a_clock_that_moves_backward) {
  app_core::VolumeOverlayState state;
  state = app_core::volume_overlay_tick(state, 0.6f, true, 10'000);
  state = app_core::volume_overlay_tick(state, 0.7f, true, 20'000);
  EXPECT_TRUE(state.visible);

  // now_ms goes backward - an unguarded subtraction would underflow and
  // close the overlay immediately.
  state = app_core::volume_overlay_tick(state, 0.7f, true, 1'000);
  EXPECT_TRUE(state.visible);
}

HOST_TEST(volume_overlay_closes_on_timeout_even_while_volume_is_unknown) {
  app_core::VolumeOverlayState state;
  state = app_core::volume_overlay_tick(state, 0.6f, true, 1'000);
  state = app_core::volume_overlay_tick(state, 0.7f, true, 2'000);
  EXPECT_TRUE(state.visible);

  // The source stops reporting a level while the overlay is up. The timeout
  // must still fire; a negative reading must not pin the overlay open.
  state = app_core::volume_overlay_tick(state, -1.0f, true,
                                        2'000 + app_core::kVolumeOverlayMs);
  EXPECT_TRUE(!state.visible);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-host --parallel`
Expected: FAIL at compile time — `now_playing_controller.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `components/app_core/include/now_playing_controller.hpp`:

```c++
#pragma once

#include <cstdint>
#include <string>

// Two small state machines for the now-playing page, kept out of ui_app.cpp
// for the same reason carousel_controller.hpp next door is: they are pure
// functions over plain structs, so a host test can drive a whole session in a
// loop with no clock, no tasks, and no LVGL.
namespace app_core {

// How long the page holds the screen when a session opens, before rotation
// resumes. A starting value to tune against the real panel, not a derived
// number - it lives here, beside the other timing constants, precisely so
// that tuning it is a one-line change and not an archaeology exercise.
inline constexpr uint32_t kNowPlayingSeizeSeconds = 60;

// How long the volume overlay stays up after the last change. Deliberately
// the same 2 s as ui::kNavigationOverlayDurationMs: both are "something just
// happened, show it briefly", and two different durations for that would read
// as an inconsistency rather than a distinction.
inline constexpr uint32_t kVolumeOverlayMs = 2'000;

struct SeizeState {
  bool owns_screen = false;
  uint64_t seized_ms = 0;
  // What was playing when the screen was last seized. Compared by value: a
  // republished identical title is the same track, and only a genuinely
  // different one is a new one worth interrupting the carousel for.
  std::string title;
  // Whether this session has already been seen open on some earlier tick.
  // Needed because a session can open before any metadata has arrived -
  // session_open true, title still "" - and SeizeState{}'s own title also
  // defaults to "", so comparing titles alone would read "" != "" as false
  // and never seize. was_open makes the first tick of an open session seize
  // unconditionally, whatever the title is at that point.
  bool was_open = false;
};

// `now_ms` is the same monotonic millisecond clock the carousel already runs
// on. Returns the next state; the caller keeps it.
inline SeizeState seize_tick(SeizeState state, bool session_open,
                             const std::string& title, uint64_t now_ms) {
  if (!session_open) return SeizeState{};
  if (!state.was_open || title != state.title) {
    // Covers the first tick of a session (was_open false, so this fires
    // regardless of title) and every later track change (title differs from
    // the one last seized on) with the same branch.
    return SeizeState{true, now_ms, title, true};
  }
  // Guard against a backward-moving clock the same way carousel_controller
  // does (see its tick()): an unguarded now_ms - state.seized_ms would
  // underflow to near UINT64_MAX and release the hold immediately. A clock
  // that has gone backward has not yet reached the timeout, so it must not
  // release either.
  if (state.owns_screen && now_ms >= state.seized_ms &&
      now_ms - state.seized_ms >= kNowPlayingSeizeSeconds * 1000) {
    state.owns_screen = false;
  }
  return state;
}

struct VolumeOverlayState {
  bool visible = false;
  uint64_t shown_ms = 0;
  // Negative means nothing has been observed yet. The first real reading only
  // establishes a baseline - without that, restoring a volume when a session
  // connects would flash the overlay for a change nobody made.
  float last_volume = -1.0f;
};

// `page_on_screen` is whether the now-playing page is the page currently
// rendered - not whether a session is open. The overlay annotates this page;
// popping it over the weather would be a notification, which is a different
// feature nobody asked for.
inline VolumeOverlayState volume_overlay_tick(VolumeOverlayState state,
                                              float volume,
                                              bool page_on_screen,
                                              uint64_t now_ms) {
  if (!page_on_screen) {
    state.visible = false;
    return state;
  }
  // A negative reading means the source has nothing to report right now; it
  // must not open the overlay or move the baseline, but it also must not
  // pin an already-open overlay past its timeout, so it falls straight
  // through to the close check below instead of returning early.
  if (volume >= 0.0f) {
    if (state.last_volume < 0.0f) {
      state.last_volume = volume;  // baseline only
      return state;
    }
    if (volume != state.last_volume) {
      // Exact equality is safe today only because volume is a direct
      // passthrough from the publishing module with no local arithmetic on
      // it. A future source that derives it (averaging, scaling) would need
      // an epsilon compare instead.
      state.last_volume = volume;
      state.visible = true;
      state.shown_ms = now_ms;
      return state;
    }
  }
  // Guard against a backward-moving clock the same way seize_tick() and
  // carousel_controller's tick() do: an unguarded subtraction would
  // underflow and close the overlay immediately. A clock that has gone
  // backward has not yet reached the timeout, so it must not close either.
  if (state.visible && now_ms >= state.shown_ms &&
      now_ms - state.shown_ms >= kVolumeOverlayMs) {
    state.visible = false;
  }
  return state;
}

}  // namespace app_core
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build-host --parallel && ./build-host/host_tests`
Expected: the eleven new cases print `PASS`, `N cases, 0 failures`.

- [ ] **Step 5: Commit**

```bash
git add components/app_core/include/now_playing_controller.hpp \
        tests/host/test_now_playing.cpp
git commit -m "feat: add the seize-then-release and volume-overlay state machines

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Render the page

**Files:**
- Create: `components/ui/render_now_playing.cpp`
- Modify: `components/ui/include/ui_app.hpp` (declaration, after `render_settings`)
- Modify: `components/ui/render_shared.cpp` (one `switch` case in `render_page`)
- Modify: `components/ui/CMakeLists.txt`

**Interfaces:**
- Consumes: `ui::now_playing_layout()`, `ui::volume_overlay_layout()`, `ui::now_playing_progress_fill_width()`, `ui::volume_overlay_fill_width()`, `ui::now_playing_artwork_fits_slot()`, `ui::format_track_time()`, `ui::volume_percent_text()`, `ui::media_state_label()` (Task 3); `app_core::now_playing()` (Task 1); `ui::label()`, `ui::label_wrapped()`, `ui::line_segment()`, `ui::apply_surface()`, `ui::bind_i1_canvas()`, `ui::repack_i1_bits()`, `ui::i1_canvas_storage_bytes()`, `ui::i1_canvas_stride()`, `ui::i1_canvas_pixel_offset()`, `ui::font_small/medium/large/hero()` (existing).

**`font_hero()` has a rule attached.** Its declaration in `ui_fonts.hpp` says
"Only the clock may use it": it is a 128px subset of the ten digits and a
colon, and every other character renders as a silent empty box — which is how
the OTA page once showed `WORKING` as five boxes. The volume readout is the
second legitimate caller because `volume_percent_text()` returns digits or
nothing, and its one non-digit return, `MUTE`, is drawn in `font_large()`
instead. Step 3 below widens that comment to say so; do not use this face
without reading it.
- Produces: `ui::render_now_playing(lv_obj_t*, const app_core::AppSnapshot&, Rect, std::size_t, std::size_t, UiContext* = nullptr)`, `ui::now_playing_artwork_fits_slot(int, int)` (Task 3's file, ui_data.hpp - pure and host-tested, since the renderer that uses it never can be).

No host test for the renderer itself — this file is LVGL and host tests do not compile it. Its geometry is already covered by Task 3; what is left is verified on the panel in Step 5. The one piece of new logic that is not pure geometry - whether a publisher's reported artwork size fits the reserved slot - lives as `ui::now_playing_artwork_fits_slot()` in `ui_data.hpp` instead of inline here for exactly that reason: that file is LVGL-free and host-tested, so the rule is checkable even though the renderer calling it never can be. Add `HOST_TEST` cases to `tests/host/test_now_playing.cpp` covering exactly-176x176 (accepted), smaller (accepted), wider (rejected), taller (rejected), and zero dimensions (rejected).

- [ ] **Step 1: Declare it**

In `components/ui/include/ui_app.hpp`, after the `render_settings` declaration, matching the signature every other renderer uses:

```c++
// Draws whatever app_core's media registry currently holds - core never
// learns which module published it (modules/README.md rule 4). Reads the
// registry directly rather than taking it through AppSnapshot; see
// media_registry.hpp for why that state is not a snapshot field.
void render_now_playing(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                        Rect bounds, std::size_t page_index,
                        std::size_t page_count, UiContext* context = nullptr);
```

- [ ] **Step 2: Write the renderer**

Create `components/ui/render_now_playing.cpp`:

```c++
#include "media_registry.hpp"
#include "ui_app.hpp"
#include "ui_fonts.hpp"

#include <cstring>
#include <string>

namespace ui {
namespace {

// Persistent backing store for the artwork canvas, for exactly the reason
// tray_indicator_icon()'s own per-slot buffers exist: LVGL keeps the pointer
// it is given rather than copying, so this must outlive the canvas, and a
// stack buffer would be freed before the first flush. One buffer, because
// exactly one artwork is ever on screen.
//
// Sized from LVGL's own stride rather than a tight (width+7)/8 pack - see
// i1_canvas_stride()'s comment for the debugging round that distinction cost
// once already.
uint8_t g_artwork_storage[/* 4.3 KB at 176x176 */
    ((176 / 8) + 8) * 176 + 64];

// Repacks a tight-packed, row-major MSB-first module bitmap (exactly what
// app_core::MediaArtwork documents) into LVGL's padded stride, after the
// palette bytes, by calling repack_i1_bits() (ui_theme.hpp) - the same shared
// building block tray_indicator_icon() uses for its own bitmaps. It is
// genuinely shared, not merely similar: every dimension is a parameter, it
// owns no storage of its own, and it already does this exact row copy plus
// its own bounds checks. repack_i1_bits() returns void and silently does
// nothing if its arguments do not fit (see its own comment), so the fit
// decision - is there a bitmap at all, does it fit the reserved artwork slot,
// does it fit this file's own backing buffer - has to be made here, before
// the call, not inferred from what the call did.
bool repack_artwork(const app_core::MediaArtwork& artwork) {
  if (artwork.bits == nullptr ||
      !now_playing_artwork_fits_slot(artwork.width, artwork.height)) {
    return false;
  }
  const std::size_t needed =
      i1_canvas_storage_bytes(artwork.width, artwork.height);
  if (needed > sizeof(g_artwork_storage)) return false;

  const int stride = i1_canvas_stride(artwork.width);
  std::memset(g_artwork_storage, 0, needed);
  repack_i1_bits(artwork.bits, g_artwork_storage, sizeof(g_artwork_storage),
                 artwork.width, artwork.height, stride,
                 i1_canvas_pixel_offset());
  return true;
}

void render_transport(lv_obj_t* parent, const NowPlayingLayout& layout,
                      const app_core::NowPlaying& media) {
  label(parent, media_state_label(media.state), layout.state, font_small());

  // Elapsed alone when the length is unknown: "1:42 / 0:00" would be a claim
  // about a live stream's duration that nobody made.
  const std::string time =
      media.total_ms == 0
          ? format_track_time(media.elapsed_ms)
          : format_track_time(media.elapsed_ms) + " / " +
                format_track_time(media.total_ms);
  label(parent, time.c_str(), layout.time, font_small(), LV_TEXT_ALIGN_RIGHT);

  const Rect outline = layout.progress_outline;
  const int fill =
      now_playing_progress_fill_width(media.elapsed_ms, media.total_ms);
  // Four 1px segments rather than a bordered object: line_segment() is what
  // every other rule on this panel is drawn with, and a styled border would
  // be a second way to make a rectangle.
  line_segment(parent, outline.x, outline.y, outline.width, 1);
  line_segment(parent, outline.x, outline.bottom() - 1, outline.width, 1);
  line_segment(parent, outline.x, outline.y, 1, outline.height);
  line_segment(parent, outline.right() - 1, outline.y, 1, outline.height);
  if (fill > 0) {
    line_segment(parent, outline.x + 2, outline.y + 2, fill,
                 outline.height - 4);
  }
}

void render_volume_overlay(lv_obj_t* parent, const Rect bounds,
                           const app_core::NowPlaying& media) {
  const VolumeOverlayLayout layout = volume_overlay_layout(bounds);
  label(parent, "VOLUME", layout.label, font_small());
  if (!media.source.empty()) {
    label(parent, media.source.c_str(), layout.source, font_small(),
          LV_TEXT_ALIGN_RIGHT);
  }

  const std::string value = volume_percent_text(media.volume, media.muted);
  if (media.muted) {
    // "MUTE" has no glyphs in the 128px face - it is digits and a colon and
    // nothing else - so the word goes in the large interface font instead, in
    // the same box. Passing it to font_hero() would draw four empty boxes and
    // no warning; see that function's own declaration.
    label(parent, value.c_str(), layout.value, font_large(),
          LV_TEXT_ALIGN_CENTER);
  } else {
    // Digits only reach font_hero(), which is the whole reason
    // volume_percent_text() returns the number without its sign. The '%' is a
    // separate label in the interface font, tucked against the right of the
    // same box.
    label(parent, value.c_str(), layout.value, font_hero(),
          LV_TEXT_ALIGN_CENTER);
    label(parent, "%", {layout.value.right() - 40, layout.value.y + 8, 34, 34},
          font_large(), LV_TEXT_ALIGN_LEFT);
  }

  const Rect bar = layout.bar_outline;
  line_segment(parent, bar.x, bar.y, bar.width, 1);
  line_segment(parent, bar.x, bar.bottom() - 1, bar.width, 1);
  line_segment(parent, bar.x, bar.y, 1, bar.height);
  line_segment(parent, bar.right() - 1, bar.y, 1, bar.height);
  const int fill = media.muted ? 0 : volume_overlay_fill_width(media.volume);
  if (fill > 0) {
    line_segment(parent, bar.x + 3, bar.y + 3, fill, bar.height - 6);
  }
}

}  // namespace

void render_now_playing(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                        Rect bounds, std::size_t page_index,
                        std::size_t page_count, UiContext* context) {
  // Page position lives in the system tray, like every other page.
  (void)snapshot;
  (void)page_index;
  (void)page_count;
  apply_surface(parent);

  const app_core::NowPlaying media = app_core::now_playing();

  // `bounds` (render_page()'s `content`) is threaded straight into the
  // layout functions below, not discarded. It was `(void)bounds;` here in
  // an earlier version of this plan, with every rect built from
  // now_playing_layout()'s own absolute safe-canvas literals unadjusted.
  // LVGL positions this page's children relative to `parent`, and
  // render_page() has already placed `parent` at the canvas origin - it
  // hands renderers the zero-offset local frame, not the absolute
  // safe-canvas frame those literals are written in. See
  // now_playing_layout()'s own comment (Task 3) for the full story: what
  // shipped, what it looked like on the panel, and why the tests did not
  // catch it.
  if (context != nullptr && context->volume_overlay_visible) {
    render_volume_overlay(parent, bounds, media);
    return;
  }

  const bool has_artwork = repack_artwork(media.artwork);
  const NowPlayingLayout layout = now_playing_layout(bounds, has_artwork);

  if (has_artwork) {
    bind_i1_canvas(parent, layout.artwork.x, layout.artwork.y,
                   media.artwork.width, media.artwork.height,
                   g_artwork_storage, lv_color_white(), LV_OPA_COVER,
                   lv_color_black());
  }

  const lv_text_align_t align =
      has_artwork ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER;
  // source/title/subtitle/detail are all drawn unconditionally, empty or
  // not: an empty string paints nothing on this reflective panel (no glyphs,
  // not a visible blank rectangle), so a guard here would only skip one
  // label() call, never change what is on screen. Four fields that behave
  // identically should read as four fields that behave identically, rather
  // than three unconditional calls and a fourth one dressed up as a special
  // case it is not.
  label(parent, media.source.c_str(), layout.source, font_small(), align);
  // Wrapped, not clipped, so a long title uses its second line before it
  // ellipsises. The box is exactly two lines tall, so LVGL truncates at the
  // right place on its own.
  label_wrapped(parent, media.title.c_str(), layout.title, font_large(), align);
  label(parent, media.subtitle.c_str(), layout.subtitle, font_medium(), align);
  label(parent, media.detail.c_str(), layout.detail, font_small(), align);

  render_transport(parent, layout, media);
}

}  // namespace ui
```

- [ ] **Step 3: Widen the `font_hero()` rule to name its second caller**

In `components/ui/include/ui_fonts.hpp`, the comment above `font_hero()` says
"Only the clock may use it". That is now one caller short, and a stale rule is
worse than none. Replace that sentence with:

```c++
// Two callers only: the clock hero, and the volume readout on the now-playing
// page (render_now_playing.cpp). Both pass digits and nothing else, which is
// the actual rule - ui::volume_percent_text() exists in the shape it does
// precisely so that its one non-digit return, "MUTE", never reaches this face.
// Anything else renders the missing characters as empty boxes, silently: the
```

Leave the rest of the comment, from "sensor page lost its temperature" onward,
exactly as it is — that is the evidence for the rule and it has not changed.

- [ ] **Step 4: Reach it from `render_page`**

In `components/ui/render_shared.cpp`, in the `switch (page)` inside `render_page`, add the case beside `PageId::Settings`:

```c++
    case app_core::PageId::NowPlaying:
      render_now_playing(replacement, snapshot, content, page_index, page_count,
                         &context);
      break;
```

Add `"render_now_playing.cpp"` to `SRCS` in `components/ui/CMakeLists.txt`, after `"render_settings.cpp"`.

- [ ] **Step 5: Add the context flag the renderer reads**

In `components/ui/include/ui_app.hpp`, inside `struct UiContext`, beside `settings_focus`:

```c++
  // Set by the LVGL tick from app_core::volume_overlay_tick() before it asks
  // for a rebuild, read by render_now_playing(). Held here rather than passed
  // as a parameter so the renderer keeps the signature every other page has.
  bool volume_overlay_visible = false;
```

- [ ] **Step 6: Build the firmware and check it on the panel**

Run: `./scripts/idf.sh build`
Expected: build succeeds. `render_now_playing.cpp` appears in the build log.

The page cannot be reached yet — Task 6 wires the tick and Task 7 publishes real data. Verifying the pixels happens at the end of Task 7, which is the first point at which anything real is on screen.

- [ ] **Step 7: Commit**

```bash
git add components/ui/render_now_playing.cpp components/ui/include/ui_app.hpp \
        components/ui/render_shared.cpp components/ui/CMakeLists.txt
git commit -m "feat: render the now-playing page and its volume overlay

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Wire both state machines into the LVGL tick

**Files:**
- Modify: `components/ui/ui_app.cpp` (`Runtime` struct, and `timer_callback`)

**Interfaces:**
- Consumes: `app_core::seize_tick()`, `app_core::volume_overlay_tick()`, `app_core::SeizeState`, `app_core::VolumeOverlayState` (Task 4); `app_core::now_playing()` (Task 1); `ui::render_now_playing()` (Task 5).
- Produces: nothing new; this is wiring.

No host test — `ui_app.cpp` is LVGL-bound and host tests do not compile it. Both state machines are already fully tested in Task 4; what is added here is only the call sites.

- [ ] **Step 1: Add the state to `Runtime`**

In `components/ui/ui_app.cpp`, add the include beside the existing ones:

```c++
#include "now_playing_controller.hpp"
```

In the `Runtime` struct, beside `carousel`:

```c++
  app_core::SeizeState now_playing_seize;
  app_core::VolumeOverlayState volume_overlay;
  bool showing_now_playing = false;
```

- [ ] **Step 2: Run both machines on the tick**

In `timer_callback`, immediately **before** the block that begins
`if (!runtime->snapshot.setup.active && !runtime->showing_settings && ...)`, so
the seize is evaluated before the carousel gets its turn. Position alone does
NOT make OTA and Setup outrank it — this block returns early, so anything below
it never runs. `takeover_page_owns_screen` is what actually enforces that
precedence, and the carousel block below is rewritten to use the same flag
rather than repeating its four conditions:

```c++
  // Both machines run every tick regardless of which page is up: the seize
  // has to notice a session opening while another page is showing, and the
  // overlay's own timer has to expire even on the tick where nothing else
  // changed.
  const app_core::NowPlaying media = app_core::now_playing();
  const app_core::SeizeState previous_seize = runtime->now_playing_seize;
  runtime->now_playing_seize = app_core::seize_tick(
      previous_seize, media.session_open, media.title, now_ms);

  // The four pages that own the screen for a reason of their own, in one
  // named place because both the seize below and the carousel block further
  // down must respect exactly the same set. Without this, a media session
  // takes the screen away from an OTA write in progress - a track title on
  // the panel while the firmware writes its own flash, and nothing left
  // telling anyone not to pull the power.
  const bool takeover_page_owns_screen =
      runtime->snapshot.setup.active || runtime->showing_settings ||
      app_core::ota_owns_screen(runtime->snapshot.ota) ||
      app_core::ota_awaits_confirm(runtime->snapshot.ota);

  const bool page_is_now_playing =
      !takeover_page_owns_screen &&
      (runtime->now_playing_seize.owns_screen ||
       (!runtime->active_pages.empty() &&
        runtime->active_pages[runtime->carousel.index] ==
            app_core::PageId::NowPlaying));

  const bool overlay_was_visible = runtime->volume_overlay.visible;
  runtime->volume_overlay = app_core::volume_overlay_tick(
      runtime->volume_overlay, media.volume, page_is_now_playing, now_ms);
  runtime->context.volume_overlay_visible = runtime->volume_overlay.visible;

  const bool now_playing_owns_screen =
      runtime->now_playing_seize.owns_screen && !takeover_page_owns_screen;

  if (now_playing_owns_screen) {
    // Rebuild only on a real transition - taking the screen, changing track,
    // or the overlay appearing or disappearing. A rebuild every tick would
    // repaint the whole reflective panel ten times a second, which is exactly
    // what update_visible_fields() exists to avoid.
    const bool seize_changed =
        !previous_seize.owns_screen ||
        previous_seize.title != runtime->now_playing_seize.title;
    if (!runtime->showing_now_playing || seize_changed ||
        overlay_was_visible != runtime->volume_overlay.visible) {
      const lv_obj_t* rendered =
          render_page(runtime->context, runtime->snapshot,
                      app_core::PageId::NowPlaying, safe_canvas(), 0, 0);
      if (rendered == nullptr) ESP_LOGE(kTag, "renderer failure page=NowPlaying");
      page_rebuilt = true;
    }
    runtime->showing_now_playing = true;
    // Keeps the carousel's dwell timer from expiring behind the seize, so
    // releasing lands on a fresh dwell rather than an already-stale one.
    runtime->carousel.page_started_ms = now_ms;
  } else if (runtime->showing_now_playing) {
    // The release tick repaints explicitly, exactly like ota-exit and
    // setup-exit elsewhere in this function. The carousel cannot do it:
    // page_started_ms was stamped on every tick of the hold, so here the
    // dwell has run ~100 ms and carousel::tick reports no change. The panel
    // would hold the last now-playing frame until the residual dwell elapsed
    // and then jump to the *next* page, skipping the one it was actually on
    // when the session opened.
    // The flag clears either way, but the repaint is only worth doing when
    // the hold ended on its own. If a takeover page grabbed the screen
    // mid-hold, its own block renders this same tick, and painting the
    // carousel first would be two full-panel repaints inside one tick.
    runtime->showing_now_playing = false;
    if (!takeover_page_owns_screen) {
      runtime->carousel.page_started_ms = now_ms;
      (void)render_current(*runtime, "now-playing-exit", false);
      page_rebuilt = true;
    }
  }
```

`page_started_ms` is `CarouselState`'s dwell origin
(`carousel_controller.hpp:13`). Stamping it means "restart the dwell".

There is deliberately **no early `return`** in that block. Everything below it
in `timer_callback` — the carousel, the takeover pages, and the clock and tray
updates at the end of the function — still has to run while the seize holds the
screen. A version that returned skipped `update_clock()` and
`update_tray_indicators()` for the whole 60 s hold, which froze the clock on a
page that does carry the tray (`page_shows_tray` excludes only `Ota`) and
reintroduced, for a full minute, the missed-indicator-blip failure
`update_tray_indicators`' own comment exists to document.

So the carousel block below changes its condition rather than relying on the
return, and the takeover blocks need no change at all — while the seize holds
the screen, none of `setup.active`, `showing_settings` or the two `ota_*`
predicates can be true, since `takeover_page_owns_screen` is what let the seize
run in the first place:

```c++
  if (!takeover_page_owns_screen && !now_playing_owns_screen) {
```

- [ ] **Step 3: Rebuild when the overlay toggles on the carousel's own page**

Still in `timer_callback`, in the existing block that renders on
`transition.page_changed`, extend the condition so an overlay change on the
now-playing page also forces a rebuild while the carousel — not the seize —
owns the screen:

```c++
    if (transition.page_changed ||
        (current_page == app_core::PageId::NowPlaying &&
         overlay_was_visible != runtime->volume_overlay.visible)) {
```

Leave the body of that block unchanged.

- [ ] **Step 4: Build**

Run: `./scripts/idf.sh build`
Expected: build succeeds with no warnings from `ui_app.cpp`.

- [ ] **Step 5: Commit**

```bash
git add components/ui/ui_app.cpp
git commit -m "feat: run the seize and volume-overlay machines on the LVGL tick

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: Publish from the AirPlay module

**Files:**
- Modify: `modules/airplay/airplay.cpp`
- Modify: `modules/airplay/README.md` (touch-point list, rule 5)

**Interfaces:**
- Consumes: `app_core::register_media_source()`, `app_core::publish_now_playing()`, `app_core::clear_media_session()`, `app_core::NowPlaying`, `app_core::MediaState` (Task 1).
- Produces: nothing core-side. This is the module filling the slot core reserved.

Artwork is not part of this task — `NowPlaying::artwork` stays default (null), so the page renders Layout B. Tasks 8 and 9 add it.

- [ ] **Step 1: Hold the session state and publish it**

In `modules/airplay/airplay.cpp`, add the include beside `tray_registry.hpp`:

```c++
#include "media_registry.hpp"
```

In the anonymous namespace, beside `g_tray_indicator`:

```c++
// The module's own copy of what it has published so far. Kept here rather
// than read back out of the registry because RAOP delivers this in pieces -
// METADATA carries no progress, PROGRESS carries no title - and each event
// must update its own fields without blanking the others.
app_core::MediaSourceHandle g_media_source;
app_core::NowPlaying g_now_playing;

void publish() {
  // Volume is read at publish time rather than tracked: raop_get_volume()
  // returns the current level in SOFTWARE mode too (RAOP_EVENT_VOLUME is
  // HARDWARE-only), so there is nothing to subscribe to and nothing to keep
  // in sync.
  g_now_playing.volume = g_handle != nullptr ? raop_get_volume(g_handle) : -1.0f;
  app_core::publish_now_playing(g_media_source, g_now_playing);
}
```

- [ ] **Step 2: Register the source**

In `airplay_init()`, directly after the existing tray-indicator registration block, and for the same reason it sits there — the slot is claimed whether or not `raop_init()` later succeeds:

```c++
  if (!g_media_source.valid()) {
    g_media_source = app_core::register_media_source();
    if (!g_media_source.valid()) {
      ESP_LOGW(kTag, "media source registration failed: already taken");
    }
  }
```

- [ ] **Step 3: Handle the remaining events**

`handle_event` currently handles only `CONNECTED` and `DISCONNECTED`, and its own comment says every other event is deliberately ignored. That comment is now wrong: the audio path still ignores them, but the page needs them. Replace the "deliberately not handled here" paragraph with:

```c++
// Every other event (BUFFERING/PLAYING/STOPPED/PAUSED/METADATA/ARTWORK/
// PROGRESS/STALLED) is a sub-state within one still-open session. None of
// them touches the audio path - audio_stream_write() holds the amplifier
// continuously across chunks and only drops it at close, so reacting to
// play/pause there would add close-then-reopen churn for no benefit. They
// are handled below solely to keep the now-playing page current.
```

Extend the `switch` in `handle_event`. `event_data` is now used, so drop the
`/*event_data*/` comment from the parameter name:

```c++
    case RAOP_EVENT_METADATA: {
      const auto* meta = static_cast<const raop_metadata_t*>(event_data);
      if (meta != nullptr) {
        g_now_playing.title = meta->title != nullptr ? meta->title : "";
        g_now_playing.subtitle = meta->artist != nullptr ? meta->artist : "";
        g_now_playing.detail = meta->album != nullptr ? meta->album : "";
      }
      publish();
      break;
    }
    case RAOP_EVENT_PROGRESS: {
      const auto* progress = static_cast<const raop_progress_t*>(event_data);
      if (progress != nullptr) {
        g_now_playing.elapsed_ms = progress->current_ms;
        g_now_playing.total_ms = progress->total_ms;
      }
      publish();
      break;
    }
    case RAOP_EVENT_PLAYING:
      g_now_playing.state = app_core::MediaState::Playing;
      publish();
      break;
    case RAOP_EVENT_PAUSED:
      g_now_playing.state = app_core::MediaState::Paused;
      publish();
      break;
    case RAOP_EVENT_BUFFERING:
      g_now_playing.state = app_core::MediaState::Buffering;
      publish();
      break;
    case RAOP_EVENT_STALLED:
      g_now_playing.state = app_core::MediaState::Stalled;
      publish();
      break;
    case RAOP_EVENT_STOPPED:
      g_now_playing.state = app_core::MediaState::Stopped;
      g_now_playing.elapsed_ms = 0;
      publish();
      break;
```

In the existing `RAOP_EVENT_CONNECTED` case, after the tray indicator is set active:

```c++
      g_now_playing = app_core::NowPlaying{};
      g_now_playing.session_open = true;
      g_now_playing.state = app_core::MediaState::Buffering;
      // The protocol's own name, not a device name: RAOP exposes no API for
      // what the sender calls itself (see the spec's "the device name has no
      // source"). A name invented here would be a claim nothing backs.
      g_now_playing.source = "AIRPLAY";
      publish();
```

In `RAOP_EVENT_DISCONNECTED`, after the tray indicator is set inactive:

```c++
      g_now_playing = app_core::NowPlaying{};
      app_core::clear_media_session(g_media_source);
```

- [ ] **Step 4: Record the touch points**

`modules/README.md` rule 5 requires the module's own README to name every core
touch point exactly. In `modules/airplay/README.md`, add to that list:

```markdown
- `app_core::register_media_source()` in `airplay_init()`, and
  `app_core::publish_now_playing()` / `app_core::clear_media_session()` from
  `handle_event()` (both in `airplay.cpp`). Core reserves one media-session
  slot and renders whatever fills it; it never learns that RAOP is what did.
  See `components/app_core/include/media_registry.hpp`.
```

- [ ] **Step 5: Build and flash**

Run:
```bash
./scripts/idf.sh build
./scripts/idf.sh flash monitor
```
Expected: build succeeds. `CONFIG_AIRPLAY_ENABLE=y` requires
`modules/airplay/secrets/raop_private_key.pem`; without it CMake stops at
configure time with the explanatory error that file's check already prints.

- [ ] **Step 6: Verify on the panel**

Connect an iPhone over AirPlay and play a track with known metadata. Confirm,
in order:

1. The page takes the screen within a second of connecting.
2. Title, artist, and album are the track's real values, not placeholders.
3. The progress bar advances and the times count up.
4. Pause on the phone: the state line reads `|| PAUSE` and the bar freezes.
5. Change the volume on the phone: the 128px overlay appears and clears about
   two seconds after the last change.
6. After 60 seconds the carousel resumes and the page comes back around in
   rotation.
7. Disconnect: the page leaves the rotation.

Record what actually happened. A step that fails is a finding for the next
task, not something to patch silently in this one.

- [ ] **Step 7: Commit**

```bash
git add modules/airplay/airplay.cpp modules/airplay/README.md
git commit -m "feat: publish AirPlay session state into the media registry

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 8: Measure the artwork before decoding it

**Files:**
- Modify: `modules/airplay/airplay.cpp` (temporary logging, removed in Task 9)

This is a measurement, not a feature. The spec calls the JPEG's size unmeasured
and the decoder unchosen; both stay that way until this produces numbers. Do not
start Task 9 before this task's numbers exist.

- [ ] **Step 1: Log what arrives**

In `handle_event`, add:

```c++
    case RAOP_EVENT_ARTWORK: {
      const auto* artwork = static_cast<const raop_artwork_t*>(event_data);
      // Temporary: sizing evidence for the decode path (Task 9), removed once
      // that lands. Logs the first bytes so the container is identifiable -
      // ff d8 ff is JPEG, 89 50 4e 47 is PNG, and AirPlay senders do send
      // both.
      if (artwork != nullptr) {
        ESP_LOGI(kTag,
                 "artwork: %u bytes, first bytes %02x %02x %02x %02x, "
                 "free internal=%u psram=%u",
                 static_cast<unsigned>(artwork->len),
                 artwork->len > 0 ? artwork->data[0] : 0,
                 artwork->len > 1 ? artwork->data[1] : 0,
                 artwork->len > 2 ? artwork->data[2] : 0,
                 artwork->len > 3 ? artwork->data[3] : 0,
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
      }
      break;
    }
```

Add `#include <esp_heap_caps.h>` to the includes if it is not already there.

- [ ] **Step 2: Collect the numbers**

Run: `./scripts/idf.sh flash monitor`

Play at least five tracks with different artwork — including one album with
large cover art — and record for each: byte count, magic bytes, free internal
heap, and free PSRAM. Also note whether the event fires once per track or
repeatedly.

- [ ] **Step 3: Write the numbers down**

Add a short section to `docs/design/specs/2026-08-19-now-playing-page.md` under
"What is not proven yet", replacing the unmeasured claim with the measurements
and the date. If any image is not JPEG, that changes what Task 9 has to decode
and must be recorded here before Task 9 starts.

- [ ] **Step 4: Commit**

```bash
git add modules/airplay/airplay.cpp docs/design/specs/2026-08-19-now-playing-page.md
git commit -m "chore: measure AirPlay artwork size and format on real hardware

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 9: Decode artwork to 1-bit

**Files:**
- Modify: `modules/airplay/airplay.cpp`
- Possibly modify: `modules/airplay/CMakeLists.txt` and `idf_component.yml` (decoder dependency)

**Interfaces:**
- Consumes: `ui::dither_bayer4x4_dark()` from `components/ui/include/dither.hpp` — module → core, the allowed direction; a header of pure inline functions with no LVGL in it. `app_core::MediaArtwork` (Task 1).
- Produces: a populated `NowPlaying::artwork`, which makes `render_now_playing()` take Layout A.

**Prerequisite:** Task 8's measurements exist. If they show the source is not JPEG, or the buffer does not fit the budget they revealed, stop and revise the spec rather than pushing through.

- [ ] **Step 1: Add the decode buffer and the pipeline**

In `modules/airplay/airplay.cpp`, in the anonymous namespace:

```c++
// 176x176 packed 1-bit: 22 bytes per row, exactly the tight, row-major,
// MSB-first layout app_core::MediaArtwork documents. Static rather than
// heap-allocated because the registry holds a pointer to it for the session's
// lifetime and there is never more than one artwork on screen.
constexpr int kArtworkSize = 176;
constexpr int kArtworkStride = (kArtworkSize + 7) / 8;
uint8_t g_artwork_bits[kArtworkStride * kArtworkSize];

// Decodes `data` into 176x176 8-bit greyscale in `grey`, which the caller
// allocates in PSRAM (30.3 KB). Returns false on any decoder error, which
// leaves the page on Layout B - a failed decode must never put a half-drawn
// image on the panel.
bool decode_to_grey(const uint8_t* data, size_t len, uint8_t* grey);

// Greyscale to 1-bit through the panel's existing ordered-dither pattern.
// dither_bayer4x4_dark() is already per-pixel and already takes a 0-16 level,
// so nothing new is written here: the 0-255 sample is scaled to that range and
// handed straight over. 176 is far above kMinDitherDimensionPx (16), so the
// pattern is inside the range that was measured on the real panel.
void dither_into_bits(const uint8_t* grey) {
  std::memset(g_artwork_bits, 0, sizeof(g_artwork_bits));
  for (int y = 0; y < kArtworkSize; ++y) {
    for (int x = 0; x < kArtworkSize; ++x) {
      // Dark where the source is dark: level is inverted luminance.
      const int level = (255 - grey[y * kArtworkSize + x]) * 16 / 255;
      if (ui::dither_bayer4x4_dark(x, y, level)) {
        g_artwork_bits[y * kArtworkStride + x / 8] |= 0x80 >> (x % 8);
      }
    }
  }
}
```

Implement `decode_to_grey` with whatever decoder Task 8's evidence supports.
The constraint the spec sets, and the one that decides this: it must decode
from a buffer to a scaled output without materialising a full-size RGB frame.
A 500×500 RGB888 intermediate is 750 KB and is not acceptable even in PSRAM
while audio buffers are competing for it. If the chosen decoder pulls in a
managed component, gate it in `idf_component.yml` and `CMakeLists.txt` exactly
the way the existing `mdns` dependency is, so `CONFIG_AIRPLAY_ENABLE=n` still
links none of it.

Add `#include "dither.hpp"` and `#include <cstring>` to the file's includes.

- [ ] **Step 2: Hook it to the event**

Replace Task 8's temporary logging case with the real one:

```c++
    case RAOP_EVENT_ARTWORK: {
      const auto* artwork = static_cast<const raop_artwork_t*>(event_data);
      if (artwork == nullptr || artwork->data == nullptr || artwork->len == 0) {
        break;
      }
      uint8_t* grey = static_cast<uint8_t*>(
          heap_caps_malloc(kArtworkSize * kArtworkSize, MALLOC_CAP_SPIRAM));
      if (grey == nullptr) {
        ESP_LOGW(kTag, "artwork: no PSRAM for the decode buffer, skipping");
        break;
      }
      if (decode_to_grey(artwork->data, artwork->len, grey)) {
        dither_into_bits(grey);
        g_now_playing.artwork = {g_artwork_bits, kArtworkSize, kArtworkSize};
      } else {
        ESP_LOGW(kTag, "artwork: decode failed, falling back to text layout");
        g_now_playing.artwork = {};
      }
      heap_caps_free(grey);
      publish();
      break;
    }
```

Clear the artwork alongside the rest of the session state — the existing
`g_now_playing = app_core::NowPlaying{};` lines in `CONNECTED` and
`DISCONNECTED` already do this, since `artwork` is one of its members. Confirm
that is still true after editing rather than assuming it.

- [ ] **Step 3: Build and flash**

Run: `./scripts/idf.sh build && ./scripts/idf.sh flash monitor`
Expected: build succeeds; no `artwork: decode failed` in the log for normal tracks.

- [ ] **Step 4: Verify on the panel**

1. Play a track with cover art. The 176×176 dithered image appears on the left
   and the text moves to the right column.
2. Look at the image from about a metre away. It should read as a picture, not
   as noise. If it does not, the problem is the luminance scaling in
   `dither_into_bits`, not the dither pattern — `/dither-card` already proved
   the pattern on this panel.
3. Play a track with no cover art. The page falls back to the centred text
   layout with no gap where the image would be.
4. Skip between tracks several times. Free PSRAM in the log returns to the same
   figure each time; a downward drift is a leak in the decode path.

- [ ] **Step 5: Commit**

```bash
git add modules/airplay/airplay.cpp modules/airplay/CMakeLists.txt \
        modules/airplay/idf_component.yml
git commit -m "feat: decode AirPlay artwork to 1-bit for the now-playing page

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Self-review

**Spec coverage.** Every section maps to a task: the core API and the registry
reasoning to Task 1; `PageId::NowPlaying`, the descriptor, and the tray/dots
predicates to Task 2; all three layouts, the progress and volume maths, the
mute/absent distinction and the state labels to Task 3; seize-then-release and
the overlay timer to Task 4; the two renderers to Task 5; the tick wiring to
Task 6; the RAOP→`MediaState` mapping, the source-name decision and the
touch-point record to Task 7; the artwork pipeline to Tasks 8 and 9.

Two spec items are deliberately not tasks. "No playback control", "no marquee",
"no artwork cache" and "no queue view" are the things this plan does not build,
and the plan builds none of them. The device name has no API, so Task 7
publishes the protocol name and the layout is unchanged either way, exactly as
the spec requires.

**Known gap.** `decode_to_grey()` in Task 9 is the one function whose body this
plan does not write, because choosing a decoder before Task 8's measurements
exist would be a guess dressed as a plan. Task 9 states the constraint it must
satisfy and the prerequisite that gates it.
