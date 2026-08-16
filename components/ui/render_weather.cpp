#include "ui_app.hpp"
#include "ui_fonts.hpp"

#include <cstdio>

namespace ui {
namespace {

const lv_font_t* hero_font() { return font_large(); }
const lv_font_t* medium_font() { return font_medium(); }
const lv_font_t* small_font() { return font_small(); }

}  // namespace

void render_weather(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                    Rect bounds, std::size_t page_index,
                    std::size_t page_count, UiContext* context) {
  (void)context;
  // Page position now lives in the system tray (see render_tray in
  // render_shared.cpp), not a corner overlay on the page itself.
  (void)page_index;
  (void)page_count;
  apply_surface(parent);
  const auto& weather = snapshot.weather;

  if (!weather.valid) {
    // No fabricated condition/temperature/rain% or seven-day forecast - see
    // ui_data.hpp no_data_rect for the shared placeholder geometry. Weather
    // has no other title text (the condition line normally serves that
    // role), so give it the same small title row the market/indoor pages
    // use.
    label(parent, text(Text::TileWeather), {bounds.x + 8, bounds.y + 4, bounds.width - 16, 18},
          small_font());
    label(parent, text(Text::NoData), no_data_rect(bounds), medium_font(),
          LV_TEXT_ALIGN_CENTER);
    return;
  }

  const auto& current = weather.current;
  // Grown from the original 34x31 - too small and too fine to read on this
  // panel. Still clears the divider at bounds.y+70 with room to spare.
  weather_icon(parent, {bounds.x + 8, bounds.y + 8, 48, 46},
              weather_icon_kind_for_condition(current.condition));
  label(parent, current.condition.c_str(),
        {bounds.x + 66, bounds.y + 4, 156, 35}, hero_font());
  label(parent, current.location.c_str(),
        {bounds.x + 68, bounds.y + 41, 131, 20}, medium_font());
  char current_line[48];
  std::snprintf(current_line, sizeof(current_line), "%.1f C   RAIN %u%%%s",
                current.temperature_c, current.rain_probability_percent,
                weather.stale ? text(Text::StaleSuffix) : "");
  label(parent, current_line,
        {bounds.x + 205, bounds.y + 42, bounds.width - 213, 20}, small_font(),
        LV_TEXT_ALIGN_RIGHT);
  divider(parent, {bounds.x + 8, bounds.y + 70, bounds.width - 16,
                   kSeparatorWidth});

  const Rect forecast = weather_forecast_rect(bounds);
  const auto columns = forecast_columns(forecast);
  for (std::size_t index = 0; index < columns.size(); ++index) {
    const Rect column = columns[index];
    const auto& day = snapshot.weather.seven_day[index];
    const ForecastColumnLayout layout = forecast_column_layout(column);
    label(parent, day.day.c_str(), layout.day, small_font(),
          LV_TEXT_ALIGN_CENTER);
    weather_icon(parent, layout.icon,
                weather_icon_kind_for_condition(day.condition));
    label(parent, forecast_condition_short(day.condition),
          {layout.condition.x + 1, layout.condition.y,
           layout.condition.width - 2, layout.condition.height},
          small_font(), LV_TEXT_ALIGN_CENTER);
    // High above low - a "H68 L54" side-by-side pair was too cramped for
    // the ~55px column width seven days across the safe canvas leaves.
    char high[16];
    std::snprintf(high, sizeof(high), "H%.0f", day.high_c);
    label(parent, high,
          {layout.high.x + 1, layout.high.y, layout.high.width - 2,
           layout.high.height},
          small_font(), LV_TEXT_ALIGN_CENTER);
    char low[16];
    std::snprintf(low, sizeof(low), "L%.0f", day.low_c);
    label(parent, low,
          {layout.low.x + 1, layout.low.y, layout.low.width - 2,
           layout.low.height},
          small_font(), LV_TEXT_ALIGN_CENTER);
    char rain[16];
    std::snprintf(rain, sizeof(rain), "R%u%%",
                  day.rain_probability_percent);
    label(parent, rain,
          {layout.rain.x + 1, layout.rain.y, layout.rain.width - 2,
           layout.rain.height},
          small_font(), LV_TEXT_ALIGN_CENTER);
  }
}

}  // namespace ui
