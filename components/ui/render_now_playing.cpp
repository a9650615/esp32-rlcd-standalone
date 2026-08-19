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
// Sized by i1_canvas_storage_bound(), not by hand: it mirrors
// lv_draw_buf_width_to_stride()'s own arithmetic at compile time, so the size
// is *proven* rather than guessed at with headroom. The first version of this
// buffer was a hand-rolled ((176/8)+8)*176+64, which over-allocated 1464 bytes
// for no reason anyone could state - and that helper exists precisely because
// an earlier over-allocation elsewhere in this file's neighbourhood cost 43 KB
// of .bss and stopped net_log creating its sender task.
//
// Persistent because LVGL keeps the pointer it is given rather than copying,
// so this must outlive the canvas and a stack buffer would be freed before the
// first flush - the same reason tray_indicator_icon() owns per-slot buffers.
// One buffer, because exactly one artwork is ever on screen.
constexpr std::size_t kArtworkStorageBytes = i1_canvas_storage_bound(
    kNowPlayingArtworkSize, kNowPlayingArtworkSize);
uint8_t g_artwork_storage[kArtworkStorageBytes];

// repack_artwork() rejects anything that does not fit the slot before it
// computes a runtime size, so the runtime figure can never exceed the bound
// this buffer is cut to. Asserted rather than trusted, because the two come
// from different functions and only this line keeps them agreeing.
static_assert(i1_canvas_storage_bound(kNowPlayingArtworkSize,
                                      kNowPlayingArtworkSize) <=
                  kArtworkStorageBytes,
              "artwork backing store must cover a full-slot canvas");

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

  // `bounds` (render_page()'s `content`) must be threaded straight into the
  // layout functions below, not discarded - it was `(void)bounds;` here
  // once, with every rect built from now_playing_layout()'s own absolute
  // safe-canvas literals unadjusted. LVGL positions this page's children
  // relative to `parent`, and render_page() has already placed `parent` at
  // the canvas origin - it hands renderers the zero-offset *local* frame,
  // not the absolute safe-canvas frame those literals are written in. An
  // unadjusted layout function is only correct at the one origin where the
  // two frames happen to coincide, which this page's `bounds` never is:
  // the safe canvas itself is inset kSafeMargin (6px) from the raw LVGL
  // canvas, so every rect landed 6px past where content_bounds() actually
  // put this page's content, in both x and y, and the right-hand column
  // clipped off the panel. Nothing in the host tests caught it: they
  // checked each rect against an absolute content box, which stays true
  // regardless of whether the renderer applied the right translation, no
  // translation, or a doubled one. The on-device ui_geometry warning log did -
  // "object outside safe canvas... x1=12 ... safe x=6" - and
  // now_playing_layout_shape_is_invariant_to_its_origin
  // (tests/host/test_now_playing.cpp) is what closes that gap now: it
  // proves the layout actually reacts to the content rect it is given,
  // which a fixed absolute-box assertion never could.
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
