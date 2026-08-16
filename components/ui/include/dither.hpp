#pragma once

#include <cstdint>

// Ordered (Bayer) dithering, for putting greyscale content on a panel that
// only has black and white: a classic 4x4 threshold matrix, tiled across
// whatever area needs it. Not wired into anything yet - see
// components/ui/render_dither_card.cpp for a static test card that
// demonstrates the pattern on the actual display, and
// components/board_rlcd/lvgl_port.cpp's flush_display() for the current,
// un-dithered hard threshold (`< 0x7fff` -> black) this could someday
// replace for real photographic/greyscale assets. Deliberately not that
// replacement itself: this header only supplies the pattern, and nothing
// here touches the flush path or converts a real asset.
//
// Callers must scale their own source value to a 0-16 "level" (luminance,
// not a packed color) before calling dither_bayer4x4_dark() - RGB565's bit
// layout does not rank by perceived brightness (green gets more bits than
// red or blue), so comparing raw 16-bit pixel magnitudes, the way
// flush_display() currently does for its plain threshold, dithers the
// wrong quantity. A future caller converting a real RGB565 source should
// compute luminance first (e.g. the usual 0.30/0.59/0.11 R/G/B weighting)
// and scale that 0-16, not reuse the packed value.
namespace ui {

// Row-major 4x4 threshold matrix, values 0-15, the standard ordered-dither
// pattern (each value appears exactly once, spread so no two adjacent
// cells share a similar threshold - this is what avoids the banding a
// naive row-by-row or checkerboard threshold would show).
inline constexpr uint8_t kBayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

// True ("paint dark") for pixel (x, y) at the given fill level, 0-16
// sixteenths (matching the matrix's 16 distinct thresholds): level 0 never
// paints, level 16 always does, and every value in between reproduces
// exactly that many of every 16 pixels dark - 8, for instance, is exactly
// half - tiled seamlessly in both directions since only x & 3 / y & 3 ever
// matter. Levels outside 0-16 saturate rather than wrap or misbehave.
inline bool dither_bayer4x4_dark(int x, int y, int level) {
  if (level <= 0) return false;
  if (level >= 16) return true;
  return kBayer4x4[y & 3][x & 3] < level;
}

}  // namespace ui
