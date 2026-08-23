#pragma once

#include <cstdint>

namespace app_core {

// Fixed capacity - no dynamic allocation. 4 is comfortably more than any
// module needs today (one, for audio's speaker indicator), with headroom
// for a couple more before this constant needs revisiting;
// register_tray_indicator() below enforces it rather than assuming callers
// register a sane number.
inline constexpr int kMaxTrayIndicators = 4;
inline constexpr uint8_t kTrayIconSize = 20;
inline constexpr int kTrayIconStride = (kTrayIconSize + 7) / 8;
inline constexpr int kTrayIconBitmapBytes = kTrayIconStride * kTrayIconSize;

static_assert(kTrayIconStride == 3);
static_assert(kTrayIconBitmapBytes == 60);

// A fixed-size 20x20, 1-bit bitmap supplied by the module that registers it.
// Core only ever blits these bytes, and never interprets what they depict or
// knows who supplied them. Row-major, MSB-first (bit 7 of byte 0 is pixel
// x=0), each row padded to kTrayIconStride bytes.
//
// This is *not* the layout LVGL's own `LV_COLOR_FORMAT_I1` canvas actually
// wants on its wire, despite an earlier version of this comment claiming
// it was: LVGL reserves palette bytes at the front of the buffer
// (lv_draw_buf_set_palette() writes there directly) and pads each row to
// its own stride (lv_draw_buf_width_to_stride()), which is not always this
// tight pack. `ui::tray_indicator_icon()` (ui_theme.cpp) repacks a
// module's bytes into that real layout before handing them to LVGL - see
// its own comment, and ui::repack_i1_bits()'s, for why. This tight,
// LVGL-agnostic format is deliberately kept as what a module authors,
// specifically so no module has to know any of that.
//
// `pixels` must point to kTrayIconBitmapBytes valid bytes and stay valid for
// the module's entire lifetime - in practice a static const array, the same
// way this project's compiled-in fonts and strings are static data rather than
// something allocated and freed. `byte_count` lets registration reject a
// short payload before any renderer reads it.
struct TrayIndicatorBitmap {
  const uint8_t* pixels = nullptr;
  uint8_t width = 0;
  uint8_t height = 0;
  uint8_t byte_count = 0;
};

// Returned by register_tray_indicator(). A negative slot (see valid())
// means registration failed because the bitmap pointer, dimensions, or byte
// count was invalid, or the registry was already full at kMaxTrayIndicators -
// callers must check this rather than assume registration always succeeds.
struct TrayIndicatorHandle {
  int8_t slot = -1;
  constexpr bool valid() const { return slot >= 0; }
};

// Registers one indicator, typically once at a module's init time. Core
// draws whatever `bitmap` contains and knows nothing about who registered
// it or why - another module registers its own icon here and this component
// does not change by a single line. See modules/README.md for the module
// contract this satisfies.
TrayIndicatorHandle register_tray_indicator(const TrayIndicatorBitmap& bitmap);

// Sets whether a registered indicator wants to show right now. Safe to
// call from any task - this only flips a flag; nothing here ever touches
// LVGL, which only runs on its own thread. A no-op for an invalid handle.
void set_tray_indicator_active(TrayIndicatorHandle handle, bool active);

// Read-only snapshot of one registry slot - a plain copy (the real state
// underneath is synchronized for cross-task access, but that is this
// file's problem, not the caller's), for ui:: to render from and for host
// tests to inspect. index outside [0, kMaxTrayIndicators) or never
// registered both come back as an all-default, unregistered slot rather
// than being undefined.
struct TrayIndicatorSlot {
  bool registered = false;
  bool active = false;
  TrayIndicatorBitmap bitmap;
};
TrayIndicatorSlot tray_indicator_slot(int index);

// Test-only: clears every slot back to unregistered. Production code never
// needs this - the registry is meant to fill up once at startup and stay
// that way - but host tests register modules-worth of indicators
// repeatedly across independent test cases and must not leak state between
// them.
void reset_tray_registry_for_test();

}  // namespace app_core
