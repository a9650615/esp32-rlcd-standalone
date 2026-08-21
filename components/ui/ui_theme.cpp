#include "ui_theme.hpp"
#include "ui_fonts.hpp"
#include "ui_data.hpp"

#include <algorithm>
#include <cstring>
#include <new>

#ifndef NDEBUG
#include <esp_log.h>
#endif

namespace ui {
namespace {

lv_color_t ink(bool inverse) { return inverse ? lv_color_white() : lv_color_black(); }

// Every LVGL style setter invalidates unconditionally.
// lv_obj_set_local_style_prop() ends in lv_obj_refresh_style(), which calls
// lv_obj_invalidate() with no old-versus-new comparison anywhere in the path
// (managed_components/lvgl__lvgl/src/core/lv_obj_style.c, twice in fact).
//
// That is free on a backlit panel already redrawing 60 times a second, and
// ruinous here: lvgl_port.cpp configures LV_DISPLAY_RENDER_MODE_FULL, so one
// invalidated object means the entire 400x300 framebuffer is re-rendered and
// the whole panel rewritten - ~28 ms reading the draw buffer out of PSRAM plus
// 10-50 ms of SPI. Writing an object the value it already holds is not a no-op
// on this board; it is a full repaint.
//
// So the setters that run on the ~100 ms UI tick compare first, through these.
// Measured before they existed: 4.4 full-panel rewrites per second against
// 0.87 publishes per second, continuously, because update_tray_indicators()
// rewrites every slot's opacity on every tick by design - the tray registry
// has no change event, so polling is correct and it is the write that has to
// be conditional. Visible on the panel as the dither pattern of a cover
// crawling, which is what a fine 1-bit pattern looks like when it is being
// rewritten four times a second.
//
// LV_OBJ_FLAG_HIDDEN would not have avoided it either: lv_obj_area_is_visible()
// tests that flag and not opacity, so an icon parked at LV_OPA_TRANSP is
// invalidated exactly like a visible one.
//
// Named `_if_changed` after set_label_text_if_changed() and
// set_setup_status_if_changed(), which this file already had for the same
// reason applied to text.
void set_style_opa_if_changed(lv_obj_t* obj, lv_opa_t opa) {
  if (obj == nullptr) return;
  if (lv_obj_get_style_opa(obj, LV_PART_MAIN) == opa) return;
  lv_obj_set_style_opa(obj, opa, 0);
}

void set_style_bg_opa_if_changed(lv_obj_t* obj, lv_opa_t opa) {
  if (obj == nullptr) return;
  if (lv_obj_get_style_bg_opa(obj, LV_PART_MAIN) == opa) return;
  lv_obj_set_style_bg_opa(obj, opa, 0);
}

// The style width rather than lv_obj_get_width(): the latter reports the laid
// out width, which is 0 until LVGL's first layout pass has run, so comparing
// against it would skip the very first write.
void set_width_if_changed(lv_obj_t* obj, int32_t width) {
  if (obj == nullptr) return;
  if (lv_obj_get_style_width(obj, LV_PART_MAIN) == width) return;
  lv_obj_set_width(obj, width);
}

}  // namespace

void apply_setup_status_style(lv_obj_t* label_obj, bool is_error) {
  if (label_obj == nullptr) return;
  // A 1-bit reflective panel has no colour to flag an error with, so invert
  // the block instead - black bar behind white text, the same convention
  // navigation_overlay uses for its own always-visible banner - rather than
  // the plain black-on-white every other label on the page uses.
  lv_obj_set_style_bg_color(label_obj, ink(!is_error), 0);
  lv_obj_set_style_bg_opa(label_obj, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(label_obj, ink(is_error), 0);
  lv_obj_set_style_text_outline_stroke_color(label_obj, ink(is_error), 0);
}

void apply_surface(lv_obj_t* object) {
  lv_obj_set_style_bg_color(object, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(object, lv_color_black(), 0);
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_shadow_width(object, 0, 0);
  lv_obj_set_style_radius(object, 0, 0);
  lv_obj_set_style_pad_all(object, 0, 0);
  lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* label_wrapped(lv_obj_t* parent, const char* text, Rect bounds,
                        const lv_font_t* font, lv_text_align_t align) {
  lv_obj_t* object = label(parent, text, bounds, font, align, true);
  if (object != nullptr) lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
  return object;
}

lv_obj_t* label(lv_obj_t* parent, const char* text, Rect bounds,
                const lv_font_t* font, lv_text_align_t align, bool wraps) {
  lv_obj_t* object = lv_label_create(parent);
  if (object == nullptr) return nullptr;
  apply_surface(object);
  const lv_font_t* effective_font =
      font == nullptr ? LV_FONT_DEFAULT : font;
  const int font_line_height = effective_font->line_height;
  lv_obj_set_pos(object, bounds.x, bounds.y);
  lv_obj_set_size(object, bounds.width,
                  safe_text_box_height(bounds.height, font_line_height));
  lv_label_set_text(object, text == nullptr ? "" : text);
  if (font != nullptr) lv_obj_set_style_text_font(object, font, 0);
  lv_obj_set_style_pad_all(object, kTextInset, 0);
  lv_obj_set_style_text_align(object, align, 0);
  lv_obj_set_style_text_line_space(object, 0, 0);
  const int outline_width = text_outline_width(font_line_height);
  lv_obj_set_style_text_outline_stroke_color(object, lv_color_black(), 0);
  lv_obj_set_style_text_outline_stroke_width(object, outline_width, 0);
  lv_obj_set_style_text_outline_stroke_opa(
      object, outline_width > 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  // LV_LABEL_LONG_DOT, not LONG_CLIP. Clipping removes characters with no
  // trace, which on this project's terms is the same defect as showing a
  // number nobody measured: "Up to date" clipped to "pdate to date" reads as a
  // message rather than as damage. An ellipsis says the text was too long.
  //
  // It also matters more than it used to. Box widths here were all derived
  // from Latin metrics, and a CJK glyph is roughly twice as wide as an average
  // Latin one at the same size, so translated strings meet these edges far
  // sooner than English ones ever did.
  lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);

#ifndef NDEBUG
  // Truncation is now visible on the panel, but only to someone looking at it.
  // This puts every instance in the serial log with its measured and available
  // widths, so a layout that no longer fits its text can be found the same way
  // an out-of-bounds object is - by capture, not by eye.
  // A wrapping label is not clipped by being wider than its box - that is the
  // point of wrapping - so it is excluded rather than reported. The caller
  // says so up front instead of the check guessing from the box height.
  const char* measured = (text == nullptr || wraps) ? "" : text;
  if (measured[0] != '\0') {
    const int32_t width = lv_text_get_width(
        measured, static_cast<uint32_t>(std::strlen(measured)),
        effective_font, 0);
    const int available = bounds.width - 2 * kTextInset;
    if (width > available) {
      ESP_LOGW("ui_text", "clipped: \"%s\" needs %dpx, box gives %dpx",
               measured, static_cast<int>(width), available);
    }
  }
#endif
  return object;
}

lv_obj_t* divider(lv_obj_t* parent, Rect bounds) {
  lv_obj_t* object = lv_obj_create(parent);
  if (object == nullptr) return nullptr;
  apply_surface(object);
  lv_obj_set_pos(object, bounds.x, bounds.y);
  lv_obj_set_size(object, std::max(1, bounds.width), std::max(1, bounds.height));
  lv_obj_set_style_bg_color(object, lv_color_black(), 0);
  return object;
}

lv_obj_t* line_segment(lv_obj_t* parent, int x, int y, int width, int height,
                       bool inverse) {
  lv_obj_t* object = lv_obj_create(parent);
  if (object == nullptr) return nullptr;
  apply_surface(object);
  lv_obj_set_pos(object, x, y);
  lv_obj_set_size(object, std::max(1, width), std::max(1, height));
  lv_obj_set_style_bg_color(object, ink(inverse), 0);
  return object;
}

// A filled circle - the same primitive page_dots below already uses for a
// bold round silhouette instead of a thin outline. On a reflective,
// backlight-less panel a large filled area reads at a glance; a fine
// outline does not.
lv_obj_t* filled_circle(lv_obj_t* parent, int center_x, int center_y,
                        int diameter, bool inverse) {
  lv_obj_t* object = lv_obj_create(parent);
  if (object == nullptr) return nullptr;
  apply_surface(object);
  lv_obj_set_pos(object, center_x - diameter / 2, center_y - diameter / 2);
  lv_obj_set_size(object, std::max(1, diameter), std::max(1, diameter));
  lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(object, ink(inverse), 0);
  return object;
}

// A rounded base plus two lobes of different sizes: one bold, continuous
// silhouette, still readable on a panel where thin outlines disappear.
//
// The base is a pill rather than a rectangle. A square-cornered bar under two
// circles reads as a block with bumps, not a cloud, and at icon sizes those
// two hard bottom corners are most of what the eye picks up. The lobes are
// deliberately unequal and off-centre for the same reason: two identical
// circles side by side read as symmetrical machinery.
void draw_cloud(lv_obj_t* parent, Rect bounds, bool inverse) {
  const int base_height = std::max(3, bounds.height * 2 / 5);
  const int base_y = bounds.bottom() - base_height;
  lv_obj_t* base =
      line_segment(parent, bounds.x, base_y, bounds.width, base_height, inverse);
  if (base != nullptr) lv_obj_set_style_radius(base, LV_RADIUS_CIRCLE, 0);
  const int big = std::max(3, bounds.height * 7 / 10);
  const int small = std::max(3, bounds.height * 1 / 2);
  filled_circle(parent, bounds.x + bounds.width * 38 / 100, base_y + 1, big,
                inverse);
  filled_circle(parent, bounds.x + bounds.width * 68 / 100, base_y + 2, small,
                inverse);
}

void draw_sun(lv_obj_t* parent, Rect bounds, bool inverse) {
  const int center_x = bounds.x + bounds.width / 2;
  const int center_y = bounds.y + bounds.height / 2;
  // Only axis-aligned rectangles are available, so the sun gets four rays
  // rather than the usual eight. That makes the disc carry the recognition:
  // it is sized generously and the rays read as short stubs around it, which
  // holds together far better than a small disc with long thin spokes.
  const int body_diameter =
      std::max(2, std::min(bounds.width, bounds.height) * 7 / 10);
  filled_circle(parent, center_x, center_y, body_diameter, inverse);
  const int ray_width = std::max(2, body_diameter / 5);
  const int vertical_reach = std::max(0, (bounds.height - body_diameter) / 2);
  const int horizontal_reach = std::max(0, (bounds.width - body_diameter) / 2);
  line_segment(parent, center_x - ray_width / 2, bounds.y, ray_width,
              vertical_reach, inverse);
  line_segment(parent, center_x - ray_width / 2, center_y + body_diameter / 2,
              ray_width, vertical_reach, inverse);
  line_segment(parent, bounds.x, center_y - ray_width / 2, horizontal_reach,
              ray_width, inverse);
  line_segment(parent, center_x + body_diameter / 2, center_y - ray_width / 2,
              horizontal_reach, ray_width, inverse);
}

void draw_rain(lv_obj_t* parent, Rect bounds, bool inverse) {
  const int cloud_height = bounds.height * 3 / 5;
  draw_cloud(parent, {bounds.x, bounds.y, bounds.width, cloud_height}, inverse);
  const int drop_width = std::max(2, bounds.width / 10);
  const int drop_y = bounds.y + cloud_height + 2;
  const int drop_height = std::max(2, bounds.bottom() - drop_y);
  line_segment(parent, bounds.x + bounds.width * 2 / 10, drop_y, drop_width,
              drop_height, inverse);
  line_segment(parent, bounds.x + bounds.width * 5 / 10, drop_y, drop_width,
              drop_height, inverse);
  line_segment(parent, bounds.x + bounds.width * 8 / 10 - drop_width, drop_y,
              drop_width, drop_height, inverse);
}

void draw_snow(lv_obj_t* parent, Rect bounds, bool inverse) {
  const int cloud_height = bounds.height * 3 / 5;
  draw_cloud(parent, {bounds.x, bounds.y, bounds.width, cloud_height}, inverse);
  const int flake_diameter = std::max(2, bounds.width / 8);
  const int flake_y = bounds.bottom() - flake_diameter;
  filled_circle(parent, bounds.x + bounds.width * 2 / 10, flake_y,
               flake_diameter, inverse);
  filled_circle(parent, bounds.x + bounds.width * 5 / 10, flake_y,
               flake_diameter, inverse);
  filled_circle(parent, bounds.x + bounds.width * 8 / 10, flake_y,
               flake_diameter, inverse);
}

void weather_icon(lv_obj_t* parent, Rect bounds, WeatherIconKind kind,
                  bool inverse) {
  switch (kind) {
    case WeatherIconKind::Sun:
      draw_sun(parent, bounds, inverse);
      return;
    case WeatherIconKind::Rain:
      draw_rain(parent, bounds, inverse);
      return;
    case WeatherIconKind::Snow:
      draw_snow(parent, bounds, inverse);
      return;
    case WeatherIconKind::Cloud:
    default:
      draw_cloud(parent, bounds, inverse);
      return;
  }
}

// A thermometer: bulb, stem, and two scale ticks. The previous version was
// four thin bars that read as a tally mark rather than an instrument - the
// same failure the weather icons had, where fine strokes disappear on a panel
// with no backlight and contrast that depends on the room.
//
// The bulb is the recognition: a filled circle under a filled stem is a
// thermometer at almost any size, where an outline of one is not.
void temperature_icon(lv_obj_t* parent, Rect bounds, bool inverse) {
  const int centre_x = bounds.x + bounds.width / 2;
  const int bulb = std::max(6, bounds.width / 2);
  const int stem_width = std::max(3, bulb / 3);
  const int stem_top = bounds.y + 1;
  const int bulb_centre_y = bounds.bottom() - bulb / 2 - 1;

  // Stem runs into the bulb so the two read as one body, not a ball on a pole.
  line_segment(parent, centre_x - stem_width / 2, stem_top, stem_width,
               bulb_centre_y - stem_top, inverse);
  filled_circle(parent, centre_x, bulb_centre_y, bulb, inverse);

  // Two ticks on the left, which is what separates a thermometer from a pin.
  const int tick_width = std::max(3, bulb / 2);
  const int tick_x = centre_x - stem_width / 2 - tick_width - 1;
  const int span = bulb_centre_y - stem_top;
  line_segment(parent, tick_x, stem_top + span / 4, tick_width, 2, inverse);
  line_segment(parent, tick_x, stem_top + span / 2, tick_width, 2, inverse);
}

void humidity_icon(lv_obj_t* parent, Rect bounds, bool inverse) {
  const int center_x = bounds.x + bounds.width / 2;
  line_segment(parent, center_x, bounds.y + 1, 2, bounds.height - 2, inverse);
  line_segment(parent, center_x - 5, bounds.y + bounds.height / 2, 12, 2,
               inverse);
  line_segment(parent, center_x - 4, bounds.y + 2, 2, bounds.height - 4,
               inverse);
  line_segment(parent, center_x + 4, bounds.y + 2, 2, bounds.height - 4,
               inverse);
}

namespace {

// Regenerate exactly with: python3 scripts/svg-to-bitmap.py
// components/ui/assets/wifi-high-bold.svg --width 20 --height 20 --fit viewbox
// --threshold 0.35 --min-stroke 1 --emit-bytes kWifiHighBoldBitmap
constexpr uint8_t kWifiHighBoldBitmap[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x00,
    0x0f, 0xff, 0x00, 0x1f, 0x9f, 0x80, 0x78, 0x01, 0xe0, 0x61, 0xf8, 0x60,
    0x07, 0xfe, 0x00, 0x0f, 0x0f, 0x00, 0x1c, 0x03, 0x80, 0x01, 0xf8, 0x00,
    0x03, 0xfc, 0x00, 0x03, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00,
    0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static_assert(sizeof(kWifiHighBoldBitmap) == app_core::kTrayIconBitmapBytes,
              "wifi-high-bold bitmap must be 60 tight-packed bytes");

// Regenerate exactly with: python3 scripts/svg-to-bitmap.py
// components/ui/assets/wifi-slash-bold.svg --width 20 --height 20 --fit viewbox
// --threshold 0.35 --min-stroke 1 --emit-bytes kWifiSlashBoldBitmap
constexpr uint8_t kWifiSlashBoldBitmap[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x1c, 0x70, 0x00,
    0x0c, 0xff, 0x00, 0x1e, 0x1f, 0x80, 0x7f, 0x01, 0xe0, 0x63, 0x80, 0x60,
    0x07, 0xce, 0x00, 0x0f, 0xef, 0x00, 0x1c, 0x73, 0x80, 0x01, 0xf8, 0x00,
    0x03, 0xfc, 0x00, 0x03, 0x0e, 0x00, 0x00, 0x07, 0x00, 0x00, 0x63, 0x00,
    0x00, 0x63, 0x80, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static_assert(sizeof(kWifiSlashBoldBitmap) == app_core::kTrayIconBitmapBytes,
              "wifi-slash-bold bitmap must be 60 tight-packed bytes");

alignas(LV_DRAW_BUF_ALIGN) uint8_t g_wifi_high_storage[
    i1_canvas_storage_bound(app_core::kTrayIconSize, app_core::kTrayIconSize)];
alignas(LV_DRAW_BUF_ALIGN) uint8_t g_wifi_slash_storage[
    i1_canvas_storage_bound(app_core::kTrayIconSize, app_core::kTrayIconSize)];

}  // namespace

// Two static I1 canvases share a tray cell. State updates change opacity only,
// avoiding a page rebuild and its full-panel redraw.
WifiIconParts wifi_icon(lv_obj_t* parent, Rect bounds, bool connected) {
  WifiIconParts parts{};

  repack_i1_bits(kWifiHighBoldBitmap, g_wifi_high_storage,
                 sizeof(g_wifi_high_storage), app_core::kTrayIconSize,
                 app_core::kTrayIconSize,
                 i1_canvas_stride(app_core::kTrayIconSize),
                 i1_canvas_pixel_offset());
  repack_i1_bits(kWifiSlashBoldBitmap, g_wifi_slash_storage,
                 sizeof(g_wifi_slash_storage), app_core::kTrayIconSize,
                 app_core::kTrayIconSize,
                 i1_canvas_stride(app_core::kTrayIconSize),
                 i1_canvas_pixel_offset());

  parts.connected = bind_i1_canvas(
      parent, bounds.x, bounds.y, app_core::kTrayIconSize,
      app_core::kTrayIconSize, g_wifi_high_storage, lv_color_white(),
      LV_OPA_TRANSP, lv_color_black());
  parts.disconnected = bind_i1_canvas(
      parent, bounds.x, bounds.y, app_core::kTrayIconSize,
      app_core::kTrayIconSize, g_wifi_slash_storage, lv_color_white(),
      LV_OPA_TRANSP, lv_color_black());

  set_wifi_icon_state(parts, connected);
  return parts;
}

void set_wifi_icon_state(const WifiIconParts& parts, bool connected) {
  set_style_opa_if_changed(parts.connected,
                           connected ? LV_OPA_COVER : LV_OPA_TRANSP);
  set_style_opa_if_changed(parts.disconnected,
                           connected ? LV_OPA_TRANSP : LV_OPA_COVER);
}

// LVGL's own row stride for an I1 canvas this wide - never recomputed as a
// bare (width+7)/8. That bare formula is what a prior version of this
// function used, and what LVGL's own stride agrees with only when the
// sdkconfig's row-padding value happens to be 1 (see
// LV_DRAW_BUF_STRIDE_ALIGN) and disagrees the moment it isn't: the panel
// showed progressive diagonal smearing, each row landing one alignment
// step further off than the last. Asking LVGL directly leaves nothing for
// a future alignment change to drift out of sync with.
int i1_canvas_stride(int width) {
  return static_cast<int>(
      lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_I1));
}

// LVGL's I1 palette lives in the *first*
// LV_COLOR_INDEXED_PALETTE_SIZE(I1) * sizeof(lv_color32_t) bytes of the
// very buffer passed to lv_canvas_set_buffer(), not in separate storage:
// lv_draw_buf_set_palette() writes each entry directly into
// `draw_buf->data`, and the software renderer's lv_draw_buf_goto_xy() skips
// exactly that many bytes before reading the first pixel. A buffer that
// starts pixel data at byte 0 instead renders nothing: both palette
// entries end up holding whatever the first pixel-writing call put there,
// typically indistinguishable, so index 0 and index 1 map to the same
// colour regardless of which bit was set. This is the minimum size a
// buffer passed to bind_i1_canvas() must be.
std::size_t i1_canvas_storage_bytes(int width, int height) {
  return static_cast<std::size_t>(i1_canvas_pixel_offset()) +
        static_cast<std::size_t>(i1_canvas_stride(width)) * height;
}

lv_obj_t* bind_i1_canvas(lv_obj_t* parent, int x, int y, int width, int height,
                        uint8_t* storage, lv_color_t background,
                        lv_opa_t background_opa, lv_color_t ink) {
  lv_obj_t* canvas = lv_canvas_create(parent);
  if (canvas == nullptr) return nullptr;
  apply_surface(canvas);
  // apply_surface() leaves every surface opaque white - correct for most
  // objects, but a canvas's own background is a style layer LVGL paints
  // *underneath* whatever the I1 palette's index 0 resolves to, not the
  // palette itself. A caller that asked for a transparent background (a
  // tray indicator sitting on something other than white) would otherwise
  // still get an opaque white square behind its "transparent" pixels -
  // this is the bug both callers happened not to trigger, not a case for
  // hardcoding transparency here instead: the battery overlay's opaque
  // white request is just as real a requirement as the tray's transparent
  // one.
  lv_obj_set_style_bg_color(canvas, background, 0);
  lv_obj_set_style_bg_opa(canvas, background_opa, 0);
  lv_canvas_set_buffer(canvas, storage, width, height, LV_COLOR_FORMAT_I1);
  lv_canvas_set_palette(canvas, 0, lv_color_to_32(background, background_opa));
  lv_canvas_set_palette(canvas, 1, lv_color_to_32(ink, LV_OPA_COVER));
  lv_obj_set_pos(canvas, x, y);
  lv_obj_set_size(canvas, width, height);
  return canvas;
}

namespace {

// i1_canvas_stride_bound()/i1_canvas_storage_bound() (ui_theme.hpp) are the
// compile-time mirrors of i1_canvas_stride()/i1_canvas_storage_bytes() that
// let the fixed backing stores below be *proven* sufficient at compile time
// instead of only ever being caught by their own runtime bounds checks - the
// same static_assert convention every other hardcoded size in this codebase
// already follows (kChargingBoltRows above, ui_strings.cpp's kRows,
// history.hpp's HistoryBlob). They started out private here; they are in the
// header now because render_dither_card.cpp needed the same answer and,
// lacking it, spelled the size `width * height` - the byte count for eight
// bits per pixel, so eight times too large.

// Generous fixed backing store for the charging overlay canvas - LVGL's
// I1-format canvas buffer must outlive the canvas object, so it cannot be a
// stack-local sized exactly to whatever Rect a given call receives (there is
// only ever one real caller, the tray's battery cell, but the function stays
// written in terms of `bounds` like the rest of this file rather than
// hardcoding that cell's numbers here). i1_canvas_pixel_offset() (8, for
// I1's 2 palette entries) plus 8 bytes/row (covers up to 64px wide) * 16
// rows is generous headroom over this cell's actual ~22x10 -
// i1_canvas_storage_bytes() computes the real, tighter bound at runtime,
// and every write into this buffer is bounds-checked against its actual
// sizeof() as the capacity, so this only needs to be *at least* big
// enough, not exact.
alignas(LV_DRAW_BUF_ALIGN) uint8_t
    g_charging_bolt_bitmap[i1_canvas_pixel_offset() + 8 * 16];

// Proves "generous headroom" above rather than asserting it in prose: the
// tray's battery cell (kTrayBatteryIconWidth x kTrayBatteryIconHeight,
// ui_data.hpp)
// is the one input battery_fill_rect() ever actually receives in this
// firmware, so its real charging-bolt canvas size is knowable at compile
// time, not just "~22x10". An icon cell grown past what this buffer holds
// now fails the build, rather than being silently rejected by
// build_battery_charging_composite()'s own bounds check at first boot.
static_assert(
    sizeof(g_charging_bolt_bitmap) >=
        i1_canvas_storage_bound(
            battery_fill_rect({0, 0, kTrayBatteryIconWidth, kTrayBatteryIconHeight})
                .width,
            battery_fill_rect({0, 0, kTrayBatteryIconWidth, kTrayBatteryIconHeight})
                .height),
    "g_charging_bolt_bitmap is too small for the tray battery cell's actual "
    "charging-overlay canvas - grow its fixed backing store");

}  // namespace

// Outline plus a terminal nub, with an inner bar whose width tracks the
// charge when not charging. While charging, the level bar is covered by an
// opaque overlay - a solid field with the bolt knocked out of it - rather
// than shown alongside the bolt: see build_battery_charging_composite()'s
// own comment for why every design that tried to show both a boundary-
// dependent level and the bolt in the same overlay was rejected on real
// hardware. An invalid reading leaves the body empty rather than drawing
// 0%, which would be a number nobody measured.
//
// The charging overlay is one canvas, opaque over the whole fill rect, its
// buffer built once, here, rather than recomputed on every level update:
// build_battery_charging_composite()'s output no longer depends on charge
// level at all, so unlike an earlier version of this function, there is
// nothing left for a level update to change about it - only whether it is
// shown, which set_battery_icon_level() still toggles.
//
// Both the fill bar and the overlay are positioned from
// battery_fill_rect(bounds) - the same call, not two copies of the same
// arithmetic - so the icon's outer footprint and the charging overlay's
// footprint can't drift apart.
BatteryIconParts battery_icon(lv_obj_t* parent, Rect bounds, uint8_t percent,
                              bool valid, bool charging) {
  BatteryIconParts parts{};
  const int body_width = bounds.width - kBatteryIconNubWidth - 1;
  lv_obj_t* body = lv_obj_create(parent);
  if (body != nullptr) {
    apply_surface(body);
    lv_obj_set_pos(body, bounds.x, bounds.y);
    lv_obj_set_size(body, body_width, bounds.height);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_border_color(body, lv_color_black(), 0);
    lv_obj_set_style_radius(body, 1, 0);
  }
  line_segment(parent, bounds.x + body_width + 1,
               bounds.y + bounds.height / 4, kBatteryIconNubWidth,
               bounds.height / 2, false);
  const Rect fill_rect = battery_fill_rect(bounds);
  parts.fill =
      line_segment(parent, fill_rect.x, fill_rect.y, 1, fill_rect.height, false);
  parts.body_width = body_width;

  // Written starting i1_canvas_pixel_offset() into the buffer, never at
  // byte 0, and at i1_canvas_stride()'s own row pitch, never a bare
  // (width+7)/8 - see both functions' own comments for why each of those
  // has already cost a debugging round on real hardware.
  build_battery_charging_composite(
      g_charging_bolt_bitmap + i1_canvas_pixel_offset(),
      sizeof(g_charging_bolt_bitmap) - i1_canvas_pixel_offset(),
      fill_rect.width, fill_rect.height, i1_canvas_stride(fill_rect.width));
  // Opaque over its whole area - index 0 (background) is solid white, not
  // transparent, because this canvas is a self-contained replacement for
  // everything build_battery_charging_composite() already decided, not a
  // partial layer relying on anything showing through it.
  parts.charging_bolt =
      bind_i1_canvas(parent, fill_rect.x, fill_rect.y, fill_rect.width,
                    fill_rect.height, g_charging_bolt_bitmap,
                    lv_color_white(), LV_OPA_COVER, lv_color_black());

  set_battery_icon_level(parts, percent, valid, charging);
  return parts;
}

void set_battery_icon_level(const BatteryIconParts& parts, uint8_t percent,
                            bool valid, bool charging) {
  if (parts.fill == nullptr) return;
  if (!valid) {
    set_style_bg_opa_if_changed(parts.fill, LV_OPA_TRANSP);
    set_style_opa_if_changed(parts.charging_bolt, LV_OPA_TRANSP);
    return;
  }
  set_style_bg_opa_if_changed(parts.fill, LV_OPA_COVER);
  const int usable = parts.body_width - 4;
  const int filled = usable * (percent > 100 ? 100 : percent) / 100;
  const int fill_width = filled < 1 ? 1 : filled;
  set_width_if_changed(parts.fill, fill_width);

  // The overlay's own content never changes (built once, in battery_icon())
  // - charging only ever toggles whether it is shown, the same one-line
  // pattern set_tray_indicator_icon_visible() already uses for a static
  // bitmap that also never changes after construction.
  set_style_opa_if_changed(parts.charging_bolt,
                           charging ? LV_OPA_COVER : LV_OPA_TRANSP);
}

// tray_indicator_icon() repacks a module's tight I1 bytes into LVGL's
// palette-and-stride backing storage once while creating its canvas. Core
// remains generic: it receives only the registered bitmap and never draws a
// module-specific shape.
namespace {

// One persistent LVGL backing buffer per registry slot, sized from the
// shared module contract rather than a particular module's icon.
alignas(LV_DRAW_BUF_ALIGN) uint8_t
    g_tray_indicator_storage[app_core::kMaxTrayIndicators]
                            [i1_canvas_storage_bound(app_core::kTrayIconSize,
                                                     app_core::kTrayIconSize)];

}  // namespace

TrayIndicatorIcon tray_indicator_icon(lv_obj_t* parent, Rect bounds, int slot,
                                      const app_core::TrayIndicatorBitmap& bitmap) {
  TrayIndicatorIcon icon{};
  if (bitmap.pixels == nullptr || bitmap.width == 0 || bitmap.height == 0 ||
      slot < 0 || slot >= app_core::kMaxTrayIndicators) {
    return icon;
  }
  uint8_t* storage = g_tray_indicator_storage[slot];
  // The module's own bytes are tight-packed ((width+7)/8/row, byte 0 is
  // pixel data) - app_core::TrayIndicatorBitmap's own documented contract,
  // and deliberately unaware of LVGL's own layout rules. Repacked here,
  // once per call, into what the canvas this function is about to bind
  // actually needs: see repack_i1_bits()'s own comment for why tight-packed
  // bits handed to LVGL directly render as nothing (palette clobbered) or a
  // diagonally smeared mess (wrong stride) - both of which have already
  // happened once each on this exact panel, for this exact reason, in
  // battery_icon()'s charging overlay.
  repack_i1_bits(bitmap.pixels, storage, sizeof(g_tray_indicator_storage[0]),
                bitmap.width, bitmap.height, i1_canvas_stride(bitmap.width),
                i1_canvas_pixel_offset());
  // Index 0 (bit clear) stays transparent so the tray's own background
  // shows through rather than a second, redundant white square; index 1
  // (bit set) is solid ink - unchanged from before this fix, only how the
  // buffer underneath it is laid out has changed.
  icon.canvas = bind_i1_canvas(parent, bounds.x, bounds.y, bitmap.width,
                              bitmap.height, storage, lv_color_white(),
                              LV_OPA_TRANSP, lv_color_black());
  return icon;
}

void set_tray_indicator_icon_visible(const TrayIndicatorIcon& icon, bool visible) {
  // This is the one that mattered: update_tray_indicators() calls it for every
  // slot on every ~100 ms tick, so before the guard it repainted the whole
  // panel ten times a second regardless of whether any indicator had moved.
  set_style_opa_if_changed(icon.canvas,
                           visible ? LV_OPA_COVER : LV_OPA_TRANSP);
}

void button_hints(lv_obj_t* parent, Rect bounds, InputHints hints) {
  if (!hints.visible) return;
  // KEY is the middle button and BOOT the right one (see the button table in
  // the board skill), so the hints run left to right in that order and the
  // pairing needs no explaining.
  const int half = bounds.width / 2;
  const std::string key = std::string("KEY ") + text(hints.key);
  const std::string boot = std::string(text(hints.boot)) + " BOOT";
  label(parent, key.c_str(), {bounds.x, bounds.y - 6, half, bounds.height + 6},
        &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
  label(parent, boot.c_str(),
        {bounds.x + half, bounds.y - 6, bounds.width - half, bounds.height + 6},
        &lv_font_montserrat_14, LV_TEXT_ALIGN_RIGHT);
}

void page_dots(lv_obj_t* parent, std::size_t page_index,
               std::size_t page_count, Rect bounds) {
  const PageDotsGeometry geometry =
      page_dots_geometry(bounds, page_index, page_count);
  for (std::size_t index = 0; index < geometry.count; ++index) {
    lv_obj_t* dot = lv_obj_create(parent);
    if (dot == nullptr) continue;
    apply_surface(dot);
    lv_obj_set_pos(dot,
                   geometry.start_x + static_cast<int>(
                                          index * (kPageDotSize + kPageDotGap)),
                   geometry.y);
    lv_obj_set_size(dot, kPageDotSize, kPageDotSize);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, index == geometry.active_index
                                      ? lv_color_black()
                                      : lv_color_white(),
                              0);
    lv_obj_set_style_border_width(dot, index == geometry.active_index ? 0 : 1,
                                  0);
    lv_obj_set_style_border_color(dot, lv_color_black(), 0);
  }
}

namespace {

struct OverlayTimerState {
  lv_obj_t* overlay = nullptr;
  lv_timer_t* timer = nullptr;
};

void overlay_deleted(lv_event_t* event) {
  auto* state =
      static_cast<OverlayTimerState*>(lv_event_get_user_data(event));
  if (state == nullptr) return;
  state->overlay = nullptr;
  if (state->timer != nullptr) {
    lv_timer_t* timer = state->timer;
    state->timer = nullptr;
    lv_timer_delete(timer);
  }
  delete state;
}

void delete_overlay_timer(lv_timer_t* timer) {
  auto* state =
      static_cast<OverlayTimerState*>(lv_timer_get_user_data(timer));
  if (state == nullptr) {
    lv_timer_delete(timer);
    return;
  }
  state->timer = nullptr;
  lv_obj_t* overlay = state->overlay;
  if (overlay != nullptr) lv_obj_delete(overlay);
  // overlay_deleted owns state lifetime and has already cleared the object
  // pointer. The timer itself is still valid until this callback returns.
  lv_timer_delete(timer);
}

}  // namespace

lv_obj_t* navigation_overlay(lv_obj_t* parent, Rect bounds) {
  lv_obj_t* overlay = lv_obj_create(parent);
  if (overlay == nullptr) return nullptr;
  apply_surface(overlay);
  lv_obj_set_pos(overlay, bounds.x, bounds.y);
  lv_obj_set_size(overlay, bounds.width, 24);
  lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
  lv_obj_set_style_text_color(overlay, lv_color_white(), 0);
  auto* state = new (std::nothrow) OverlayTimerState;
  if (state == nullptr) {
    lv_obj_delete(overlay);
    return nullptr;
  }
  state->overlay = overlay;
  lv_obj_set_user_data(overlay, state);
  lv_obj_add_event_cb(overlay, overlay_deleted, LV_EVENT_DELETE, state);
  lv_obj_t* overlay_text =
      label(overlay, "KEY  <   AUTO   >  BOOT", {0, 0, bounds.width, 24},
            font_small(), LV_TEXT_ALIGN_CENTER);
  if (overlay_text != nullptr) {
    lv_obj_set_style_bg_color(overlay_text, lv_color_black(), 0);
    lv_obj_set_style_text_color(overlay_text, lv_color_white(), 0);
    lv_obj_set_style_text_outline_stroke_color(overlay_text, lv_color_white(),
                                               0);
  }
  lv_timer_t* timer = lv_timer_create(delete_overlay_timer,
                                      kNavigationOverlayDurationMs, state);
  if (timer == nullptr) {
    lv_obj_delete(overlay);
    return nullptr;
  }
  state->timer = timer;
  lv_timer_set_repeat_count(timer, 1);
  return overlay;
}

}  // namespace ui
