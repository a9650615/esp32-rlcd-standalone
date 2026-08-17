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

// Smallest dimension (width or height) a patch can have and still read as
// a distinct grey rather than a dark dot. Measured by eye on the physical
// panel on 2026-08-17, using /dither-card's size ramp (64/48/32/24/16/12 px
// squares, all at the same 50% level, per its own comment in
// render_dither_card.cpp): 16 px was the smallest that still read cleanly;
// 12 px did not. Not derived from any formula about dot pitch or viewing
// distance - re-measuring this means re-running that card and looking at
// the real panel, not reasoning about the number from here.
//
// One number, not a per-size or per-density correction: a report during
// this same session that smaller squares looked darker than larger ones at
// the same 50% turned out to be a photograph's moire/exposure artefact,
// not something visible looking directly at the panel - there is no
// evidence for a size-dependent tone difference, so none is encoded here.
inline constexpr int kMinDitherDimensionPx = 16;

// Flat luminance threshold - the fallback for a patch smaller than
// kMinDitherDimensionPx on either dimension, where the ordered-dither
// pattern stops reading as a distinct grey (see that constant). Same 0-16
// level scale as dither_bayer4x4_dark; the whole patch renders as one flat
// value rather than attempting the pattern at a size it no longer works
// at.
constexpr bool plain_threshold_dark(int level) { return level >= 8; }

// The one function an actual caller (a future dithered asset - nothing
// today calls this) should use: dither_bayer4x4_dark's pattern for a patch
// at least kMinDitherDimensionPx on both dimensions, plain_threshold_dark's
// flat fallback otherwise. /dither-card's own size ramp deliberately does
// not go through this - it calls dither_bayer4x4_dark directly at every
// size, unfiltered, because it is the instrument for re-measuring
// kMinDitherDimensionPx itself, and filtering its own output through the
// constant it exists to calibrate would make a future re-measurement
// circular.
inline bool dither_pixel_dark(int x, int y, int width, int height,
                              int level) {
  if (width < kMinDitherDimensionPx || height < kMinDitherDimensionPx) {
    return plain_threshold_dark(level);
  }
  return dither_bayer4x4_dark(x, y, level);
}

}  // namespace ui
