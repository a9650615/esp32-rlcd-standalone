#pragma once

#include "app_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace app_core {

// One page in the carousel. `id` says what kind of page it is; `slot` says
// which one of that kind, and is 0 for every page there is only ever one of.
//
// Slots exist because the assistant's cards are several pages that share one
// renderer and one PageId, with nothing else to tell them apart: the carousel
// carries keys rather than ids precisely so that "card 2" and "card 3" are
// two different positions in rotation instead of the same one twice.
struct PageKey {
  PageId id = PageId::Home;
  uint8_t slot = 0;
};

inline bool operator==(PageKey left, PageKey right) {
  return left.id == right.id && left.slot == right.slot;
}
inline bool operator!=(PageKey left, PageKey right) { return !(left == right); }

// How loudly a page asks to be seen, as a ladder.
//
// Carried and reported, and deliberately not consulted by anything in this
// file: begin_cycle still orders purely by `order`, and
// next_relevant_auto_index still ignores priority entirely. The field is here
// first so a card can carry a priority end to end - host, wire, registry -
// before the rotation policy that acts on it exists, and so that policy can
// be written against real cards carrying real values rather than a guess
// about what an assistant would send.
//
// ponytail: inert field by request, wire it into next_relevant_auto_index
// once real cards have been observed asking for the screen.
enum class PagePriority : uint8_t {
  Background,  // fills rotation, never asks for it
  Normal,      // an ordinary data page
  Elevated,    // wants to be reached sooner than plain rotation would
  Urgent,      // wants the screen now
};

struct PageDescriptor {
  PageKey key;
  uint8_t dwell_seconds = 12;
  // Rotation order within a cycle, ascending. Registration order breaks ties,
  // so pages that do not care can all leave this at 0 and still come out in a
  // stable, predictable sequence.
  int16_t order = 0;
  PagePriority priority = PagePriority::Normal;
  // Is this page in rotation at all this cycle? nullptr means always. Takes
  // the whole key so one function can serve every slot of a multi-slot page -
  // "does the feed hold a card at this index" is one function, not six.
  bool (*is_available)(const AppSnapshot&, PageKey) = nullptr;
  // Is the page worth dwelling on unattended right now? nullptr means yes
  // whenever it is available. False never removes the page from rotation -
  // see page_relevant_for_auto_rotation.
  bool (*is_relevant)(const AppSnapshot&, PageKey) = nullptr;
};

// Registration ceiling: 6 built-in pages plus the assistant's 6 cards, with
// headroom for a couple more before this constant needs revisiting.
// register_page() enforces it rather than assuming callers register a sane
// number. A bound rather than a no-allocation claim - the table reserves once
// and never grows past this, but PageRegistry itself already carries a vector,
// so pretending this layer is allocation-free would be a comment nothing else
// in the file honours.
inline constexpr int kMaxRegisteredPages = 16;

// Home anchors every cycle, so its order sits below anything a caller would
// plausibly pick rather than relying on it registering first.
inline constexpr int16_t kOrderHome = -1000;

// Registers one page, normally once at startup. False when the table is full
// or when `key` is already registered; callers must check rather than assume
// registration always succeeds, the same as register_tray_indicator().
//
// There is no unregister, deliberately. A page that comes and goes - an
// assistant card that expired, a market that closed - says so through
// is_available, which begin_cycle already consults every cycle. Two ways to
// remove a page would be two answers to one question.
bool register_page(const PageDescriptor& descriptor);

// The built-in pages: Home, the two markets, weather, indoor, now-playing.
// Idempotent, so a second call adds nothing.
void register_builtin_pages();

// Drops every registration, built-ins included. Exists for host tests, which
// share one process and so share this table.
void reset_page_registrations();

// Read-only view of the registration table, in registration order.
const std::vector<PageDescriptor>& registered_pages();

class PageRegistry {
 public:
  void begin_cycle(const AppSnapshot& snapshot);

  const std::vector<PageDescriptor>& descriptors() const { return descriptors_; }
  std::vector<PageKey> page_keys() const;
  std::size_t size() const { return descriptors_.size(); }

 private:
  std::vector<PageDescriptor> descriptors_;
};

// True when a page has something worth the carousel dwelling on unattended
// right now. False never removes the page from rotation - it stays reachable
// by manual KEY/BOOT navigation and still renders its own NO DATA (or
// closed-market) placeholder when reached - this only steers automatic dwell
// time away from it.
//
// The answer comes from the page's own is_relevant callback, registered
// beside it, rather than from a switch here that has to grow a case every
// time a page is added. An unregistered key is relevant: a page the carousel
// is somehow showing without a registration is still a page in front of a
// person, and skipping it would be the more surprising failure.
bool page_relevant_for_auto_rotation(PageKey page, const AppSnapshot& snapshot);

// Never let automatic rotation land on nothing: starting at `from` and
// searching forward through `pages` (wrapping once), returns the index of the
// first page page_relevant_for_auto_rotation approves. If none qualify,
// `from` is returned unchanged, so the carousel still shows whatever page it
// already landed on rather than spinning forever looking for a relevant one.
std::size_t next_relevant_auto_index(const std::vector<PageKey>& pages,
                                     std::size_t from,
                                     const AppSnapshot& snapshot);

}  // namespace app_core
