#pragma once

#include "ui_theme.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace ui {

struct ChartPoint {
  int x = 0;
  int y = 0;
};

struct MarketLayout {
  Rect primary;
  Rect side;
};

struct PageDotsGeometry {
  std::size_t count = 0;
  std::size_t active_index = 0;
  int start_x = 0;
  int total_width = 0;
  int y = 0;
};

inline constexpr int kPageDotSize = 5;
inline constexpr int kPageDotGap = 4;
inline constexpr char kComfortBandLabel[] = "COMFORT BAND  40-60 RH";

constexpr PageDotsGeometry page_dots_geometry(const Rect bounds,
                                              std::size_t page_index,
                                              std::size_t page_count) {
  if (page_count == 0) {
    return {0, 0, bounds.right(), 0, bounds.y + bounds.height - kPageDotSize};
  }
  const std::size_t active_index =
      page_index < page_count ? page_index : page_count - 1;
  const int total_width = static_cast<int>(
      page_count * kPageDotSize + (page_count - 1) * kPageDotGap);
  return {page_count, active_index, bounds.right() - total_width, total_width,
          bounds.y + bounds.height - kPageDotSize};
}

inline std::string format_minute_clock(std::string clock) {
  const std::size_t first = clock.find(':');
  if (first != std::string::npos) {
    const std::size_t second = clock.find(':', first + 1);
    if (second != std::string::npos) clock.resize(second);
  }
  return clock;
}

inline std::string compact_clock_source(const std::string& source) {
  if (source == "RTC fallback") return "DEMO / FALLBACK";
  if (source == "PCF85063") return "DEMO / RTC";
  return "DEMO / UNKNOWN";
}

constexpr MarketLayout market_layout(const Rect bounds) {
  constexpr int kPrimaryNumerator = 72;
  constexpr int kPercent = 100;
  const int primary_width =
      (bounds.width * kPrimaryNumerator) / kPercent;
  return {{bounds.x, bounds.y, primary_width, bounds.height},
          {bounds.x + primary_width + kSeparatorWidth, bounds.y,
           bounds.width - primary_width - kSeparatorWidth, bounds.height}};
}

constexpr std::array<Rect, 7> forecast_columns(const Rect bounds) {
  std::array<Rect, 7> columns{};
  const int column_width = bounds.width / 7;
  for (std::size_t index = 0; index < columns.size(); ++index) {
    const int x = bounds.x + static_cast<int>(index) * column_width;
    const int next_x = index + 1 == columns.size()
                           ? bounds.right()
                           : x + column_width;
    columns[index] = {x, bounds.y, next_x - x, bounds.height};
  }
  return columns;
}

constexpr std::array<ChartPoint, 8> normalize_chart_samples(
    const std::array<int, 8>& samples, const Rect bounds) {
  std::array<ChartPoint, 8> points{};
  int minimum = samples[0];
  int maximum = samples[0];
  for (const int sample : samples) {
    if (sample < minimum) minimum = sample;
    if (sample > maximum) maximum = sample;
  }
  const int range = maximum - minimum;
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const int x = bounds.x +
                  static_cast<int>((index * static_cast<std::size_t>(
                                        bounds.width - 1)) /
                                    (samples.size() - 1));
    int y = bounds.y + bounds.height / 2;
    if (range != 0) {
      const int from_top =
          ((maximum - samples[index]) * (bounds.height - 1)) / range;
      y = bounds.y + from_top;
    }
    if (y < bounds.y) y = bounds.y;
    if (y >= bounds.bottom()) y = bounds.bottom() - 1;
    points[index] = {x, y};
  }
  return points;
}

}  // namespace ui
