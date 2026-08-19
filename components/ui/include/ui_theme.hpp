#pragma once

// For InputHints, which button_hints below renders.
#include "settings_menu.hpp"
// For app_core::TrayIndicatorBitmap, which tray_indicator_icon() renders.
#include "tray_registry.hpp"

#include <algorithm>
#include <cstdint>
#include <array>
#include <cstddef>
#include <string_view>

#ifndef UI_THEME_GEOMETRY_ONLY
#include <lvgl.h>
#else
struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;
#endif

namespace ui {

inline constexpr int kCanvasWidth = 400;
inline constexpr int kCanvasHeight = 300;
inline constexpr int kSafeMargin = 6;
inline constexpr int kSeparatorWidth = 1;
inline constexpr uint32_t kNavigationOverlayDurationMs = 2'000;
inline constexpr int kTileContentHeight = 76;  // 24 title + 34 value + 18 detail
inline constexpr int kTileInset = 6;
inline constexpr int kTextStrokeWidth = 1;
inline constexpr int kTextInset = 1;
inline constexpr int kDataLineWidth = 2;

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  constexpr int right() const { return x + width; }
  constexpr int bottom() const { return y + height; }
};

struct TileTextLayout {
  Rect title;
  Rect value;
  Rect detail;
};

// Collapsed from the WMO forecast codes' dozen-plus condition strings (see
// weather_parse.cpp condition_for_wmo_code) down to the handful of shapes a
// 1-bit, backlight-less panel can actually resolve at a glance - see
// weather_icon_kind_for_condition in ui_data.hpp for the mapping and
// weather_icon below for the bold silhouette each one draws.
enum class WeatherIconKind { Sun, Cloud, Rain, Snow };

constexpr Rect safe_canvas() {
  return {kSafeMargin, kSafeMargin, kCanvasWidth - 2 * kSafeMargin,
          kCanvasHeight - 2 * kSafeMargin};
}

constexpr bool within_safe_canvas(const Rect rect) {
  const Rect canvas = safe_canvas();
  return rect.x >= canvas.x && rect.y >= canvas.y &&
         rect.right() <= canvas.right() && rect.bottom() <= canvas.bottom() &&
         rect.width >= 0 && rect.height >= 0;
}

// General-purpose rect predicates used by static_assert layout proofs
// (ui_data.hpp) as well as host tests.
constexpr bool rect_within(const Rect outer, const Rect inner) {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.right() <= outer.right() && inner.bottom() <= outer.bottom();
}

// Named rects_intersect (not rects_overlap) to avoid an ADL clash with the
// unrelated local helper of that name in tests/host/test_setup_page.cpp.
constexpr bool rects_intersect(const Rect a, const Rect b) {
  return a.x < b.right() && b.x < a.right() && a.y < b.bottom() &&
        b.y < a.bottom();
}

constexpr std::array<Rect, 3> right_tile_cells(const Rect bounds) {
  const int cell_height =
      (bounds.height - 2 * kSeparatorWidth) / 3;
  return {{{bounds.x, bounds.y, bounds.width, cell_height},
           {bounds.x, bounds.y + cell_height + kSeparatorWidth, bounds.width,
            cell_height},
           {bounds.x,
            bounds.y + 2 * (cell_height + kSeparatorWidth), bounds.width,
            bounds.height - 2 * (cell_height + kSeparatorWidth)}}};
}

constexpr Rect tile_content_rect(const Rect cell) {
  return {cell.x + kTileInset,
          cell.y + (cell.height - kTileContentHeight) / 2,
          cell.width - 2 * kTileInset, kTileContentHeight};
}

// Title 24, value 34, detail 18. The title used to be the smallest row on the
// tile, which read as a caption under a number rather than a heading over one;
// at font 20 it is a heading. The value grows with it so the reading still
// dominates - equal sizes would remove the hierarchy rather than fix it.
inline constexpr int kTileTitleHeight = 24;
inline constexpr int kTileValueHeight = 34;
inline constexpr int kTileDetailHeight = 18;
constexpr TileTextLayout tile_text_layout(const Rect cell) {
  const Rect content = tile_content_rect(cell);
  return {{cell.x + kTileInset, content.y, cell.width - 2 * kTileInset,
           kTileTitleHeight},
          {cell.x + kTileInset, content.y + kTileTitleHeight,
           cell.width - 2 * kTileInset, kTileValueHeight},
          {cell.x + kTileInset,
           content.y + kTileTitleHeight + kTileValueHeight,
           cell.width - 2 * kTileInset, kTileDetailHeight}};
}

constexpr Rect tile_leading_visual_rect(const Rect cell,
                                        const bool visible) {
  const Rect value = tile_text_layout(cell).value;
  // 4px in from the cell edge, not 8. The sidebar cell is 108px wide and the
  // icon plus its old gap left only 57px for the value, which is 3px short of
  // "25.2 C" - measured on the device, not guessed.
  return visible ? Rect{cell.x + 4, value.y, 28, 28}
                 : Rect{value.x, value.y, 0, 0};
}

constexpr Rect tile_value_rect(const Rect cell, const bool with_leading_visual) {
  const Rect value = tile_text_layout(cell).value;
  return with_leading_visual
             ? Rect{cell.x + 36, value.y, cell.width - 40, value.height}
             : value;
}

constexpr bool tile_content_is_centered(const Rect cell) {
  const Rect content = tile_content_rect(cell);
  const int cell_center = cell.y * 2 + cell.height;
  const int content_center = content.y * 2 + content.height;
  const int delta = content_center > cell_center
                        ? content_center - cell_center
                        : cell_center - content_center;
  return delta <= 2;
}

constexpr int safe_text_box_height(const int requested_height,
                                   const int font_line_height) {
  const int stroke_safe_height = font_line_height + 2 * kTextInset;
  return requested_height > stroke_safe_height ? requested_height
                                                : stroke_safe_height;
}

constexpr int text_outline_width(const int font_line_height) {
  return font_line_height <= 16 ? kTextStrokeWidth : 0;
}

// Tray indicators, as plain data so UiContext can hold them in the LVGL-free
// host build. Both drawing calls return their mutable parts so a state change
// is applied in place: rebuilding the page to move a battery bar would repaint
// the whole reflective panel, which is what the cheap update path exists to
// avoid.
struct WifiIconParts {
  lv_obj_t* bars[3]{};
};

struct BatteryIconParts {
  // The level bar. Shown only while not charging - see charging_bolt below
  // for what covers it while charging, and build_battery_charging_composite()
  // (ui_theme.hpp) for why showing both at once was tried twice and
  // rejected both times.
  lv_obj_t* fill = nullptr;
  int body_width = 0;
  // The charging overlay: one canvas, opaque, positioned exactly over
  // `fill`'s own footprint, its content built once at construction
  // (battery_icon(), ui_theme.cpp) rather than recomputed per level update
  // - see build_battery_charging_composite()'s own comment for exactly
  // what it draws (a solid field with the bolt knocked out of it) and why
  // it no longer depends on charge level at all.
  lv_obj_t* charging_bolt = nullptr;
};

// The nub the battery outline reserves at its right end (see battery_icon()),
// pulled out as a named constant rather than left as a literal duplicated in
// both the drawing code and this geometry function.
inline constexpr int kBatteryIconNubWidth = 3;

// The exact inner Rect a fully-filled level bar reaches: battery_icon()'s
// own `body_width - 4` wide, `bounds.height - 4` tall, inset by 2px on the
// left/top from the outline. Exposed as its own pure function - rather than
// left as arithmetic duplicated wherever something needs to know it - so
// that both the fill bar and the charging bolt bitmap are positioned by
// calling this exact same function, not by two copies of the same formula
// that could drift apart. This is what "the charging variant occupies
// exactly the same rectangle as the level variant" means as a guarantee
// rather than a claim: they share the computation, not just the intent.
constexpr Rect battery_fill_rect(const Rect bounds) {
  const int body_width = bounds.width - kBatteryIconNubWidth - 1;
  return {bounds.x + 2, bounds.y + 2, body_width - 4, bounds.height - 4};
}

// The charging bolt, drawn as itself: one string per row, read top to
// bottom, 'X' is ink, '.' is background. Generated, not hand-placed - see
// UPSTREAM.md for full provenance. Declared here, ahead of the LVGL guard,
// so the host build can see it: this and build_battery_charging_composite()
// are pure bit-pattern logic with no LVGL or ESP-IDF dependency, and belong
// in tests/host rather than only ever being checked by screenshotting a
// real panel.
//
// Source: Bootstrap Icons' `lightning-charge-fill` (a solid filled shape,
// not an outline - it survives reduction to this size far better than the
// outlined Material Symbols glyph an earlier version of this used, per the
// operator's own instruction), vendored at
// components/ui/assets/lightning-charge-fill.svg, MIT licensed - see
// UPSTREAM.md for the pinned commit. The upstream glyph is vertical; the
// tray needs it horizontal.
//
// To regenerate (e.g. after tuning threshold/min-stroke, or if the
// vendored SVG is updated):
//   python3 scripts/svg-to-bitmap.py components/ui/assets/lightning-charge-fill.svg
//   --width 22 --height 10 --rotate 90 --threshold 0.35 --min-stroke 1
//   --emit-rows kChargingBoltRows
// and paste the printed array back in below. --rotate 90 turns the
// upstream vertical glyph horizontal *before* rasterising, not after -
// rotating the finished bitmap instead would re-jaggify an already-
// rasterised shape. Threshold 0.35 (not the naive-looking 0.5) is what
// keeps tapered tips instead of dropping them.
//
// --min-stroke 1 (the tool's stated default is 2) is deliberate, not a
// typo: this glyph is a solid fill, already chunky by construction (the
// whole reason it was chosen over the outlined Material Symbols one), and
// at min-stroke 2 the dilation step puffs up the shape's own naturally
// tapered tips into round blobs that visually detach from the tapering
// line feeding into them - checked with scipy.ndimage.label, the
// min-stroke-2 mask split into multiple connected components where the
// min-stroke-1 mask is exactly one. Dilation exists to stop a taper
// thinning into a dotted line on an *outline* glyph; a solid fill does not
// have that failure mode, so forcing the same fix onto it just introduces
// a different one. Do not hand-edit the rows below to tweak the shape -
// regenerate instead, or this comment stops being true.
inline constexpr std::string_view kChargingBoltRows[] = {
    "..........X...........",
    ".........XXX..........",
    "....XXX..XXXX.........",
    ".....XXXXXXXXX........",
    "......XXXXXXXXX.......",
    ".......XXXXXXXXX......",
    "........XXXXXXXXX.....",
    ".........XXXX..XXX....",
    "..........XXX.........",
    "...........X..........",
};
inline constexpr int kChargingBoltWidth = 22;
inline constexpr int kChargingBoltHeight =
    sizeof(kChargingBoltRows) / sizeof(kChargingBoltRows[0]);

// The one runnable check hand-authored-looking pixel art like this actually
// needs: a row one character short of kChargingBoltWidth would read past
// its own end in build_battery_charging_composite() below - std::string_view
// does not null-terminate the way a bare const char* would stop a strlen()
// at. This catches that at compile time, before it ever becomes a stray
// pixel (or worse) that only a screenshot would reveal.
constexpr bool charging_bolt_rows_match_declared_width() {
  for (const std::string_view& row : kChargingBoltRows) {
    if (row.size() != static_cast<std::size_t>(kChargingBoltWidth)) return false;
  }
  return true;
}
static_assert(charging_bolt_rows_match_declared_width(),
             "every kChargingBoltRows entry must be exactly kChargingBoltWidth "
             "characters wide");

// Recomputes the *entire* charging overlay - not just the bolt - into
// `out`: bit set means final ink colour black, bit clear means white, for
// every pixel of a `width`x`height` rect, packed row-major MSB-first at
// `(width+7)/8` bytes/row (the same tightly-packed I1 layout
// tray_indicator_icon()'s own comment documents).
//
// A pixel is black exactly when it is *not* part of the bolt shape: solid
// black field, bolt knocked out of it to white. The level bar is not shown
// while charging - no `filled`/charge-boundary concept is involved at all,
// which is why this function does not take one.
//
// Two other designs were tried on the panel first, both with a glyph
// already verified as one connected shape (see kChargingBoltRows's own
// provenance comment - this was not the earlier rounds' problem, both
// alternatives below failed with a known-good glyph feeding them):
//   - Clipped (AND-NOT): ink iff a column had charged AND the pixel was not
//     part of the bolt, nothing drawn past the charge boundary. At a
//     realistic charge level the boundary cuts through the bolt's kink, so
//     only one of its two strokes survives and the remainder reads as a
//     plain wedge, not a bolt.
//   - Inverted (XOR): ink iff exactly one of "column charged" and "part of
//     the bolt" held, so the bolt's full silhouette stayed visible on both
//     sides of the boundary, one half white-on-black and the other
//     black-on-white. Still did not read as one shape: the eye segments
//     the two halves by brightness before it can fuse them, regardless of
//     which side of the boundary either half falls on.
// Both failures share a cause a boundary-dependent design cannot avoid: a
// bolt this wide, split by any boundary partway across it, reads as two
// marks rather than one. Solid fill with the bolt knocked out avoids the
// split entirely - there is no boundary within the overlay for the eye to
// segment against. Losing the level bar while charging costs nothing real:
// terminal voltage under charge is the charger's output, not the cell's
// state, so there is no trustworthy level to show at that moment anyway -
// which is the reason this icon exists at all. Do not re-litigate either
// rejected alternative without a hardware screenshot of that *specific*
// alternative against this glyph, the way both rejections above are.
//
// Pure: reads only its arguments, writes only to `out` (silently does
// nothing if `out` is too small for width x height - a canvas one pixel
// too wide must not scribble past its own buffer). No global state, so it
// needs no cleanup between calls: every call fully repaints `out` from
// scratch.
//
// `stride` (bytes between the start of one row and the next) is the
// caller's to supply, not this function's to guess. An earlier version
// computed it here as a bare `(width + 7) / 8` - the tightest possible
// pack; that turned out not to be the actual bug that round (LVGL's own
// `lv_draw_buf_width_to_stride()`, in LVGL's lv_draw_buf.c, agrees with
// that bare formula exactly under this project's current
// `LV_DRAW_BUF_STRIDE_ALIGN`, confirmed against the panel - the on-screen
// smearing that round had a different, unrelated cause), but deriving the
// stride from LVGL rather than assuming one is still the right call: a
// build with a different alignment value would otherwise drift silently.
// The firmware call site passes LVGL's own computed stride; tests/host
// pass whatever stride they want to prove this honours, tight or padded.
inline void build_battery_charging_composite(uint8_t* out, std::size_t out_capacity,
                                              int width, int height,
                                              int stride) {
  if (out == nullptr || stride <= 0 || stride * 8 < width || height <= 0 ||
      static_cast<std::size_t>(stride) * static_cast<std::size_t>(height) >
          out_capacity) {
    return;
  }
  std::fill(out, out + stride * height, uint8_t{0});
  const int offset_x = (width - kChargingBoltWidth) / 2;
  const int offset_y = (height - kChargingBoltHeight) / 2;
  for (int y = 0; y < height; ++y) {
    const int row = y - offset_y;
    const bool row_in_bolt = row >= 0 && row < kChargingBoltHeight;
    const std::string_view line =
        row_in_bolt ? kChargingBoltRows[row] : std::string_view{};
    for (int x = 0; x < width; ++x) {
      const int col = x - offset_x;
      const bool bolt =
          row_in_bolt && col >= 0 && col < kChargingBoltWidth && line[col] == 'X';
      if (!bolt) {
        out[y * stride + x / 8] |= static_cast<uint8_t>(0x80 >> (x % 8));
      }
    }
  }
}

// Copies `tight_bits` - packed at the tight (width+7)/8 bytes/row every
// hand-authored bitmap in this codebase already uses (see
// app_core::TrayIndicatorBitmap's own comment, and kChargingBoltRows above)
// - into `out`, laid out the way an LVGL I1 canvas actually needs instead:
// `palette_bytes` left untouched at the front (LVGL's own
// lv_canvas_set_palette() claims that space - see
// g_charging_bolt_bitmap's comment in ui_theme.cpp for the full mechanism),
// then each row starting at a multiple of `stride` bytes, which is not
// necessarily the tight pack either (see build_battery_charging_composite()
// above for why this file no longer assumes it is). Both `palette_bytes`
// and `stride` are the caller's to supply, typically from
// i1_canvas_storage_bytes()/i1_canvas_stride() (ui_theme.cpp) rather than
// computed here - this function only repacks, it does not decide layout.
//
// Pure, and the shared building block behind every static tray-indicator
// icon (see bind_i1_canvas() in ui_theme.cpp): modules keep authoring the
// tight, LVGL-agnostic format app_core::TrayIndicatorBitmap documents, and
// this is the one place that gets translated into what LVGL's canvas
// actually requires, rather than every bitmap's own build function having
// to know LVGL's palette-and-stride rules for itself.
inline void repack_i1_bits(const uint8_t* tight_bits, uint8_t* out,
                           std::size_t out_capacity, int width, int height,
                           int stride, int palette_bytes) {
  const int tight_stride = (width + 7) / 8;
  if (tight_bits == nullptr || out == nullptr || width <= 0 || height <= 0 ||
      stride <= 0 || stride * 8 < width || palette_bytes < 0 ||
      static_cast<std::size_t>(palette_bytes) +
              static_cast<std::size_t>(stride) * static_cast<std::size_t>(height) >
          out_capacity) {
    return;
  }
  for (int y = 0; y < height; ++y) {
    std::copy(tight_bits + y * tight_stride,
              tight_bits + y * tight_stride + tight_stride,
              out + palette_bytes + y * stride);
  }
}

// A tray-registry indicator's icon: one LVGL canvas object showing exactly
// the bytes its module registered (see app_core::TrayIndicatorBitmap) -
// core never draws anything module-specific itself. Its cell is reserved
// unconditionally for every registered slot (see render_tray()) precisely
// so the cheap per-tick visibility toggle below always has a real target.
struct TrayIndicatorIcon {
  lv_obj_t* canvas = nullptr;
};

#ifndef UI_THEME_GEOMETRY_ONLY

void apply_surface(lv_obj_t* object);
// `wraps` only suppresses the debug overflow warning; it does not change the
// long mode. Prefer label_wrapped below, which does both.
lv_obj_t* label(lv_obj_t* parent, const char* text, Rect bounds,
                const lv_font_t* font = nullptr,
                lv_text_align_t align = LV_TEXT_ALIGN_LEFT,
                bool wraps = false);

// A label that wraps instead of ellipsising. One call rather than the
// create-then-set-long-mode pair, which had to be written four times and got
// the overflow warning wrong every time.
lv_obj_t* label_wrapped(lv_obj_t* parent, const char* text, Rect bounds,
                        const lv_font_t* font = nullptr,
                        lv_text_align_t align = LV_TEXT_ALIGN_LEFT);
lv_obj_t* divider(lv_obj_t* parent, Rect bounds);
lv_obj_t* line_segment(lv_obj_t* parent, int x, int y, int width, int height,
                       bool inverse = false);
void weather_icon(lv_obj_t* parent, Rect bounds, WeatherIconKind kind,
                  bool inverse = false);
void temperature_icon(lv_obj_t* parent, Rect bounds, bool inverse = false);
void humidity_icon(lv_obj_t* parent, Rect bounds, bool inverse = false);
// Fills the bottom band on pages where the buttons do something other than
// turn pages. A no-op when hints.visible is false.
WifiIconParts wifi_icon(lv_obj_t* parent, Rect bounds, bool connected);
void set_wifi_icon_state(const WifiIconParts& parts, bool connected);

// The shared building block behind every I1 canvas in this file
// (battery_icon()'s charging overlay, tray_indicator_icon()'s module
// bitmaps): the two things that have each cost their own debugging round
// on this exact panel - LVGL's I1 palette living in the first
// i1_canvas_stride/-storage bytes of the buffer, not a separate allocation
// (see g_charging_bolt_bitmap's comment, ui_theme.cpp), and LVGL padding
// each row to its own stride rather than a tight (width+7)/8 pack - only
// need working out once.
//
// i1_canvas_stride(): LVGL's own row stride for a `width`-pixel I1 row
// (wraps lv_draw_buf_width_to_stride() - never recompute this as
// (width+7)/8, see build_battery_charging_composite()'s own comment for
// why that drifts). i1_canvas_storage_bytes(): the minimum size a buffer
// passed to bind_i1_canvas() must be. bind_i1_canvas() itself creates the
// canvas, binds `storage` (which must already hold pixel data starting at
// i1_canvas_stride()'s own palette offset - see repack_i1_bits() above for
// how a tight-packed bitmap gets there, or build_battery_charging_composite
// for content computed directly at the right offset) and sets its
// 2-colour palette; `storage` must outlive the returned canvas, since LVGL
// keeps the pointer rather than copying.
// Where pixel data starts within an I1 canvas buffer - everything before
// this is LVGL's own palette (lv_draw_buf_set_palette() writes there
// directly; lv_draw_buf_goto_xy() skips exactly this many bytes before
// reading the first pixel). Genuinely constexpr - LV_COLOR_INDEXED_PALETTE_
// SIZE and sizeof(lv_color32_t) are both compile-time - so every caller
// that needs this offset (i1_canvas_storage_bytes below,
// build_battery_charging_composite's and repack_i1_bits's callers) uses
// this one definition rather than each re-deriving the same "8".
constexpr int i1_canvas_pixel_offset() {
  return LV_COLOR_INDEXED_PALETTE_SIZE(LV_COLOR_FORMAT_I1) *
         static_cast<int>(sizeof(lv_color32_t));
}
int i1_canvas_stride(int width);
std::size_t i1_canvas_storage_bytes(int width, int height);

// Compile-time equivalents of the two functions just above, for the fixed
// backing arrays that have to be sized before LVGL exists to be asked. Those
// two call into LVGL and so cannot be constexpr; these mirror
// lv_draw_buf_width_to_stride()'s own formula for LV_COLOR_FORMAT_I1 (see
// width_to_stride() in lv_draw_buf.c: the 1-bit-per-pixel byte width rounded
// up to LV_DRAW_BUF_STRIDE_ALIGN, which is read by name here rather than
// assumed to stay 1).
//
// Public rather than private to ui_theme.cpp because sizing an I1 buffer by
// hand is precisely what has gone wrong twice: once as a bare (width + 7) / 8
// that ignored the palette prefix and LVGL's stride, and once as
// `width * height`, which is the byte count for 8 bits per pixel and so
// over-allocated by 8x - 43 KB of .bss for one debug screen's swatches, enough
// that net_log could no longer create its 4 KiB sender task. A caller that
// needs a compile-time size should have one correct answer to reach for.
constexpr int i1_canvas_stride_bound(int width) {
  const int width_bytes = (width + 7) / 8;  // 1 bit per pixel, rounded up
  return ((width_bytes + LV_DRAW_BUF_STRIDE_ALIGN - 1) /
          LV_DRAW_BUF_STRIDE_ALIGN) *
         LV_DRAW_BUF_STRIDE_ALIGN;
}
constexpr std::size_t i1_canvas_storage_bound(int width, int height) {
  return static_cast<std::size_t>(i1_canvas_pixel_offset()) +
         static_cast<std::size_t>(i1_canvas_stride_bound(width)) *
             static_cast<std::size_t>(height);
}
// `background_opa` is separate from `ink`'s (always LV_OPA_COVER) because
// callers disagree on it: the battery overlay is a self-contained
// replacement for everything underneath it (LV_OPA_COVER), while a tray
// indicator's off pixels must stay transparent so the tray's own
// background shows through rather than a second, redundant white square
// (LV_OPA_TRANSP).
lv_obj_t* bind_i1_canvas(lv_obj_t* parent, int x, int y, int width, int height,
                        uint8_t* storage, lv_color_t background,
                        lv_opa_t background_opa, lv_color_t ink);

// `charging` is a separate flag, not folded into `valid`: an invalid
// reading draws an empty body (nothing measured), while charging draws a
// solid body with a bolt reversed out of it (a real reading exists, it is
// just not a trustworthy level - see ui_data.hpp's
// battery_percent_trustworthy()). The two must stay tellable apart.
BatteryIconParts battery_icon(lv_obj_t* parent, Rect bounds, uint8_t percent,
                              bool valid, bool charging);
void set_battery_icon_level(const BatteryIconParts& parts, uint8_t percent,
                            bool valid, bool charging);
// Renders a module's registered 1-bit bitmap as an LVGL canvas
// (LV_COLOR_FORMAT_I1) positioned at `bounds` - core blits the bytes and
// never interprets them. Returns an all-null TrayIndicatorIcon if `bitmap`
// is empty (nothing registered for this slot) or the canvas could not be
// created. See app_core::TrayIndicatorBitmap for the exact byte layout a
// module must supply - that layout is deliberately simple (tight-packed,
// LVGL-agnostic) and stays the module's problem to author; repacking it
// into what LVGL's own canvas actually needs (see repack_i1_bits() and
// bind_i1_canvas() above) is this function's problem, not every module's.
// `slot` (an index into app_core::kMaxTrayIndicators) selects which of
// this function's own per-slot storage buffers to repack into - a fixed,
// small number of persistent backing buffers, not one shared buffer three
// modules could stomp on if more than one icon were ever visible at once.
TrayIndicatorIcon tray_indicator_icon(lv_obj_t* parent, Rect bounds, int slot,
                                      const app_core::TrayIndicatorBitmap& bitmap);
// Shows or hides the whole icon - not a per-part toggle like wifi_icon's
// rings, since a tray-registry icon is one opaque bitmap, not several
// hand-drawn primitives core understands individually.
void set_tray_indicator_icon_visible(const TrayIndicatorIcon& icon, bool visible);

void button_hints(lv_obj_t* parent, Rect bounds, InputHints hints);
void page_dots(lv_obj_t* parent, std::size_t page_index,
               std::size_t page_count, Rect bounds);
lv_obj_t* navigation_overlay(lv_obj_t* parent, Rect bounds);
// Inverts a label to a black-filled bar with white text when is_error is
// true, or restores the normal black-on-white look otherwise. Shared by the
// initial Setup page render and the label-only status repaint path so a
// status that flips from neutral to a failure (or back) while Setup stays
// on screen is restyled the same way a full page rebuild would style it.
void apply_setup_status_style(lv_obj_t* label_obj, bool is_error);

#endif

}  // namespace ui
