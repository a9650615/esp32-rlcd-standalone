#include "ui_app.hpp"

#include <algorithm>
#include <cstdio>

namespace ui {
namespace {

const lv_font_t* hero_font() { return &lv_font_montserrat_28; }
const lv_font_t* medium_font() { return &lv_font_montserrat_20; }
const lv_font_t* small_font() { return &lv_font_montserrat_14; }

bool rainy(const std::string& condition) {
  return condition == "Rain" || condition == "Storm";
}

}  // namespace

void render_weather(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                    Rect bounds, std::size_t page_index,
                    std::size_t page_count, UiContext* context) {
  apply_surface(parent);
  render_mast(parent, snapshot, {bounds.x, bounds.y, bounds.width, 28}, context);

  const Rect body{bounds.x, bounds.y + 28, bounds.width,
                  std::max(1, bounds.height - 28 - 8)};
  const auto& current = snapshot.weather.current;
  weather_icon(parent, {body.x + 8, body.y + 8, 34, 31}, rainy(current.condition));
  label(parent, current.condition.c_str(),
        {body.x + 52, body.y + 4, 170, 35}, hero_font());
  label(parent, current.location.c_str(),
        {body.x + 54, body.y + 41, 145, 20}, medium_font());
  char current_line[48];
  std::snprintf(current_line, sizeof(current_line), "%.1f C   RAIN %u%%",
                current.temperature_c, current.rain_probability_percent);
  label(parent, current_line, {body.x + 205, body.y + 42, body.width - 213, 20},
        small_font(), LV_TEXT_ALIGN_RIGHT);
  divider(parent, {body.x + 8, body.y + 70, body.width - 16, kSeparatorWidth});

  const Rect forecast{body.x + 8, body.y + 82, body.width - 16,
                      std::max(1, body.bottom() - body.y - 82)};
  const auto columns = forecast_columns(forecast);
  for (std::size_t index = 0; index < columns.size(); ++index) {
    const Rect column = columns[index];
    const auto& day = snapshot.weather.seven_day[index];
    label(parent, day.day.c_str(), {column.x, column.y, column.width, 18},
          small_font(), LV_TEXT_ALIGN_CENTER);
    weather_icon(parent, {column.x + (column.width - 26) / 2, column.y + 24,
                          26, 27},
                 rainy(day.condition));
    label(parent, day.condition.c_str(),
          {column.x + 1, column.y + 55, column.width - 2, 18}, small_font(),
          LV_TEXT_ALIGN_CENTER);
    char high_low[32];
    std::snprintf(high_low, sizeof(high_low), "H%.0f L%.0f", day.high_c,
                  day.low_c);
    label(parent, high_low,
          {column.x + 1, column.y + 77, column.width - 2, 18}, small_font(),
          LV_TEXT_ALIGN_CENTER);
    char rain[16];
    std::snprintf(rain, sizeof(rain), "R%u%%",
                  day.rain_probability_percent);
    label(parent, rain,
          {column.x + 1, column.y + 96, column.width - 2, 18}, small_font(),
          LV_TEXT_ALIGN_CENTER);
  }
  page_dots(parent, page_index, page_count, bounds);
}

}  // namespace ui
