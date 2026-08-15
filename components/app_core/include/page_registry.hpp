#pragma once

#include "app_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace app_core {

struct PageDescriptor {
  PageId id;
  uint8_t dwell_seconds;
  bool (*is_available)(const AppSnapshot&);
};

class PageRegistry {
 public:
  void begin_cycle(const AppSnapshot& snapshot);

  const std::vector<PageDescriptor>& descriptors() const { return descriptors_; }
  std::vector<PageId> page_ids() const;
  std::size_t size() const { return descriptors_.size(); }

 private:
  std::vector<PageDescriptor> descriptors_;
};

// True when a page has something worth the carousel dwelling on
// unattended right now. False never removes the page from rotation - it
// stays reachable by manual KEY/BOOT navigation and still renders its own
// NO DATA (or closed-market) placeholder when reached - this only steers
// automatic dwell time away from it. Two cheap, honest signals: the page's
// own backing data is not valid, or it is a market page on a Taipei-local
// weekend (both the Taiwan and US markets are closed - this covers both
// without a holiday calendar the market provider deliberately declined to
// build; a national holiday still shows the previous session's close, which
// stays honest). The weekend signal only applies once snapshot.clock is a
// real, SNTP-synced local time - before sync, clock.date is a compile-time
// guess with no bearing on where the sun actually is.
bool page_relevant_for_auto_rotation(PageId page, const AppSnapshot& snapshot);

// Never let automatic rotation land on nothing: starting at `from` and
// searching forward through `pages` (wrapping once), returns the index of
// the first page page_relevant_for_auto_rotation approves. If none qualify -
// every page currently in rotation is either invalid or a closed market -
// `from` is returned unchanged, so the carousel still shows whatever page it
// already landed on rather than spinning forever looking for a relevant one.
std::size_t next_relevant_auto_index(const std::vector<PageId>& pages,
                                     std::size_t from,
                                     const AppSnapshot& snapshot);

}  // namespace app_core
