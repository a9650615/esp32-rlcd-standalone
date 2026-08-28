#include "page_registry.hpp"

#include <algorithm>
#include <string>

#include "media_registry.hpp"

namespace app_core {
namespace {

// Deliberately never destroyed. Pages register from static and early-init
// contexts, and a table that ran its own destructor at exit would be a
// use-after-free for anything still holding a descriptor - the same reason
// this codebase's other registries outlive main.
std::vector<PageDescriptor>& registration_table() {
  static std::vector<PageDescriptor>* table = [] {
    auto* fresh = new std::vector<PageDescriptor>();
    fresh->reserve(kMaxRegisteredPages);
    return fresh;
  }();
  return *table;
}

bool g_builtins_registered = false;

bool taiwan_available(const AppSnapshot& snapshot, PageKey) {
  return snapshot.availability.taiwan_market;
}
bool us_available(const AppSnapshot& snapshot, PageKey) {
  return snapshot.availability.us_market;
}
bool weather_available(const AppSnapshot& snapshot, PageKey) {
  return snapshot.availability.weather;
}
bool indoor_available(const AppSnapshot& snapshot, PageKey) {
  return snapshot.availability.indoor;
}

// Ignores the snapshot deliberately: this page's availability lives in the
// media registry, not in AppSnapshot, because a module - not a core provider
// task - is what fills it (modules/README.md rule 4). The PageDescriptor
// signature already permits this; no signature change is needed.
bool now_playing_available(const AppSnapshot&, PageKey) {
  return now_playing().session_open;
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

// Two cheap, honest signals for a market page: its own backing data is not
// valid, or it is a Taipei-local weekend (both the Taiwan and US markets are
// closed - this covers both without a holiday calendar the market provider
// deliberately declined to build; a national holiday still shows the previous
// session's close, which stays honest). The weekend signal only applies once
// snapshot.clock is a real, SNTP-synced local time - before sync, clock.date
// is a compile-time guess with no bearing on where the sun actually is.
bool taiwan_relevant(const AppSnapshot& snapshot, PageKey) {
  return snapshot.taiwan_market.valid && !clock_is_weekend(snapshot.clock);
}
bool us_relevant(const AppSnapshot& snapshot, PageKey) {
  return snapshot.us_market.valid && !clock_is_weekend(snapshot.clock);
}
bool weather_relevant(const AppSnapshot& snapshot, PageKey) {
  return snapshot.weather.valid;
}
bool indoor_relevant(const AppSnapshot& snapshot, PageKey) {
  return snapshot.indoor.valid;
}

const PageDescriptor* find_registration(PageKey key) {
  for (const PageDescriptor& descriptor : registration_table()) {
    if (descriptor.key == key) return &descriptor;
  }
  return nullptr;
}

}  // namespace

bool register_page(const PageDescriptor& descriptor) {
  std::vector<PageDescriptor>& table = registration_table();
  if (table.size() >= static_cast<std::size_t>(kMaxRegisteredPages)) return false;
  if (find_registration(descriptor.key) != nullptr) return false;
  table.push_back(descriptor);
  return true;
}

void register_builtin_pages() {
  if (g_builtins_registered) return;
  g_builtins_registered = true;

  // Home is the anchor and always present; everything else earns its place
  // through is_available. Now Playing goes first among the optional pages, so
  // a session that just started is the next thing rotation reaches.
  register_page({{PageId::Home, 0}, 30, kOrderHome, PagePriority::Normal,
                 nullptr, nullptr});
  register_page({{PageId::NowPlaying, 0}, 12, 10, PagePriority::Normal,
                 now_playing_available, nullptr});
  register_page({{PageId::TaiwanMarket, 0}, 12, 20, PagePriority::Normal,
                 taiwan_available, taiwan_relevant});
  register_page({{PageId::UsMarket, 0}, 12, 30, PagePriority::Normal,
                 us_available, us_relevant});
  register_page({{PageId::Weather, 0}, 12, 40, PagePriority::Normal,
                 weather_available, weather_relevant});
  register_page({{PageId::Indoor, 0}, 12, 50, PagePriority::Normal,
                 indoor_available, indoor_relevant});
}

void reset_page_registrations() {
  registration_table().clear();
  g_builtins_registered = false;
}

const std::vector<PageDescriptor>& registered_pages() {
  return registration_table();
}

void PageRegistry::begin_cycle(const AppSnapshot& snapshot) {
  descriptors_.clear();
  for (const PageDescriptor& descriptor : registration_table()) {
    if (descriptor.is_available == nullptr ||
        descriptor.is_available(snapshot, descriptor.key)) {
      descriptors_.push_back(descriptor);
    }
  }
  // Stable, so pages that share an order come out in registration order
  // rather than in whatever order the sort happened to produce.
  std::stable_sort(descriptors_.begin(), descriptors_.end(),
                   [](const PageDescriptor& left, const PageDescriptor& right) {
                     return left.order < right.order;
                   });
}

std::vector<PageKey> PageRegistry::page_keys() const {
  std::vector<PageKey> keys;
  keys.reserve(descriptors_.size());
  for (const PageDescriptor& descriptor : descriptors_) keys.push_back(descriptor.key);
  return keys;
}

bool page_relevant_for_auto_rotation(PageKey page, const AppSnapshot& snapshot) {
  const PageDescriptor* descriptor = find_registration(page);
  if (descriptor == nullptr || descriptor->is_relevant == nullptr) return true;
  return descriptor->is_relevant(snapshot, page);
}

std::size_t next_relevant_auto_index(const std::vector<PageKey>& pages,
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
