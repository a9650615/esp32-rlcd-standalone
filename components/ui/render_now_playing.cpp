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
// palette bytes. Same transformation repack_i1_bits() does for tray icons;
// not shared with it because that one is sized for tray-scale bitmaps and
// owns its own per-slot storage.
bool repack_artwork(const app_core::MediaArtwork& artwork) {
  if (artwork.bits == nullptr || artwork.width == 0 || artwork.height == 0) {
    return false;
  }
  const std::size_t needed =
      i1_canvas_storage_bytes(artwork.width, artwork.height);
  if (needed > sizeof(g_artwork_storage)) return false;

  const int stride = i1_canvas_stride(artwork.width);
  const int source_stride = (artwork.width + 7) / 8;
  std::memset(g_artwork_storage, 0, needed);
  uint8_t* pixels = g_artwork_storage + i1_canvas_pixel_offset();
  for (int row = 0; row < artwork.height; ++row) {
    std::memcpy(pixels + row * stride, artwork.bits + row * source_stride,
                source_stride);
  }
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

void render_volume_overlay(lv_obj_t* parent,
                           const app_core::NowPlaying& media) {
  const VolumeOverlayLayout layout = volume_overlay_layout();
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
  (void)bounds;
  (void)page_index;
  (void)page_count;
  apply_surface(parent);

  const app_core::NowPlaying media = app_core::now_playing();

  if (context != nullptr && context->volume_overlay_visible) {
    render_volume_overlay(parent, media);
    return;
  }

  const bool has_artwork = repack_artwork(media.artwork);
  const NowPlayingLayout layout = now_playing_layout(has_artwork);

  if (has_artwork) {
    bind_i1_canvas(parent, layout.artwork.x, layout.artwork.y,
                   media.artwork.width, media.artwork.height,
                   g_artwork_storage, lv_color_white(), LV_OPA_COVER,
                   lv_color_black());
  }

  const lv_text_align_t align =
      has_artwork ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER;
  if (!media.source.empty()) {
    label(parent, media.source.c_str(), layout.source, font_small(), align);
  }
  // Wrapped, not clipped, so a long title uses its second line before it
  // ellipsises. The box is exactly two lines tall, so LVGL truncates at the
  // right place on its own.
  label_wrapped(parent, media.title.c_str(), layout.title, font_large(), align);
  label(parent, media.subtitle.c_str(), layout.subtitle, font_medium(), align);
  label(parent, media.detail.c_str(), layout.detail, font_small(), align);

  render_transport(parent, layout, media);
}

}  // namespace ui
