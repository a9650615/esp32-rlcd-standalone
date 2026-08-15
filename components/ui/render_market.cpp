#include "ui_app.hpp"

#include <algorithm>
#include <cstdio>
#include <new>

namespace ui {
namespace {

const lv_font_t* large_font() { return &lv_font_montserrat_28; }
const lv_font_t* medium_font() { return &lv_font_montserrat_20; }
const lv_font_t* small_font() { return &lv_font_montserrat_14; }

void release_chart_points(lv_event_t* event) {
  auto* points = static_cast<lv_point_precise_t*>(lv_event_get_user_data(event));
  delete[] points;
}

void dotted_grid(lv_obj_t* parent, const Rect chart) {
  for (int row = 1; row <= 3; ++row) {
    const int y = chart.y + (chart.height * row) / 4;
    for (int x = chart.x + 1; x < chart.right() - 1; x += 9) {
      line_segment(parent, x, y, std::min(4, chart.right() - x - 1), 1);
    }
  }
}

void polyline(lv_obj_t* parent, const std::array<ChartPoint, 8>& source,
              const Rect chart) {
  auto* points = new (std::nothrow) lv_point_precise_t[source.size()];
  if (points == nullptr) return;
  for (std::size_t index = 0; index < source.size(); ++index) {
    points[index] = {source[index].x - chart.x, source[index].y - chart.y};
  }
  lv_obj_t* line = lv_line_create(parent);
  if (line == nullptr) {
    delete[] points;
    return;
  }
  apply_surface(line);
  lv_obj_set_pos(line, chart.x, chart.y);
  lv_obj_set_size(line, chart.width, chart.height);
  lv_obj_set_style_line_color(line, lv_color_black(), 0);
  lv_obj_set_style_line_width(line, kDataLineWidth, 0);
  lv_obj_set_style_line_rounded(line, false, 0);
  lv_line_set_points(line, points, source.size());
  lv_obj_add_event_cb(line, release_chart_points, LV_EVENT_DELETE, points);
}

}  // namespace

void render_market(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                   const app_core::MarketData& market, Rect bounds,
                   std::size_t page_index, std::size_t page_count,
                   bool us_market, UiContext* context) {
  (void)context;
  apply_surface(parent);
  const MarketLayout layout = market_layout(bounds);
  const Rect primary = layout.primary;
  const Rect chart{primary.x + 8, primary.y + 70, primary.width - 16,
                   std::max(1, primary.bottom() - primary.y - 88)};

  label(parent, us_market ? "US MARKET" : "TAIWAN MARKET",
        {primary.x + 8, primary.y + 4, primary.width - 16, 18}, small_font());
  label(parent, market.primary_label.c_str(),
        {primary.x + 8, primary.y + 23, primary.width / 2 - 8, 30},
        medium_font());
  char value[24];
  char change[24];
  std::snprintf(value, sizeof(value), "%d", market.primary_value);
  std::snprintf(change, sizeof(change), "%+.2f%%",
                market.primary_change_percent);
  label(parent, value,
        {primary.x + primary.width / 2, primary.y + 20,
         primary.width / 2 - 8, 32},
        large_font(), LV_TEXT_ALIGN_RIGHT);
  label(parent, change,
        {primary.x + primary.width / 2, primary.y + 52,
         primary.width / 2 - 8, 18},
        small_font(), LV_TEXT_ALIGN_RIGHT);
  divider(parent, {primary.x + 8, primary.y + 66, primary.width - 16,
                   kSeparatorWidth});

  dotted_grid(parent, chart);
  polyline(parent, normalize_chart_samples(market.intraday_samples, chart),
           chart);
  label(parent, "09:00", {chart.x, chart.bottom() + 1, chart.width / 3, 17},
        small_font());
  label(parent, "MID",
        {chart.x + chart.width / 3, chart.bottom() + 1, chart.width / 3, 17},
        small_font(), LV_TEXT_ALIGN_CENTER);
  label(parent, "NOW",
        {chart.x + 2 * chart.width / 3, chart.bottom() + 1,
         chart.width / 3, 17},
        small_font(), LV_TEXT_ALIGN_RIGHT);

  render_market_sidebar(parent, snapshot, market, layout.side, us_market);
  page_dots(parent, page_index, page_count, bounds);
}

}  // namespace ui
