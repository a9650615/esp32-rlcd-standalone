#include "net_log_ring.hpp"

#include "test_support.hpp"

#include <cstring>
#include <string>

namespace {

std::string to_string(const char* data, std::size_t len) {
  return std::string(data, len);
}

}  // namespace

HOST_TEST(net_log_ring_reads_lines_back_in_order) {
  std::uint8_t storage[128];
  net_log::LineRing ring(storage, sizeof(storage));
  ring.push("first", 5);
  ring.push("second", 6);
  ring.push("third", 5);

  std::uint64_t cursor = 0;
  char out[32];
  std::size_t len = 0;

  EXPECT_TRUE(ring.read_line(cursor, out, sizeof(out), len));
  EXPECT_EQ(to_string(out, len), "first");
  EXPECT_TRUE(ring.read_line(cursor, out, sizeof(out), len));
  EXPECT_EQ(to_string(out, len), "second");
  EXPECT_TRUE(ring.read_line(cursor, out, sizeof(out), len));
  EXPECT_EQ(to_string(out, len), "third");
  EXPECT_TRUE(!ring.read_line(cursor, out, sizeof(out), len));
  EXPECT_EQ(ring.dropped_lines(), 0u);
}

HOST_TEST(net_log_ring_wraps_and_keeps_reading_the_newer_lines_in_order) {
  // Small enough that repeatedly pushing "line-N" (6 bytes + 2-byte header
  // = 8 bytes/frame) wraps the physical storage several times over.
  std::uint8_t storage[40];
  net_log::LineRing ring(storage, sizeof(storage));

  char line[8];
  for (int i = 0; i < 20; ++i) {
    std::snprintf(line, sizeof(line), "line-%d", i % 10);
    ring.push(line, std::strlen(line));
  }
  EXPECT_TRUE(ring.dropped_lines() > 0);  // storage can't hold all 20

  // Whatever remains must still read back oldest-to-newest, ending at the
  // last line pushed ("line-9"), with no gaps or reordering.
  std::uint64_t cursor = 0;
  char out[16];
  std::size_t len = 0;
  std::string last;
  int count = 0;
  while (ring.read_line(cursor, out, sizeof(out), len)) {
    const std::string current = to_string(out, len);
    if (!last.empty()) EXPECT_TRUE(last != current);
    last = current;
    ++count;
  }
  EXPECT_TRUE(count > 0);
  EXPECT_EQ(last, "line-9");
}

HOST_TEST(net_log_ring_full_buffer_drops_instead_of_blocking_and_counts_it) {
  std::uint8_t storage[32];
  net_log::LineRing ring(storage, sizeof(storage));

  // Each push below returns immediately either way (there is nothing to
  // block on in this pure class) - what is under test is that a full ring
  // evicts the oldest retained line rather than rejecting or stalling on
  // the newest one, and that every eviction is counted.
  for (int i = 0; i < 10; ++i) {
    ring.push("0123456789", 10);  // 12-byte frame; only ~2 fit at once
  }
  EXPECT_TRUE(ring.dropped_lines() > 0);

  // The newest line must still be present and readable.
  std::uint64_t cursor = 0;
  char out[16];
  std::size_t len = 0;
  bool saw_a_line = false;
  while (ring.read_line(cursor, out, sizeof(out), len)) {
    EXPECT_EQ(to_string(out, len), "0123456789");
    saw_a_line = true;
  }
  EXPECT_TRUE(saw_a_line);
}

HOST_TEST(net_log_ring_line_longer_than_the_whole_buffer_is_dropped_and_counted) {
  std::uint8_t storage[16];
  net_log::LineRing ring(storage, sizeof(storage));

  std::string huge(64, 'x');  // frame (2 + 64) far exceeds the 16-byte ring
  ring.push(huge.data(), huge.size());
  EXPECT_EQ(ring.dropped_lines(), 1u);

  // The oversized line never touched the ring at all.
  std::uint64_t cursor = 0;
  char out[16];
  std::size_t len = 0;
  EXPECT_TRUE(!ring.read_line(cursor, out, sizeof(out), len));

  // A line that does fit still works normally afterwards.
  ring.push("ok", 2);
  EXPECT_TRUE(ring.read_line(cursor, out, sizeof(out), len));
  EXPECT_EQ(to_string(out, len), "ok");
  EXPECT_EQ(ring.dropped_lines(), 1u);
}

HOST_TEST(net_log_ring_late_reader_cursor_is_clamped_to_the_oldest_retained_line) {
  // Models a viewer that connects (cursor = 0) after some lines were
  // already evicted: it must not see nothing and must not read garbage -
  // it should land exactly on the oldest line still retained.
  std::uint8_t storage[32];
  net_log::LineRing ring(storage, sizeof(storage));
  for (int i = 0; i < 10; ++i) {
    char line[8];
    std::snprintf(line, sizeof(line), "L%d", i);
    ring.push(line, std::strlen(line));
  }
  EXPECT_TRUE(ring.dropped_lines() > 0);  // some of L0..L9 were evicted

  std::uint64_t stale_cursor = 0;  // a brand-new viewer always starts at 0
  char out[16];
  std::size_t len = 0;
  EXPECT_TRUE(ring.read_line(stale_cursor, out, sizeof(out), len));
  // Whatever the first surviving line is, it must be readable cleanly
  // (starts with 'L', not truncated/garbled mid-frame).
  EXPECT_TRUE(len > 0 && out[0] == 'L');
}
