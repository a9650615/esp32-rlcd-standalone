#include "net_log_ring.hpp"

#include <algorithm>
#include <cstring>

namespace net_log {

LineRing::LineRing(uint8_t* storage, std::size_t capacity_bytes)
    : storage_(storage), capacity_(capacity_bytes) {}

void LineRing::write_bytes(std::uint64_t at, const std::uint8_t* src,
                            std::size_t n) {
  const std::size_t phys = static_cast<std::size_t>(at % capacity_);
  const std::size_t first = std::min(n, capacity_ - phys);
  std::memcpy(storage_ + phys, src, first);
  if (n > first) std::memcpy(storage_, src + first, n - first);
}

void LineRing::read_bytes(std::uint64_t at, std::uint8_t* dst,
                           std::size_t n) const {
  const std::size_t phys = static_cast<std::size_t>(at % capacity_);
  const std::size_t first = std::min(n, capacity_ - phys);
  std::memcpy(dst, storage_ + phys, first);
  if (n > first) std::memcpy(dst + first, storage_ + first, n - first);
}

void LineRing::push(const char* data, std::size_t len) {
  // uint16 length prefix caps a single line at 65535 bytes - far more than
  // any ESP_LOG line - and the frame (prefix + payload) must fit in the
  // whole ring at all, or it can never be stored no matter what is evicted.
  const std::size_t frame_len = 2 + len;
  if (len > 0xFFFFu || frame_len > capacity_) {
    ++dropped_lines_;
    return;
  }
  while (buffered_ + frame_len > capacity_) {
    std::uint8_t header[2];
    read_bytes(total_written_ - buffered_, header, sizeof(header));
    const std::size_t oldest_frame =
        2 + (static_cast<std::size_t>(header[0]) |
             (static_cast<std::size_t>(header[1]) << 8));
    buffered_ -= oldest_frame;
    ++dropped_lines_;
  }
  const std::uint8_t header[2] = {
      static_cast<std::uint8_t>(len & 0xFFu),
      static_cast<std::uint8_t>((len >> 8) & 0xFFu)};
  write_bytes(total_written_, header, sizeof(header));
  write_bytes(total_written_ + 2, reinterpret_cast<const std::uint8_t*>(data),
              len);
  total_written_ += frame_len;
  buffered_ += frame_len;
}

bool LineRing::read_line(std::uint64_t& position, char* out, std::size_t cap,
                          std::size_t& out_len) const {
  const std::uint64_t oldest = total_written_ - buffered_;
  if (position < oldest) position = oldest;  // skip data since evicted
  if (position >= total_written_) return false;

  std::uint8_t header[2];
  read_bytes(position, header, sizeof(header));
  const std::size_t len = static_cast<std::size_t>(header[0]) |
                          (static_cast<std::size_t>(header[1]) << 8);
  const std::size_t copy_len = std::min(len, cap);
  read_bytes(position + 2, reinterpret_cast<std::uint8_t*>(out), copy_len);
  position += 2 + len;
  out_len = copy_len;
  return true;
}

}  // namespace net_log
