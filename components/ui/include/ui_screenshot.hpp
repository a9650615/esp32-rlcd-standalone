#pragma once

#ifndef NDEBUG
#include "app_snapshot.hpp"

namespace ui {

// Emits the last drawn frame over serial as base64, once per page per boot.
//
// Geometry logs can say a label is out of bounds or clipped; they cannot say a
// layout looks wrong. This closes that gap - scripts/decode-screenshots.py
// turns a serial capture into PNGs.
//
// Debug builds only, and deliberately not behind a Kconfig: an option
// defaulting off is how CONFIG_LV_USE_QRCODE shipped a feature that did not
// exist, and a development aid that has to be remembered is one that will not
// be there when it is wanted.
void dump_frame_once(app_core::PageId page, const char* name);

}  // namespace ui
#endif
