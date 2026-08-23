// Turns an AirPlay cover-art JPEG into the 1-bit bitmap the now-playing page
// blits.
//
// Measured before any of this was written: an iPhone sends
// `Content-Type: image/jpeg` with a body of 180,224 bytes. 176 KB of JPEG
// cannot be decoded to anything full-size here, so the pipeline shrinks as it
// goes and then spends its remaining effort on tone rather than on resolution.
//
//   PSRAM JPEG  ->  tjpgd, smallest power-of-two scale that still OVERSHOOTS
//   the slot  ->  8-bit grayscale  ->  box downsample to exactly the slot
//   ->  Floyd-Steinberg error diffusion  ->  1-bit
//
// Two decisions here reverse earlier ones in this file, both for the same
// reason: this is a photograph, not a UI fill.
//
// The first version dithered inside tjpgd's output callback with no grayscale
// intermediate, to save 30 KB. That forces the dither to be an ordered one -
// error diffusion needs raster order and whole rows, and tjpgd delivers MCU
// blocks. Ordered dithering is what ui::dither_pixel_dark() does, and it is
// right for the flat tones it was built for; on a photograph its 4x4 Bayer
// matrix gives 17 levels and a visible cross-hatch. 30 KB of PSRAM is a cheap
// price for tone that reads as grayscale.
//
// The first version also decoded to the largest power-of-two scale that FIT
// the slot, which for a 600x600 cover is 1/4 = 150 px into a 176 px slot -
// undersized, and dithering 150 px of detail. It now overshoots deliberately
// and box-averages down. The averaging is the point: four source pixels
// becoming one destination pixel produce intermediate grey levels that did not
// exist in the decoded image, and error diffusion turns levels into apparent
// continuous tone. Decoding smaller and dithering harder cannot recover that.

#include "artwork.hpp"

#include <cstring>

#include <esp_heap_caps.h>
#include <esp_log.h>

extern "C" {
#include "tjpgd/tjpgd.h"
}

namespace airplay {
namespace {

constexpr char kTag[] = "airplay_art";

// The page reserves a square this many pixels on a side - ui_data.hpp's
// kNowPlayingArtworkSize. Filling it exactly is the whole point of the box
// downsample below.
//
// Duplicated rather than included: this module publishes an
// app_core::MediaArtwork and does not otherwise know components/ui exists, and
// including a rendering header here to read one integer would invert that.
// The duplication is not left on trust - now_playing_artwork_fits_slot()
// rejects anything larger than the slot and render_now_playing.cpp then draws
// no cover at all, so the two drifting apart makes the artwork silently
// vanish. artwork_target_edge_matches_the_page_slot in
// tests/host/test_artwork.cpp includes both headers and fails if they differ.
constexpr int kTargetEdge = 190;

// Ceiling on the decoded intermediate. 400x400 of 8-bit grayscale is 160 KB of
// PSRAM held for the length of one decode. Past this the extra source detail
// is averaged away by the downsample anyway, so it would be memory spent on
// nothing.
constexpr int kMaxDecodedEdge = 400;

// TJpgDec's work area; 4096 matches what LVGL's copy uses at the same
// JD_FASTDECODE level, against an upstream floor of about 3100.
//
// PSRAM rather than internal, like everything else here: internal RAM on this
// board measured 5,120 bytes of largest free block during startup TLS, and a
// 4 KB internal request there fails intermittently rather than cleanly - the
// root cause of five separate defects in this codebase. A slower decode once
// per track change is the right trade.
constexpr size_t kWorkAreaBytes = 4096;

// One decoded cover at a time. Held for the life of the session because
// MediaArtwork does not copy - app_core blits these bytes and the publisher
// owns them until it publishes different ones (media_registry.hpp).
uint8_t* g_bits = nullptr;
size_t g_bits_capacity = 0;
uint16_t g_width = 0;
uint16_t g_height = 0;

struct Source {
  const uint8_t* data;
  size_t length;
  size_t offset;
  // The grayscale intermediate tjpgd writes into.
  uint8_t* gray;
  int gray_width;
  int gray_height;
};

size_t read_source(JDEC* jd, uint8_t* buff, size_t nbyte) {
  Source* src = static_cast<Source*>(jd->device);
  const size_t remaining = src->length - src->offset;
  const size_t n = nbyte < remaining ? nbyte : remaining;
  // A null buff means "skip forward", which is how tjpgd steps over segments
  // it does not need.
  if (buff != nullptr) std::memcpy(buff, src->data + src->offset, n);
  src->offset += n;
  return n;
}

// Copies one decoded rectangle into the grayscale intermediate, converting to
// luminance on the way. No tonal decisions here on purpose - those all happen
// after the whole image exists, where raster order is available.
//
// tjpgd hands over three bytes per pixel in B, G, R order - see the
// `*pix++ = /*B*/` sequence in jd_mcu_output(). Blue first, which is the sort of
// thing worth reading off the source rather than assuming, since swapping the
// weights would tint every cover without ever looking wrong enough to notice.
//
// Rec.601 weights, in integer: 77/150/29 over 256 sums to exactly 256, so a
// grey input comes back the same grey rather than drifting a level. The JPEG's
// Y component already was luminance and tjpgd expanded it to BGR, so for a
// monochrome cover this round-trips - a few levels lost to BYTECLIP at the
// extremes, against needing no grayscale path in a source that has none.
int write_gray(JDEC* jd, void* bitmap, JRECT* rect) {
  Source* src = static_cast<Source*>(jd->device);
  const uint8_t* in = static_cast<const uint8_t*>(bitmap);
  const int rect_width = rect->right - rect->left + 1;

  for (int y = rect->top; y <= rect->bottom; ++y) {
    if (y >= src->gray_height) continue;
    const uint8_t* row =
        in + static_cast<size_t>(y - rect->top) * rect_width * 3;
    for (int x = rect->left; x <= rect->right; ++x) {
      if (x >= src->gray_width) continue;
      const uint8_t* px = row + static_cast<size_t>(x - rect->left) * 3;
      const unsigned luma = (29u * px[0] + 150u * px[1] + 77u * px[2]) >> 8;
      src->gray[static_cast<size_t>(y) * src->gray_width + x] =
          static_cast<uint8_t>(luma);
    }
  }
  return 1;  // non-zero continues, 0 aborts
}

// Smallest scale (0..3 for 1/1..1/8) whose output still covers the target, so
// the downsample below always has something to average. Falls back to the
// smallest available when even 1/8 overshoots kMaxDecodedEdge.
uint8_t choose_scale(uint16_t width, uint16_t height) {
  const int edge = width > height ? width : height;
  uint8_t best = 0;
  for (uint8_t scale = 0; scale <= 3; ++scale) {
    const int out = edge >> scale;
    if (out > kMaxDecodedEdge) {
      best = scale;      // still too big; keep shrinking
      continue;
    }
    if (out >= kTargetEdge) return scale;  // fits the cap and covers the target
    // Under the target: the previous scale was the last one that covered it,
    // unless this is the first iteration, in which case the image is simply
    // smaller than the slot and gets used as-is.
    return scale == 0 ? 0 : static_cast<uint8_t>(scale - 1);
  }
  return best;
}

// Box average from `src_edge` down to `dst_edge`. Integer accumulation, one
// destination pixel at a time - the source region for each destination pixel is
// computed rather than stepped, so a non-integer ratio does not drift.
void box_downsample(const uint8_t* in, int src_w, int src_h, uint8_t* out,
                    int dst_w, int dst_h) {
  for (int dy = 0; dy < dst_h; ++dy) {
    const int y0 = dy * src_h / dst_h;
    int y1 = (dy + 1) * src_h / dst_h;
    if (y1 <= y0) y1 = y0 + 1;
    for (int dx = 0; dx < dst_w; ++dx) {
      const int x0 = dx * src_w / dst_w;
      int x1 = (dx + 1) * src_w / dst_w;
      if (x1 <= x0) x1 = x0 + 1;
      uint32_t sum = 0;
      uint32_t count = 0;
      for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
          sum += in[static_cast<size_t>(y) * src_w + x];
          ++count;
        }
      }
      out[static_cast<size_t>(dy) * dst_w + dx] =
          static_cast<uint8_t>(count ? sum / count : 0);
    }
  }
}

// Floyd-Steinberg error diffusion, in place over the grayscale buffer, writing
// 1 bits where ink goes.
//
// Not ui::dither_pixel_dark(): that is an ordered 4x4 Bayer matrix, correct for
// the flat fills it was written for and wrong for a photograph, where it
// posterises to 17 levels and lays a visible cross-hatch over the image. Error
// diffusion carries each pixel's quantisation error into its neighbours, so
// local average brightness is preserved and the result reads as continuous
// tone. Whiter regions simply receive fewer ink pixels, which is what a
// grayscale rendering on a 1-bit panel means.
//
// The 7/16, 3/16, 5/16, 1/16 weights are Floyd and Steinberg's. Errors are
// accumulated into the grayscale buffer itself as signed values clamped to
// 0..255 - a separate error plane would be more faithful for extreme images and
// another buffer this does not need.
void diffuse_to_bits(uint8_t* gray, int w, int h, uint8_t* bits, size_t stride) {
  const auto at = [&](int x, int y) -> uint8_t& {
    return gray[static_cast<size_t>(y) * w + x];
  };
  const auto spread = [&](int x, int y, int error, int numerator) {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    int value = at(x, y) + (error * numerator) / 16;
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    at(x, y) = static_cast<uint8_t>(value);
  };

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int old = at(x, y);
      // Ink below mid-grey. The threshold is the only place polarity is
      // decided: bit set means palette index 1, which bind_i1_canvas() maps to
      // ink (ui_theme.cpp), so a dark pixel sets its bit.
      const bool dark = old < 128;
      if (dark) {
        bits[static_cast<size_t>(y) * stride + (x >> 3)] |=
            static_cast<uint8_t>(0x80U >> (x & 7));
      }
      const int error = old - (dark ? 0 : 255);
      spread(x + 1, y,     error, 7);
      spread(x - 1, y + 1, error, 3);
      spread(x,     y + 1, error, 5);
      spread(x + 1, y + 1, error, 1);
    }
  }
}

}  // namespace

void clear_artwork() {
  g_width = 0;
  g_height = 0;
}

bool decode_artwork(const uint8_t* jpeg, size_t length,
                    app_core::MediaArtwork& out) {
  clear_artwork();
  if (jpeg == nullptr || length == 0) return false;

  uint8_t* work = static_cast<uint8_t*>(
      heap_caps_malloc(kWorkAreaBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (work == nullptr) {
    ESP_LOGE(kTag, "no PSRAM for a %u-byte decode work area",
             static_cast<unsigned>(kWorkAreaBytes));
    return false;
  }

  Source src{jpeg, length, 0, nullptr, 0, 0};
  JDEC jd;
  JRESULT rc = jd_prepare(&jd, read_source, work, kWorkAreaBytes, &src);
  if (rc != JDR_OK) {
    // With the code, because the codes mean different things: JDR_FMT1 is a
    // malformed image and JDR_MEM1 is the work area being too small, and
    // reporting them alike would send someone tuning memory for a corrupt file.
    ESP_LOGW(kTag, "jd_prepare failed (rc=%d) on a %u-byte image", rc,
             static_cast<unsigned>(length));
    heap_caps_free(work);
    return false;
  }

  const uint8_t scale = choose_scale(jd.width, jd.height);
  const int gray_w = jd.width >> scale;
  const int gray_h = jd.height >> scale;
  const size_t gray_bytes = static_cast<size_t>(gray_w) * gray_h;
  uint8_t* gray = static_cast<uint8_t*>(
      heap_caps_malloc(gray_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (gray == nullptr) {
    ESP_LOGE(kTag, "no PSRAM for a %dx%d grayscale intermediate (%u bytes)",
             gray_w, gray_h, static_cast<unsigned>(gray_bytes));
    heap_caps_free(work);
    return false;
  }
  std::memset(gray, 0, gray_bytes);

  src.gray = gray;
  src.gray_width = gray_w;
  src.gray_height = gray_h;
  rc = jd_decomp(&jd, write_gray, scale);
  heap_caps_free(work);
  if (rc != JDR_OK) {
    ESP_LOGW(kTag, "jd_decomp failed (rc=%d) at 1/%d scale", rc, 1 << scale);
    heap_caps_free(gray);
    return false;
  }

  // Down to the slot. An image already at or under the target is left alone
  // rather than upscaled: interpolating detail that is not there gains nothing
  // on a 1-bit panel and would only soften the dither.
  int dst_w = gray_w;
  int dst_h = gray_h;
  uint8_t* shrunk = gray;
  if (gray_w > kTargetEdge || gray_h > kTargetEdge) {
    const int edge = gray_w > gray_h ? gray_w : gray_h;
    dst_w = gray_w * kTargetEdge / edge;
    dst_h = gray_h * kTargetEdge / edge;
    shrunk = static_cast<uint8_t*>(heap_caps_malloc(
        static_cast<size_t>(dst_w) * dst_h, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (shrunk == nullptr) {
      ESP_LOGE(kTag, "no PSRAM for a %dx%d downsample", dst_w, dst_h);
      heap_caps_free(gray);
      return false;
    }
    box_downsample(gray, gray_w, gray_h, shrunk, dst_w, dst_h);
    heap_caps_free(gray);
  }

  const size_t stride = static_cast<size_t>((dst_w + 7) / 8);
  const size_t needed = stride * static_cast<size_t>(dst_h);
  if (needed > g_bits_capacity) {
    // Grown, never shrunk: covers are all much the same size within a source,
    // so the second track reuses the first one's buffer instead of returning it
    // to a heap that has to find it again.
    uint8_t* grown = static_cast<uint8_t*>(
        heap_caps_realloc(g_bits, needed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (grown == nullptr) {
      ESP_LOGE(kTag, "no PSRAM for a %dx%d 1-bit cover (%u bytes)", dst_w, dst_h,
               static_cast<unsigned>(needed));
      if (shrunk != gray) heap_caps_free(shrunk);
      return false;
    }
    g_bits = grown;
    g_bits_capacity = needed;
  }
  // Cleared because diffuse_to_bits() only ever sets bits - a stale buffer
  // would show the previous cover's dark pixels through this one.
  std::memset(g_bits, 0, needed);

  diffuse_to_bits(shrunk, dst_w, dst_h, g_bits, stride);
  heap_caps_free(shrunk);

  g_width = static_cast<uint16_t>(dst_w);
  g_height = static_cast<uint16_t>(dst_h);
  ESP_LOGI(kTag,
           "cover %ux%u -> decoded 1/%d to %dx%d -> %dx%d dithered (%u bytes) "
           "from a %u-byte JPEG",
           jd.width, jd.height, 1 << scale, gray_w, gray_h, dst_w, dst_h,
           static_cast<unsigned>(needed), static_cast<unsigned>(length));

  out.bits = g_bits;
  out.width = g_width;
  out.height = g_height;
  return true;
}

}  // namespace airplay
