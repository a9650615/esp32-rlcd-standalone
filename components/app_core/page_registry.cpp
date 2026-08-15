#include "page_registry.hpp"

#include <algorithm>
#include <string>

namespace app_core {
namespace {

bool always_available(const AppSnapshot&) { return true; }
bool taiwan_available(const AppSnapshot& snapshot) {
  return snapshot.availability.taiwan_market;
}
bool us_available(const AppSnapshot& snapshot) {
  return snapshot.availability.us_market;
}
bool weather_available(const AppSnapshot& snapshot) {
  return snapshot.availability.weather;
}
bool indoor_available(const AppSnapshot& snapshot) {
  return snapshot.availability.indoor;
}

const PageDescriptor kHome{PageId::Home, 30, always_available};
const PageDescriptor kTaiwan{PageId::TaiwanMarket, 12, taiwan_available};
const PageDescriptor kUs{PageId::UsMarket, 12, us_available};
const PageDescriptor kWeather{PageId::Weather, 12, weather_available};
const PageDescriptor kIndoor{PageId::Indoor, 12, indoor_available};

}  // namespace

void PageRegistry::begin_cycle(const AppSnapshot& snapshot) {
  descriptors_.clear();
  descriptors_.push_back(kHome);

  const PageDescriptor* ordered[] = {&kTaiwan, &kUs, &kWeather, &kIndoor};
  switch (snapshot.scenario) {
    case DemoScenario::MorningAlert:
      ordered[0] = &kWeather;
      ordered[1] = &kTaiwan;
      ordered[2] = &kUs;
      ordered[3] = &kIndoor;
      break;
    case DemoScenario::TaiwanSession:
      break;
    case DemoScenario::NightSession:
      ordered[0] = &kUs;
      ordered[1] = &kWeather;
      ordered[2] = &kTaiwan;
      ordered[3] = &kIndoor;
      break;
  }

  for (const PageDescriptor* descriptor : ordered) {
    if (descriptor->is_available(snapshot)) descriptors_.push_back(*descriptor);
  }
}

std::vector<PageId> PageRegistry::page_ids() const {
  std::vector<PageId> ids;
  ids.reserve(descriptors_.size());
  for (const PageDescriptor& descriptor : descriptors_) ids.push_back(descriptor.id);
  return ids;
}

namespace {

bool page_data_valid(PageId page, const AppSnapshot& snapshot) {
  switch (page) {
    case PageId::TaiwanMarket: return snapshot.taiwan_market.valid;
    case PageId::UsMarket: return snapshot.us_market.valid;
    case PageId::Weather: return snapshot.weather.valid;
    case PageId::Indoor: return snapshot.indoor.valid;
    case PageId::Home:
    case PageId::Setup:
    case PageId::Settings:
    case PageId::Ota:
      return true;
  }
  return true;
}

bool is_market_page(PageId page) {
  return page == PageId::TaiwanMarket || page == PageId::UsMarket;
}

// ClockData::date is formatted as "Sun, 16 Aug 2026" (see ui_app.cpp
// weekday_name and the mock fixture), so the weekday abbreviation is always
// its first three characters - no separate date parsing needed.
bool clock_is_weekend(const ClockData& clock) {
  if (clock.source != "SNTP") return false;
  if (clock.date.size() < 3) return false;
  const std::string weekday = clock.date.substr(0, 3);
  return weekday == "Sat" || weekday == "Sun";
}

}  // namespace

bool page_relevant_for_auto_rotation(PageId page, const AppSnapshot& snapshot) {
  if (!page_data_valid(page, snapshot)) return false;
  if (is_market_page(page) && clock_is_weekend(snapshot.clock)) return false;
  return true;
}

std::size_t next_relevant_auto_index(const std::vector<PageId>& pages,
                                     std::size_t from,
                                     const AppSnapshot& snapshot) {
  if (pages.empty()) return from;
  const std::size_t start = from % pages.size();
  for (std::size_t step = 0; step < pages.size(); ++step) {
    const std::size_t index = (start + step) % pages.size();
    if (page_relevant_for_auto_rotation(pages[index], snapshot)) return index;
  }
  return from;
}

}  // namespace app_core
