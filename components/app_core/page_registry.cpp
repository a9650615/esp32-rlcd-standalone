#include "page_registry.hpp"

#include <algorithm>

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

}  // namespace app_core
