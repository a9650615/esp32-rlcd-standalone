#include "ui_app.hpp"

#include <cstdio>
#include <new>

namespace ui {
namespace {

const lv_font_t* small_font() { return &lv_font_montserrat_14; }
const lv_font_t* medium_font() { return &lv_font_montserrat_20; }

constexpr uint32_t kOwnerMagic = 0x5549524Fu;

struct RenderOwner {
  uint32_t magic = kOwnerMagic;
  lv_obj_t* root = nullptr;
};

void owner_host_deleted(lv_event_t* event) {
  auto* owner = static_cast<RenderOwner*>(lv_event_get_user_data(event));
  if (owner != nullptr && owner->magic == kOwnerMagic) {
    owner->root = nullptr;
    owner->magic = 0;
    lv_obj_set_user_data(static_cast<lv_obj_t*>(lv_event_get_current_target(event)),
                         nullptr);
    delete owner;
  }
}

RenderOwner* render_owner(lv_obj_t* host) {
  // The stable host reserves its LVGL user-data slot for this owner. Refuse
  // to guess if a caller already owns that slot rather than dereferencing an
  // unknown pointer or silently overwriting another subsystem's state.
  if (lv_obj_get_user_data(host) != nullptr) {
    auto* existing =
        static_cast<RenderOwner*>(lv_obj_get_user_data(host));
    return existing->magic == kOwnerMagic ? existing : nullptr;
  }
  auto* owner = new (std::nothrow) RenderOwner;
  if (owner == nullptr) return nullptr;
  lv_obj_set_user_data(host, owner);
  lv_obj_add_event_cb(host, owner_host_deleted, LV_EVENT_DELETE, owner);
  return owner;
}

#ifndef NDEBUG
void assert_tree_in_safe_canvas(lv_obj_t* object) {
  lv_obj_update_layout(object);
  lv_area_t area{};
  lv_obj_get_coords(object, &area);
  const Rect safe = safe_canvas();
  LV_ASSERT_MSG(area.x1 >= safe.x && area.y1 >= safe.y &&
                    area.x2 < safe.right() && area.y2 < safe.bottom(),
                "page object outside 6px safe canvas");
  const uint32_t child_count = lv_obj_get_child_count(object);
  for (uint32_t index = 0; index < child_count; ++index) {
    assert_tree_in_safe_canvas(lv_obj_get_child(object, index));
  }
}
#endif

void tile(lv_obj_t* parent, const char* title, const char* value,
          const char* detail, Rect bounds, bool weather, bool indoor) {
#ifndef NDEBUG
  LV_ASSERT_MSG(tile_content_is_centered(bounds),
                "right tile content is not vertically centered");
  LV_ASSERT_MSG(tile_content_has_no_reserved_footer(bounds),
                "right tile has a reserved footer band");
#endif
  const Rect content = tile_content_rect(bounds);
  label(parent, title, {bounds.x + 6, bounds.y + 8, bounds.width - 12, 18},
        small_font(), LV_TEXT_ALIGN_LEFT);
  if (weather) {
    weather_icon(parent, {bounds.x + 8, content.y + 2, 30, 30}, false);
  } else if (indoor) {
    temperature_icon(parent, {bounds.x + 8, content.y + 2, 30, 30});
  } else {
    line_segment(parent, bounds.x + 8, content.y + 31, bounds.width - 16, 1);
    line_segment(parent, bounds.x + 10, content.y + 28, bounds.width / 3, 1);
  }
  const int value_x = bounds.x + (weather || indoor ? 43 : 6);
  const int value_w = bounds.width - (weather || indoor ? 49 : 12);
  label(parent, value, {value_x, content.y, value_w, 28}, medium_font(),
        LV_TEXT_ALIGN_CENTER);
  label(parent, detail, {bounds.x + 6, content.y + 28,
                         bounds.width - 12, 16},
        small_font(), LV_TEXT_ALIGN_CENTER);
}

}  // namespace

void render_right_tiles(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                        Rect bounds) {
  const auto cells = right_tile_cells(bounds);
  const Rect& weather = cells[0];
  const Rect& indoor = cells[1];
  const Rect& market = cells[2];
  char weather_value[24];
  char indoor_value[24];
  char market_value[24];
  char weather_detail[24];
  char indoor_detail[24];
  char market_detail[24];
  std::snprintf(weather_value, sizeof(weather_value), "%.0f C",
                snapshot.weather.current.temperature_c);
  std::snprintf(weather_detail, sizeof(weather_detail), "%s %u%%",
                snapshot.weather.current.condition.c_str(),
                snapshot.weather.current.rain_probability_percent);
  std::snprintf(indoor_value, sizeof(indoor_value), "%.1f C",
                snapshot.indoor.temperature_c);
  std::snprintf(indoor_detail, sizeof(indoor_detail), "RH %u%%",
                snapshot.indoor.humidity_percent);
  std::snprintf(market_value, sizeof(market_value), "%d",
                snapshot.taiwan_market.primary_value);
  std::snprintf(market_detail, sizeof(market_detail), "%+.2f%%",
                snapshot.taiwan_market.primary_change_percent);
  tile(parent, "WEATHER", weather_value, weather_detail, weather, true, false);
  tile(parent, "INDOOR", indoor_value, indoor_detail, indoor, false, true);
  tile(parent, "MARKET", market_value, market_detail, market, false, false);
  divider(parent, {bounds.x, weather.bottom(), bounds.width, kSeparatorWidth});
  divider(parent, {bounds.x, indoor.bottom(), bounds.width, kSeparatorWidth});
}

void render_mast(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                 Rect bounds) {
  label(parent, "RLCD", {bounds.x, bounds.y, 42, 18}, small_font());
  label(parent, snapshot.clock.source.c_str(), {bounds.x + 48, bounds.y,
                                                  bounds.width - 48, 18},
        small_font(), LV_TEXT_ALIGN_RIGHT);
}

lv_obj_t* render_page(lv_obj_t* host, const app_core::AppSnapshot& snapshot,
                      app_core::PageId page, Rect bounds,
                      uint8_t active_page) {
  if (host == nullptr || !within_safe_canvas(bounds)) return nullptr;

  RenderOwner* owner = render_owner(host);
  if (owner == nullptr) return nullptr;

  // LVGL screens are parentless and therefore form a genuinely detached
  // staging surface. The page root is moved to the host only after rendering.
  lv_obj_t* staging_screen = lv_obj_create(nullptr);
  if (staging_screen == nullptr) return nullptr;
  lv_obj_set_size(staging_screen, kCanvasWidth, kCanvasHeight);
  lv_obj_t* replacement = lv_obj_create(staging_screen);
  if (replacement == nullptr) {
    lv_obj_delete(staging_screen);
    return nullptr;
  }
  lv_obj_add_flag(replacement, LV_OBJ_FLAG_HIDDEN);
  apply_surface(replacement);
  lv_obj_set_pos(replacement, bounds.x, bounds.y);
  lv_obj_set_size(replacement, bounds.width, bounds.height);

#ifndef NDEBUG
  LV_ASSERT_MSG(within_safe_canvas(bounds),
                "replacement root outside safe canvas");
#endif

  const Rect local_bounds{0, 0, bounds.width, bounds.height};

  // Task 5 owns the home shell. Other pages intentionally fall back to the
  // same shell until their dedicated renderers land in Task 6.
  switch (page) {
    case app_core::PageId::Home:
    default:
      render_home(replacement, snapshot, local_bounds, active_page);
      break;
  }
  lv_obj_set_parent(replacement, host);
  lv_obj_delete(staging_screen);
  if (owner->root != nullptr && owner->root != replacement) {
    lv_obj_delete(owner->root);
  }
  owner->root = replacement;
  lv_obj_clear_flag(replacement, LV_OBJ_FLAG_HIDDEN);
#ifndef NDEBUG
  assert_tree_in_safe_canvas(replacement);
#endif
  return replacement;
}

}  // namespace ui
