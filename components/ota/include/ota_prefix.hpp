#pragma once

#include <cstddef>
#include <cstdint>

#include "ota_image.hpp"

namespace ota {

// Accumulates the first kImagePrefixBytes of an upload so the image can be
// judged before esp_ota_begin erases a 3 MiB slot.
//
// A feeder does not get to choose its chunk sizes: an HTTP body arrives in
// whatever pieces the stack hands over, and the first one is routinely smaller
// than the header. The subtlety is that the bytes held back for inspection are
// still image bytes - they must reach flash in full once the verdict is in,
// and exactly once. Getting that wrong writes a corrupt image that passes
// every later check.
//
// Pure and allocation-free, so the boundary cases are host-testable.
class PrefixInspector {
 public:
  // Returns true once `verdict()` is meaningful.
  bool ready() const { return ready_; }
  ImageVerdict verdict() const { return info_.verdict; }
  const ImageInfo& info() const { return info_; }
  std::size_t buffered() const { return filled_; }

  // Feeds one chunk. Copies what it still needs; when the buffer completes,
  // runs the inspection and latches the verdict. Never re-inspects.
  void feed(const uint8_t* data, std::size_t length);

  // The held-back bytes, valid once ready(). The caller must write these to
  // flash before writing the remainder of the chunk that completed them.
  const uint8_t* buffer() const { return buffer_; }

  // Of `length` bytes in the chunk just fed, how many were consumed into the
  // buffer. The caller writes buffer() once, then only the bytes from this
  // offset onward, so nothing is written twice or dropped.
  std::size_t consumed_from_last_chunk() const { return consumed_; }

 private:
  uint8_t buffer_[kImagePrefixBytes]{};
  std::size_t filled_ = 0;
  std::size_t consumed_ = 0;
  bool ready_ = false;
  ImageInfo info_;
};

}  // namespace ota
