// What a remote card is allowed to say.
//
// The layer the assistant's host writes to. It carries a picture and the two
// facts the carousel needs about it, and nothing else: no coordinates, no font
// sizes, no colours. Everything a host could get wrong is either impossible to
// express here or rejected before it reaches the panel.
//
// The body is a 1 bpp mask rather than text, and that is not a compromise on
// this board - it is the panel's own format. board_rlcd's flush_display()
// thresholds every pixel to black or white before it reaches the glass, so a
// mask loses nothing on the way in. What it buys is the ability to say
// anything at all in Traditional Chinese: this firmware's CJK face is a
// 121-glyph subset generated from the string literals in components/ui
// (scripts/cjk_glyphs.py), so an assistant's sentence drawn on the device
// would come out as a row of empty boxes, silently - a missing glyph is not an
// overflow and produces no warning. See ui_fonts.hpp, which learned this when
// the OTA page rendered "WORKING" as five boxes.
//
// The device draws the frame; the host draws the body. The frame is the system
// tray (time, network, battery, module indicators) and the page dots, all of
// it built from facts only the device has. Everything inside the content area
// - including the card's own title - is the host's, drawn into the mask.
#pragma once

#include <cstddef>
#include <cstdint>

#include "page_registry.hpp"
#include "ui_data.hpp"
#include "ui_theme.hpp"

namespace ui {

// A card carries the same chrome as any other page in rotation, so its body is
// exactly the content area content_bounds() hands every rotation page. Derived
// through that one path rather than written down, because a hand-copied
// derived number is how a budget drifts without anything failing: the tray got
// a pixel taller once already.
constexpr Rect card_body_bounds() {
  return content_bounds(safe_canvas(), app_core::PageId::AssistantCard);
}

inline constexpr int kCardBodyWidth = card_body_bounds().width;
inline constexpr int kCardBodyHeight = card_body_bounds().height;

// Rows are padded to whole bytes, MSB first: bit 7 of byte 0 is pixel x=0.
// The same tight, LVGL-agnostic packing app_core::TrayIndicatorBitmap
// documents and ui::repack_i1_bits() translates, so a card body and a module's
// tray icon are authored in one format and only one place knows what LVGL's
// canvas really wants.
constexpr int card_mask_stride(int width) { return (width + 7) / 8; }

inline constexpr int kCardBodyStride = card_mask_stride(kCardBodyWidth);
inline constexpr int kCardBodyMaxBytes = kCardBodyStride * kCardBodyHeight;

// Six, matching what the host's carousel keeps per device. The reason is
// attention, not memory: a card this board never reaches before the host
// retires it was never worth fetching.
inline constexpr int kMaxCards = 6;

// ASCII, and never drawn. It names the card in the log and nowhere else -
// anything a reader is meant to see is in the mask, where the host's own fonts
// drew it. Kept short deliberately: a field that cannot be seen has no reason
// to grow.
inline constexpr int kMaxCardLabelBytes = 16;  // the storage, NUL included
// What a host may actually send, which is the storage minus the terminator.
// Published separately because these two differing by one is exactly the kind
// of off-by-one a host author cannot see and the device would reject in
// silence.
inline constexpr int kMaxCardLabelLength = kMaxCardLabelBytes - 1;

// The whole serialised response, as a bound the fetch buffer is sized from.
// Arithmetic over the budgets rather than a round number, because a round
// number stops being right the first time a field grows: base64 is four bytes
// out per three in, and each card also carries its label, priority, dwell and
// the JSON punctuation around them.
constexpr int base64_length(int raw_bytes) { return ((raw_bytes + 2) / 3) * 4; }

inline constexpr int kCardEnvelopeBytes = 128;  // label, priority, dwell, punctuation
inline constexpr int kMaxDocumentBytes =
    kMaxCards * (base64_length(kCardBodyMaxBytes) + kCardEnvelopeBytes) + 256;

// The body as pixels. Null bits is "this card has no body", and it is the only
// way to say it: dimensions mean nothing without bits, so presence is the
// pointer and nothing else can disagree with it.
struct CardMask {
  const uint8_t* bits = nullptr;
  // The mask's own pixel dimensions. Carried rather than inferred from a byte
  // count, because 11760 bytes is 388x240 and also 8x11760, and the renderer
  // cannot guess which one the host drew. A mask smaller than the body area is
  // legal and is centred; a larger one is rejected.
  int16_t width = 0;
  int16_t height = 0;
};

struct Card {
  char label[kMaxCardLabelBytes] = {};
  app_core::PagePriority priority = app_core::PagePriority::Normal;
  // 12 seconds, the same dwell every data page uses, because a card is in the
  // rotation, not privileged within it. A host may ask for longer; see
  // kMaxCardDwellSeconds for why it cannot ask for the panel outright.
  uint8_t dwell_seconds = 12;
  CardMask body;
};

// A dwell a person would not sit through is a stuck panel, not a long card.
// The ceiling is the device's, not the host's, for the reason every other
// number here is the device's: the side that suffers the failure is the side
// that gets to bound it.
inline constexpr uint8_t kMaxCardDwellSeconds = 60;
inline constexpr uint8_t kMinCardDwellSeconds = 5;

// True when every field is one this panel can actually draw. The single gate
// between the wire and the renderer: a card that fails here is dropped, and
// the slot it would have filled reports itself unavailable rather than
// rendering something half-checked.
constexpr bool card_is_drawable(const Card& card) {
  return card.body.bits != nullptr && card.body.width > 0 &&
         card.body.height > 0 && card.body.width <= kCardBodyWidth &&
         card.body.height <= kCardBodyHeight &&
         card.dwell_seconds >= kMinCardDwellSeconds &&
         card.dwell_seconds <= kMaxCardDwellSeconds;
}

}  // namespace ui
