#pragma once

#include <cstddef>
#include <cstdint>

#include "media_registry.hpp"

namespace airplay {

// Decodes an AirPlay cover-art JPEG into the 1-bit bitmap app_core blits.
//
// On success `out` points at a buffer this module owns, which stays valid until
// the next decode_artwork() or clear_artwork() - the same ownership rule
// app_core::MediaArtwork already documents, since core never copies.
//
// Returns false and leaves `out` untouched when the image cannot be decoded.
// That is not exceptional: a sender can send a format tjpgd does not accept, or
// PSRAM can be unavailable, and the page has to be able to show a track with no
// cover rather than no track.
//
// Expensive by embedded standards - a 176 KB JPEG, measured, is what an iPhone
// actually sends - so call it once per artwork message, not per repaint.
bool decode_artwork(const uint8_t* jpeg, size_t length,
                    app_core::MediaArtwork& out);

// Forgets the current cover. Called when a sender sends `image/none`, which is
// how it says this track has no art - without this the panel keeps showing the
// previous track's cover, which is the bug the image/none handling exists to
// prevent.
void clear_artwork();

}  // namespace airplay
