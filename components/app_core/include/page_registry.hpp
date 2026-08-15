#pragma once

#include "app_snapshot.hpp"

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

  // Snapshot changes are deliberately ignored until the next cycle boundary.
  void observe(const AppSnapshot& snapshot);

  const std::vector<PageDescriptor>& descriptors() const { return descriptors_; }
  std::vector<PageId> page_ids() const;

 private:
  std::vector<PageDescriptor> descriptors_;
};

}  // namespace app_core
