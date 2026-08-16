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

// The radiating fan, not signal bars - bars read as cellular, and this is a
// wifi indicator.
//
// There is no arc primitive here and no diagonal, so the arcs are whole rings
// whose centres sit below the bottom of a clipping container: LVGL clips
// children to their parent unless LV_OBJ_FLAG_OVERFLOW_VISIBLE is set, so only
// each ring's upper half is drawn. Two rings and a dot give the standard mark.
//
// Returns the arcs so the caller can hide them in place. Rebuilding the page
// to change a connection indicator would repaint the whole panel, which is
// visible on a reflective display.
WifiIconParts wifi_icon(lv_obj_t* parent, Rect bounds, bool connected) {
  WifiIconParts parts{};
  const int dot = 4;
  // Everything above the dot is arc; the container's bottom edge is where the
  // rings are cut.
  const int arc_height = bounds.height - dot - 1;
  lv_obj_t* clip = lv_obj_create(parent);
  if (clip == nullptr) return parts;
  apply_surface(clip);
  lv_obj_set_pos(clip, bounds.x, bounds.y);
  lv_obj_set_size(clip, bounds.width, arc_height);

  // Concentric about the dot, which sits below the container - so what shows
  // is a shallow cap of each circle rather than a half of it. Centring them on
  // the container's own bottom edge gave exact semicircles, and a semicircle
  // is a dome: that is the hotspot idiom, not the wifi one.
  const int centre_x = bounds.width / 2;
  const int dot_centre_y = bounds.height - dot / 2 - 1;
  // Both diameters are chosen so the visible cap is genuinely curved. Going
  // much wider than the icon flattens the top of the circle into a straight
  // line across the container - measured, not guessed: at radius 16 with the
  // centre 11px down, the arc is already 23px wide by the time it reaches the
  // top edge of an 18px box.
  const int diameters[2] = {bounds.width, bounds.width * 3 / 5};
  for (int i = 0; i < 2; ++i) {
    lv_obj_t* ring = lv_obj_create(clip);
    if (ring == nullptr) continue;
    apply_surface(ring);
    const int size = diameters[i];
    lv_obj_set_pos(ring, centre_x - size / 2, dot_centre_y - size / 2);
    lv_obj_set_size(ring, size, size);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, lv_color_black(), 0);
    parts.bars[i] = ring;
  }
  // The dot is always drawn: an indicator that disappears entirely is
  // indistinguishable from one that failed to render.
  filled_circle(parent, bounds.x + centre_x, bounds.bottom() - dot / 2 - 1,
                dot, false);
  set_wifi_icon_state(parts, connected);
  return parts;
}

void set_wifi_icon_state(const WifiIconParts& parts, bool connected) {
  // Hidden rather than hollowed: these are already outlines, so there is no
  // emptier version of them to fall back to. Dot alone means no link.
  for (lv_obj_t* ring : parts.bars) {
    if (ring == nullptr) continue;
    lv_obj_set_style_border_opa(ring, connected ? LV_OPA_COVER : LV_OPA_TRANSP,
                                0);
  }
}

// Outline plus a terminal nub, with an inner bar whose width tracks the
// charge. An invalid reading leaves the body empty rather than drawing 0%,
// which would be a number nobody measured.
BatteryIconParts battery_icon(lv_obj_t* parent, Rect bounds, uint8_t percent,
                              bool valid) {
  BatteryIconParts parts{};
  const int nub_width = 3;
  const int body_width = bounds.width - nub_width - 1;
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
               bounds.y + bounds.height / 4, nub_width, bounds.height / 2,
               false);
  parts.fill = line_segment(parent, bounds.x + 2, bounds.y + 2, 1,
                            bounds.height - 4, false);
  parts.body_width = body_width;
  set_battery_icon_level(parts, percent, valid);
  return parts;
}

void set_battery_icon_level(const BatteryIconParts& parts, uint8_t percent,
                            bool valid) {
  if (parts.fill == nullptr) return;
  if (!valid) {
    lv_obj_set_style_bg_opa(parts.fill, LV_OPA_TRANSP, 0);
    return;
  }
  lv_obj_set_style_bg_opa(parts.fill, LV_OPA_COVER, 0);
  const int usable = parts.body_width - 4;
  const int filled = usable * (percent > 100 ? 100 : percent) / 100;
  lv_obj_set_width(parts.fill, filled < 1 ? 1 : filled);
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
