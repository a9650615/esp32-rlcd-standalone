#include "ui_theme.hpp"

#include <algorithm>
#include <new>

namespace ui {
namespace {

lv_color_t ink(bool inverse) { return inverse ? lv_color_white() : lv_color_black(); }

}  // namespace

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

lv_obj_t* label(lv_obj_t* parent, const char* text, Rect bounds,
                const lv_font_t* font, lv_text_align_t align) {
  lv_obj_t* object = lv_label_create(parent);
  if (object == nullptr) return nullptr;
  apply_surface(object);
  lv_obj_set_pos(object, bounds.x, bounds.y);
  lv_obj_set_size(object, bounds.width, bounds.height);
  lv_label_set_text(object, text == nullptr ? "" : text);
  if (font != nullptr) lv_obj_set_style_text_font(object, font, 0);
  lv_obj_set_style_text_align(object, align, 0);
  lv_obj_set_style_text_line_space(object, 0, 0);
  lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
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

void weather_icon(lv_obj_t* parent, Rect bounds, bool rain, bool inverse) {
  // A compact cloud made from a baseline and two risers, with optional rain.
  const int mid_y = bounds.y + bounds.height / 2;
  line_segment(parent, bounds.x + 4, mid_y + 4, bounds.width - 8, 2, inverse);
  line_segment(parent, bounds.x + 9, mid_y - 3, 2, 9, inverse);
  line_segment(parent, bounds.x + bounds.width / 2, mid_y - 7, 2, 13, inverse);
  line_segment(parent, bounds.x + bounds.width - 11, mid_y - 1, 2, 7, inverse);
  if (rain) {
    line_segment(parent, bounds.x + 10, mid_y + 9, 1, 5, inverse);
    line_segment(parent, bounds.x + bounds.width / 2, mid_y + 9, 1, 5, inverse);
    line_segment(parent, bounds.x + bounds.width - 11, mid_y + 9, 1, 5,
                 inverse);
  }
}

void temperature_icon(lv_obj_t* parent, Rect bounds, bool inverse) {
  const int stem_x = bounds.x + bounds.width / 2;
  line_segment(parent, stem_x, bounds.y + 2, 2, bounds.height - 8, inverse);
  line_segment(parent, stem_x - 4, bounds.y + bounds.height - 8, 10, 2, inverse);
  line_segment(parent, stem_x - 4, bounds.y + 2, 2, 5, inverse);
  line_segment(parent, stem_x + 4, bounds.y + 2, 2, 5, inverse);
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

void page_dots(lv_obj_t* parent, uint8_t active_page, Rect bounds) {
  constexpr int kDotSize = 5;
  constexpr int kGap = 4;
  constexpr int kCount = 5;
  const int total_width = kCount * kDotSize + (kCount - 1) * kGap;
  const int start_x = bounds.x + bounds.width - total_width;
  const int y = bounds.y + bounds.height - kDotSize;
  for (int index = 0; index < kCount; ++index) {
    lv_obj_t* dot = lv_obj_create(parent);
    if (dot == nullptr) continue;
    apply_surface(dot);
    lv_obj_set_pos(dot, start_x + index * (kDotSize + kGap), y);
    lv_obj_set_size(dot, kDotSize, kDotSize);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, index == active_page ? lv_color_black()
                                                         : lv_color_white(),
                              0);
    lv_obj_set_style_border_width(dot, index == active_page ? 0 : 1, 0);
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
      label(overlay, "BOOT  ‹   AUTO   ›  KEY", {0, 0, bounds.width, 24},
            &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
  if (overlay_text != nullptr) {
    lv_obj_set_style_bg_color(overlay_text, lv_color_black(), 0);
    lv_obj_set_style_text_color(overlay_text, lv_color_white(), 0);
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
