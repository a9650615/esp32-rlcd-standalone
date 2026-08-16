#define UI_THEME_GEOMETRY_ONLY
#include "app_snapshot.hpp"
#include "tray_registry.hpp"
#include "ui_data.hpp"

#include "test_support.hpp"

#include <array>

namespace {

// A single registered indicator's cells, in slot-priority order (matches
// TrayIndicators::entries' index order): time/network/battery are always
// present; indicators[i] only when entries[i] is active and it fits.
ui::TrayIndicators one_active(int width) { return ui::one_indicator(width); }

// Several slots active at once, at the given width each - the shape a real
// render builds by reading every app_core registry slot (see render_tray()
// in render_shared.cpp), reduced here to plain data so the layout function
// can be exercised without the registry's atomics.
ui::TrayIndicators n_active(int count, int width) {
  ui::TrayIndicators indicators{};
  for (int i = 0; i < count && i < app_core::kMaxTrayIndicators; ++i) {
    indicators.entries[i] = {true, width};
  }
  return indicators;
}

}  // namespace

// The full combination space for a single indicator is small - two page
// choices (does the leading cell go wide, i.e. Home vs. not) x two
// visibility choices - so it is enumerated exhaustively here rather than
// sampled, per the layout function's own contract: nothing overlaps,
// nothing sits outside the tray bounds, and no cell that is supposed to be
// present has zero or negative width.
HOST_TEST(tray_layout_all_combinations_are_valid) {
  const ui::Rect bounds = ui::safe_canvas();
  const std::array<app_core::PageId, 2> pages = {app_core::PageId::Home,
                                                 app_core::PageId::Weather};
  const std::array<bool, 2> indicator_states = {false, true};

  for (const app_core::PageId page : pages) {
    for (const bool active : indicator_states) {
      const ui::SystemTrayLayout cells = ui::system_tray_layout(
          bounds, page, active ? one_active(20) : ui::TrayIndicators{});
      const ui::Rect indicator = cells.indicators[0];
      const bool indicator_present = indicator.width > 0;

      // Always-present cells never collapse to nothing.
      EXPECT_TRUE(cells.time.width > 0);
      EXPECT_TRUE(cells.network.width > 0);
      EXPECT_TRUE(cells.battery.width > 0);

      // Everything present stays inside the tray's own bounds.
      EXPECT_TRUE(ui::rect_within(bounds, cells.time));
      EXPECT_TRUE(ui::rect_within(bounds, cells.network));
      EXPECT_TRUE(ui::rect_within(bounds, cells.battery));
      if (indicator_present) {
        EXPECT_TRUE(ui::rect_within(bounds, indicator));
      } else {
        // Not present means not present: no space reserved, not just an
        // empty-looking cell sitting somewhere.
        EXPECT_EQ(indicator.width, 0);
      }

      // No two present cells overlap, for every pair.
      EXPECT_TRUE(!ui::rects_intersect(cells.time, cells.network));
      EXPECT_TRUE(!ui::rects_intersect(cells.time, cells.battery));
      EXPECT_TRUE(!ui::rects_intersect(cells.network, cells.battery));
      if (indicator_present) {
        EXPECT_TRUE(!ui::rects_intersect(indicator, cells.time));
        EXPECT_TRUE(!ui::rects_intersect(indicator, cells.network));
        EXPECT_TRUE(!ui::rects_intersect(indicator, cells.battery));
      }
    }
  }
}

// battery sits furthest right and network immediately to its left, and
// neither may ever move when a transient indicator appears or disappears -
// see the comment on system_tray_layout() for why. Checked across both page
// choices, since a real render also varies the leading cell's width.
HOST_TEST(tray_layout_anchors_never_move_when_indicator_toggles) {
  const ui::Rect bounds = ui::safe_canvas();
  for (const app_core::PageId page :
       {app_core::PageId::Home, app_core::PageId::Weather}) {
    const ui::SystemTrayLayout without =
        ui::system_tray_layout(bounds, page, ui::TrayIndicators{});
    const ui::SystemTrayLayout with =
        ui::system_tray_layout(bounds, page, one_active(20));
    EXPECT_EQ(without.network.x, with.network.x);
    EXPECT_EQ(without.network.width, with.network.width);
    EXPECT_EQ(without.battery.x, with.battery.x);
    EXPECT_EQ(without.battery.width, with.battery.width);
  }
}

// A tray too narrow to fit everything requested must drop the transient
// indicator rather than let it overlap the leading cell or the anchored
// group - this bounds is picked specifically to leave room for time,
// network and battery (with a gap on each side) but not for an indicator
// cell as well; see system_tray_layout()'s comment for the rule this proves.
HOST_TEST(tray_layout_drops_indicator_when_the_tray_is_too_narrow) {
  const ui::Rect bounds{0, 0, 140, ui::kSystemTrayHeight};
  const ui::SystemTrayLayout cells =
      ui::system_tray_layout(bounds, app_core::PageId::Weather, one_active(20));

  EXPECT_EQ(cells.indicators[0].width, 0);
  // Dropping the transient indicator must not be an excuse to let the
  // always-present cells go invalid or collide either.
  EXPECT_TRUE(cells.time.width > 0);
  EXPECT_TRUE(cells.network.width > 0);
  EXPECT_TRUE(cells.battery.width > 0);
  EXPECT_TRUE(ui::rect_within(bounds, cells.time));
  EXPECT_TRUE(ui::rect_within(bounds, cells.network));
  EXPECT_TRUE(ui::rect_within(bounds, cells.battery));
  EXPECT_TRUE(!ui::rects_intersect(cells.time, cells.network));
  EXPECT_TRUE(!ui::rects_intersect(cells.network, cells.battery));
}

// Several indicators active at once - up to every registry slot - all get
// distinct, non-overlapping cells, placed outward from the anchored group
// in slot order (slot 0 closest to network). This is the case the dynamic
// layout exists for: a design with a fixed single "speaker" cell could not
// have expressed a second and third module both wanting to show something
// at the same time.
HOST_TEST(tray_layout_places_every_active_indicator_when_all_fit) {
  const ui::Rect bounds = ui::safe_canvas();
  for (int count = 1; count <= app_core::kMaxTrayIndicators; ++count) {
    const ui::SystemTrayLayout cells = ui::system_tray_layout(
        bounds, app_core::PageId::Weather, n_active(count, 20));

    for (int i = 0; i < count; ++i) {
      EXPECT_TRUE(cells.indicators[i].width > 0);
      EXPECT_TRUE(ui::rect_within(bounds, cells.indicators[i]));
      EXPECT_TRUE(!ui::rects_intersect(cells.indicators[i], cells.time));
      EXPECT_TRUE(!ui::rects_intersect(cells.indicators[i], cells.network));
      EXPECT_TRUE(!ui::rects_intersect(cells.indicators[i], cells.battery));
      for (int j = 0; j < i; ++j) {
        EXPECT_TRUE(!ui::rects_intersect(cells.indicators[i], cells.indicators[j]));
      }
    }
    // Slot 0 sits immediately left of network (closest to the anchored
    // group); each later slot sits further left still - the outward-from-
    // network ordering the module contract's dynamic layout promises.
    for (int i = 1; i < count; ++i) {
      EXPECT_TRUE(cells.indicators[i].right() <= cells.indicators[i - 1].x);
    }
  }
}

// With several indicators active but not enough room for all of them, the
// lower-index (higher-priority) ones win and the rest are dropped, rather
// than shrinking everyone or overlapping the leading cell. 160px is wide
// enough for time + network + battery + exactly one 20px indicator (below
// that, per the narrow-tray test above, even one is dropped; at 156px the
// margin is exactly zero) - requesting two here proves the second one
// drops while the first still gets placed, instead of both being dropped
// or both crowding in somewhere they do not belong.
HOST_TEST(tray_layout_drops_lowest_priority_indicators_first_when_crowded) {
  const ui::Rect bounds{0, 0, 160, ui::kSystemTrayHeight};
  const ui::SystemTrayLayout cells =
      ui::system_tray_layout(bounds, app_core::PageId::Weather, n_active(2, 20));

  EXPECT_TRUE(cells.indicators[0].width > 0);
  EXPECT_EQ(cells.indicators[1].width, 0);
  EXPECT_TRUE(ui::rect_within(bounds, cells.indicators[0]));
  EXPECT_TRUE(!ui::rects_intersect(cells.indicators[0], cells.time));
  EXPECT_TRUE(!ui::rects_intersect(cells.indicators[0], cells.network));
}

// --- app_core::tray_registry.hpp -------------------------------------

// Fixed capacity, enforced: filling every slot succeeds with distinct
// handles, and the next registration past capacity is refused rather than
// silently growing the registry or overwriting an existing slot.
HOST_TEST(tray_registry_capacity_is_enforced) {
  app_core::reset_tray_registry_for_test();
  const uint8_t pixel = 0;
  app_core::TrayIndicatorHandle handles[app_core::kMaxTrayIndicators];

  for (int i = 0; i < app_core::kMaxTrayIndicators; ++i) {
    handles[i] = app_core::register_tray_indicator({&pixel, 1, 1});
    EXPECT_TRUE(handles[i].valid());
    for (int j = 0; j < i; ++j) {
      EXPECT_TRUE(handles[i].slot != handles[j].slot);
    }
  }

  const app_core::TrayIndicatorHandle one_too_many =
      app_core::register_tray_indicator({&pixel, 1, 1});
  EXPECT_TRUE(!one_too_many.valid());

  app_core::reset_tray_registry_for_test();
}

// set_tray_indicator_active() on a valid handle is reflected by
// tray_indicator_slot(); on an invalid one (registry was full, or the
// caller never checked) it is a documented no-op, not a crash.
HOST_TEST(tray_registry_set_active_reflects_in_slot_and_ignores_invalid_handle) {
  app_core::reset_tray_registry_for_test();
  const uint8_t pixel = 0xff;
  const app_core::TrayIndicatorHandle handle =
      app_core::register_tray_indicator({&pixel, 3, 2});
  EXPECT_TRUE(handle.valid());

  app_core::TrayIndicatorSlot slot = app_core::tray_indicator_slot(handle.slot);
  EXPECT_TRUE(slot.registered);
  EXPECT_TRUE(!slot.active);
  EXPECT_EQ(static_cast<int>(slot.bitmap.width), 3);
  EXPECT_EQ(static_cast<int>(slot.bitmap.height), 2);

  app_core::set_tray_indicator_active(handle, true);
  slot = app_core::tray_indicator_slot(handle.slot);
  EXPECT_TRUE(slot.active);

  app_core::set_tray_indicator_active(handle, false);
  slot = app_core::tray_indicator_slot(handle.slot);
  EXPECT_TRUE(!slot.active);

  // Invalid handle: no crash, and it must not somehow reach a real slot.
  app_core::set_tray_indicator_active(app_core::TrayIndicatorHandle{}, true);

  app_core::reset_tray_registry_for_test();
}

// An index nothing has ever registered - whether never touched or out of
// range - comes back as an all-default, unregistered slot rather than
// undefined behaviour or a lucky zero-initialised look-alike.
HOST_TEST(tray_registry_slot_of_unregistered_index_is_safe_default) {
  app_core::reset_tray_registry_for_test();
  const app_core::TrayIndicatorSlot never_registered =
      app_core::tray_indicator_slot(0);
  EXPECT_TRUE(!never_registered.registered);
  EXPECT_TRUE(!never_registered.active);

  const app_core::TrayIndicatorSlot out_of_range =
      app_core::tray_indicator_slot(app_core::kMaxTrayIndicators);
  EXPECT_TRUE(!out_of_range.registered);
  const app_core::TrayIndicatorSlot negative = app_core::tray_indicator_slot(-1);
  EXPECT_TRUE(!negative.registered);
}
