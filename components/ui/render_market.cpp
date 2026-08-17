#include "ui_app.hpp"
#include "ui_fonts.hpp"

#include <algorithm>
#include <cstdio>
#include <new>

namespace ui {
namespace {

const lv_font_t* large_font() { return font_large(); }
const lv_font_t* medium_font() { return font_medium(); }
const lv_font_t* small_font() { return font_small(); }

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

// `count` - not source.size() - is how many of `source` are real: since
// app_snapshot.hpp's MarketData::intraday_sample_count can be smaller than
// the array's own app_core::kIntradaySampleCount (early in a session,
// before enough real bars exist to fill it), normalize_chart_samples_n
// leaves every slot past `count` zero-valued rather than a lie about
// where those points belong - drawing all of source unconditionally would
// draw a line down to (and back from) the coordinate origin.
void polyline(lv_obj_t* parent,
              const std::array<ChartPoint, app_core::kIntradaySampleCount>& source,
              std::size_t count, const Rect chart) {
  auto* points = new (std::nothrow) lv_point_precise_t[count];
  if (points == nullptr) return;
  for (std::size_t index = 0; index < count; ++index) {
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
  lv_line_set_points(line, points, count);
  lv_obj_add_event_cb(line, release_chart_points, LV_EVENT_DELETE, points);
}

}  // namespace

void render_market(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                   const app_core::MarketData& market, Rect bounds,
                   std::size_t page_index, std::size_t page_count,
                   bool us_market, UiContext* context) {
  (void)context;
  // Page position now lives in the system tray (see render_tray in
  // render_shared.cpp), not a corner overlay on the page itself.
  (void)page_index;
  (void)page_count;
  apply_surface(parent);
  const MarketLayout layout = market_layout(bounds);
  const Rect primary = layout.primary;

  // The title and the date split the row. They used to share one rect, and
  // because every label paints an opaque background the date simply covered
  // the title - the exact failure the board skill warns against fixing with
  // paint order.
  const int header_width = primary.width - 16;
  const Rect title_rect{primary.x + 8, primary.y + 4, header_width * 3 / 5, 18};
  const Rect date_rect{title_rect.right(), primary.y + 4,
                       header_width - title_rect.width, 18};
  label(parent, us_market ? text(Text::TitleUsMarket) : text(Text::TitleTaiwanMarket),
        title_rect, small_font());

  // The session these figures come from, on every market page and in every
  // state. The US page has an intraday series and so draws a chart, which
  // implies "now" more strongly than a bare number does - and outside trading
  // hours that chart is last session's. Dating it is the difference between a
  // stale reading and a lie.
  //
  // Shown unconditionally rather than only when the date differs from today's:
  // deciding that needs the device's date against the exchange's, in different
  // timezones, and a wrong answer there fails silently in the direction of
  // saying nothing. One short line costs less than the comparison.
  const std::string as_of = market_as_of_short(market);
  if (market.valid && !as_of.empty()) {
    label(parent, as_of.c_str(), date_rect, small_font(),
          LV_TEXT_ALIGN_RIGHT);
  }

  if (!market.valid) {
    // No fabricated primary_value/percent/chart below the title - see
    // ui_data.hpp no_data_rect for the shared placeholder geometry.
    label(parent, text(Text::NoData), no_data_rect(primary), medium_font(),
          LV_TEXT_ALIGN_CENTER);
  } else {
    // The axis labels below the chart get whatever label() will actually
    // give them, not what we ask for: a 17 px box grows to
    // font.line_height + 2 and the extra row used to land one pixel past the
    // safe canvas. Reserve the real height here so the chart yields the row
    // instead.
    const int axis_height = safe_text_box_height(17, small_font()->line_height);
    const Rect chart = market_chart_rect(primary, small_font()->line_height);

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

    if (market.has_intraday) {
      // The grid and the 開盤/盤中/收盤 labels below stay at the chart's
      // full width regardless - they are the session's time axis, not a
      // claim about how much of it the samples cover. Only the polyline
      // itself is narrowed:
      //
      // - Width, by market.session_elapsed_fraction: a session still in
      //   progress only ever has samples up through "now", and stretching
      //   those across the full axis would claim a finished trading day
      //   that has not happened yet (see app_snapshot.hpp's own comment on
      //   that field). A completed session, and the TWSE fallback, never
      //   see anything but the field's 1.0 default - full width, exactly
      //   as before.
      // - Point count, via normalize_chart_samples_n's own `count`
      //   parameter and market.intraday_sample_count: early in a session
      //   there may be fewer real bars than the array's target resolution
      //   (app_core::kIntradaySampleCount), and the unused trailing slots
      //   must not be read as real, zero-valued data.
      const float fraction =
          std::clamp(market.session_elapsed_fraction, 0.0f, 1.0f);
      const Rect data_extent{
          chart.x, chart.y,
          std::max(1, static_cast<int>(chart.width * fraction)),
          chart.height};
      dotted_grid(parent, chart);
      polyline(parent,
              normalize_chart_samples_n(market.intraday_samples, data_extent,
                                        market.intraday_sample_count),
              market.intraday_sample_count, data_extent);
      label(parent, text(Text::ChartOpen),
            {chart.x, chart.bottom() + 1, chart.width / 3, axis_height},
            small_font());
      label(parent, text(Text::ChartMid),
            {chart.x + chart.width / 3, chart.bottom() + 1, chart.width / 3,
             axis_height},
            small_font(), LV_TEXT_ALIGN_CENTER);
      label(parent, text(Text::ChartClose),
            {chart.x + 2 * chart.width / 3, chart.bottom() + 1,
             chart.width / 3, axis_height},
            small_font(), LV_TEXT_ALIGN_RIGHT);
    } else {
      // A daily-close-only source (e.g. TWSE) has no intraday series - the
      // figures above are real, but drawing a flat repeat of the close would
      // read as "the market did not move" rather than "no intraday data
      // exists". Skip the chart, grid, and axis labels; say so instead.
      // Naming the session is the point. A closed market is the normal
      // weekend state, and a page showing Thursday's close with no date
      // invites reading it as today's - the figures are real either way, but
      // only one of those is honest about when.
      const Rect placeholder = chart_placeholder_rect(chart);
      const std::string as_of = market_as_of_text(market);
      if (as_of.empty()) {
        label(parent, text(Text::NoIntradayData), placeholder, small_font(),
              LV_TEXT_ALIGN_CENTER);
      } else {
        label(parent, as_of.c_str(),
              {placeholder.x, placeholder.y - 12, placeholder.width, 30},
              medium_font(), LV_TEXT_ALIGN_CENTER);
        label(parent, text(Text::NoIntradayData),
              {placeholder.x, placeholder.y + 20, placeholder.width,
               placeholder.height - 20},
              small_font(), LV_TEXT_ALIGN_CENTER);
      }
    }
  }

  render_market_sidebar(parent, snapshot, market, layout.side, us_market);
}

}  // namespace ui
