#include "ui_app.hpp"
#include "ui_fonts.hpp"

#include <algorithm>
#include <cstdio>
#include <new>

namespace ui {
namespace {

const lv_font_t* hero_font() { return font_hero(); }
const lv_font_t* medium_font() { return font_medium(); }
const lv_font_t* small_font() { return font_small(); }

void release_points(lv_event_t* event) {
  auto* points = static_cast<lv_point_precise_t*>(lv_event_get_user_data(event));
  delete[] points;
}

void mini_history(lv_obj_t* parent, const Rect bounds,
                  const std::array<double, 8>& history) {
  std::array<int, 8> samples{};
  for (std::size_t index = 0; index < history.size(); ++index) {
    samples[index] = static_cast<int>(history[index] * 10.0);
  }
  const auto normalized = normalize_chart_samples(samples, bounds);
  auto* points = new (std::nothrow) lv_point_precise_t[normalized.size()];
  if (points == nullptr) return;
  for (std::size_t index = 0; index < normalized.size(); ++index) {
    points[index] = {normalized[index].x - bounds.x,
                     normalized[index].y - bounds.y};
  }
  lv_obj_t* line = lv_line_create(parent);
  if (line == nullptr) {
    delete[] points;
    return;
  }
  apply_surface(line);
  lv_obj_set_pos(line, bounds.x, bounds.y);
  lv_obj_set_size(line, bounds.width, bounds.height);
  lv_obj_set_style_line_color(line, lv_color_black(), 0);
  lv_obj_set_style_line_width(line, kDataLineWidth, 0);
  lv_obj_set_style_line_rounded(line, false, 0);
  lv_line_set_points(line, points, normalized.size());
  lv_obj_add_event_cb(line, release_points, LV_EVENT_DELETE, points);
}

}  // namespace

void render_indoor(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                   Rect bounds, std::size_t page_index,
                   std::size_t page_count, UiContext* context) {
  (void)context;
  // Page position now lives in the system tray (see render_tray in
  // render_shared.cpp), not a corner overlay on the page itself.
  (void)page_index;
  (void)page_count;
  apply_surface(parent);
  const MarketLayout layout = market_layout(bounds);
  const Rect primary = layout.primary;
  label(parent, text(Text::TileIndoor), {primary.x + 8, primary.y + 5, 100, 18}, small_font());

  if (!snapshot.indoor.valid) {
    // No fabricated temperature/humidity/comfort band/history chart below
    // the title - see ui_data.hpp no_data_rect for the shared placeholder
    // geometry. A comfort band drawn at 0% or a flat all-zero history line
    // would both be fabricated numbers, so nothing below the title draws.
    label(parent, text(Text::NoData), no_data_rect(primary), medium_font(),
          LV_TEXT_ALIGN_CENTER);
  } else {
    char temperature[24];
    char humidity[24];
    std::snprintf(temperature, sizeof(temperature), "%.1f°", snapshot.indoor.temperature_c);
    std::snprintf(humidity, sizeof(humidity), "RH %u%%",
                  snapshot.indoor.humidity_percent);
    label(parent, temperature, {primary.x + 8, primary.y + 25, 190, 58},
          hero_font());
    label(parent, humidity, {primary.x + 211, primary.y + 44,
                             primary.width - 219, 28}, medium_font(),
          LV_TEXT_ALIGN_RIGHT);
    label(parent, text(Text::ComfortBand),
          {primary.x + 8, primary.y + 98, primary.width - 16, 18}, small_font());
    const int band_x = primary.x + 14;
    const int band_y = primary.y + 129;
    const int band_width = primary.width - 28;
    line_segment(parent, band_x, band_y, band_width, 2);
    line_segment(parent, band_x + band_width * 40 / 100, band_y - 5,
                 band_width * 20 / 100, 12);
    const int humidity_x = band_x + band_width *
                                       std::min(100, static_cast<int>(
                                                         snapshot.indoor.humidity_percent)) /
                                       100;
    line_segment(parent, humidity_x, band_y - 8, 2, 18);
    label(parent, text(Text::StatusDry), {band_x, band_y + 14, 42, 18}, small_font());
    label(parent, "OK", {band_x + band_width / 2 - 12, band_y + 14, 24, 18},
          small_font(), LV_TEXT_ALIGN_CENTER);
    label(parent, text(Text::StatusHumid), {band_x + band_width - 48, band_y + 14, 48, 18},
          small_font(), LV_TEXT_ALIGN_RIGHT);
    label(parent, text(Text::TileHistory), {primary.x + 8, primary.y + 174, 72, 18},
          small_font());
    mini_history(parent, {primary.x + 8, primary.y + 195, primary.width - 16, 35},
                 snapshot.indoor.temperature_history_c);
  }

  render_market_sidebar(parent, snapshot, snapshot.taiwan_market, layout.side,
                        false);
}

}  // namespace ui
