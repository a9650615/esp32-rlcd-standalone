#include "ota_prefix.hpp"

#include <cstring>

namespace ota {

void PrefixInspector::feed(const uint8_t* data, std::size_t length) {
  consumed_ = 0;
  // Latched: once judged, later chunks pass straight through. Re-running the
  // inspection on a chunk that happens to start with 0xE9 would be a way to
  // launder a bad verdict halfway through an upload.
  if (ready_ || data == nullptr || length == 0) return;

  const std::size_t wanted = kImagePrefixBytes - filled_;
  consumed_ = length < wanted ? length : wanted;
  std::memcpy(buffer_ + filled_, data, consumed_);
  filled_ += consumed_;

  if (filled_ < kImagePrefixBytes) return;
  info_ = inspect_image_prefix(buffer_, filled_);
  ready_ = true;
}

}  // namespace ota
