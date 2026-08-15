#pragma once

#include <cstddef>
#include <cstdint>

// Pure fixed-capacity ring buffer of length-framed log lines, no ESP
// dependency, so host tests can exercise wrap/drop/framing without a
// network or a device. net_log.cpp is the ESP-side owner: it allocates the
// backing storage (from PSRAM - see net_log.hpp) and wraps every call with
// a mutex, since this class itself is not thread-safe.
namespace net_log {

// Each stored line is framed as [uint16 length][bytes], so a reader always
// gets back whole lines in the order they were written, never a partial
// split across a push/read race or a wraparound.
class LineRing {
 public:
  // storage/capacity_bytes are owned by the caller and must outlive this
  // object. capacity_bytes must be at least a few bytes to hold anything.
  LineRing(uint8_t* storage, std::size_t capacity_bytes);

  // Appends one line, evicting the oldest retained line(s) to make room if
  // the ring is full. Never blocks, never allocates. If the line cannot
  // ever fit (longer than the whole ring), it is dropped instead and the
  // ring is left untouched. Every eviction and every too-long drop
  // increments dropped_lines().
  void push(const char* data, std::size_t len);

  // Total lines lost since construction: evicted-to-make-room plus
  // never-fit. This is the number to surface to an operator - a silently
  // truncated log is the exact failure mode this exists to prevent.
  std::uint32_t dropped_lines() const { return dropped_lines_; }

  // Reads the next retained line at/after `position` into out (cap bytes,
  // truncated if the line is longer - normal callers size cap the same as
  // every push, so this never triggers). Advances `position` past it.
  // `position` is an opaque cursor: start it at 0 to read the full current
  // backlog, since a position older than what is still retained is
  // clamped forward to the oldest retained line automatically. Returns
  // false (leaving position/out_len untouched) once nothing new remains.
  bool read_line(std::uint64_t& position, char* out, std::size_t cap,
                 std::size_t& out_len) const;

 private:
  void write_bytes(std::uint64_t at, const std::uint8_t* src, std::size_t n);
  void read_bytes(std::uint64_t at, std::uint8_t* dst, std::size_t n) const;

  std::uint8_t* storage_;
  std::size_t capacity_;
  std::uint64_t total_written_ = 0;  // logical bytes ever appended
  std::size_t buffered_ = 0;         // bytes currently retained
  std::uint32_t dropped_lines_ = 0;
};

}  // namespace net_log
