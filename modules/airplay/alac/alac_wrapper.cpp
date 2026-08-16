// alac_wrapper.cpp - thin C API bridge from esp-raop-receiver's
// alac_wrapper.h onto Apple's own ALACDecoder C++ class (vendored in
// modules/airplay/alac/codec/, see modules/airplay/UPSTREAM.md).
//
// Upstream esp-raop-receiver does not ship this file: it links a prebuilt
// lib/libalac.a instead, an opaque 169 KB binary this project will not carry
// (see UPSTREAM.md). This file is the replacement - new code written for
// this repo, not vendored from anywhere, licensed under this repo's own
// GPL-3.0 like every other file in modules/airplay/ (it only calls the
// Apache-2.0 ALACDecoder API next door; it does not incorporate its source).
//
// Not exercised by any test yet - see "What is unverified" in
// modules/airplay/README.md.
#include "alac_wrapper.h"

#include "ALACDecoder.h"
#include "ALACBitUtilities.h"

#include <cstdlib>

struct alac_codec_s {
  ALACDecoder *decoder;
  uint32_t num_channels;
};

// Upper bound for one ALAC frame's compressed input, used only as
// BitBufferInit's bounds-check fence, not as a length that drives decoding
// (the ALAC bitstream is self-describing; BitBuffer just needs to know it is
// not allowed to read past this). esp-raop-receiver's own RTP receive
// buffers (MAX_PACKET in rtp.c) are 1408 bytes; this is a deliberately
// generous round number above that, not a tuned value.
static constexpr uint32_t kMaxInputBytes = 2048;

extern "C" struct alac_codec_s *alac_create_decoder(
    int magic_cookie_size, unsigned char *magic_cookie,
    unsigned char *sample_size, unsigned *sample_rate,
    unsigned char *channels, unsigned int *block_size) {
  auto *codec = static_cast<alac_codec_s *>(malloc(sizeof(alac_codec_s)));
  if (!codec) {
    return nullptr;
  }
  codec->decoder = new ALACDecoder();

  if (codec->decoder->Init(magic_cookie, static_cast<uint32_t>(magic_cookie_size)) != 0) {
    delete codec->decoder;
    free(codec);
    return nullptr;
  }

  codec->num_channels = codec->decoder->mConfig.numChannels;
  if (sample_size) *sample_size = codec->decoder->mConfig.bitDepth;
  if (sample_rate) *sample_rate = codec->decoder->mConfig.sampleRate;
  if (channels) *channels = codec->decoder->mConfig.numChannels;
  if (block_size) *block_size = codec->decoder->mConfig.frameLength;

  return codec;
}

extern "C" void alac_delete_decoder(struct alac_codec_s *codec) {
  if (!codec) {
    return;
  }
  delete codec->decoder;
  free(codec);
}

extern "C" bool alac_to_pcm(struct alac_codec_s *codec, unsigned char *input,
                            unsigned char *output, char channels,
                            unsigned *out_frames) {
  if (!codec || !codec->decoder || !input || !output || !out_frames) {
    return false;
  }

  BitBuffer bits;
  BitBufferInit(&bits, input, kMaxInputBytes);

  uint32_t decoded_frames = 0;
  int32_t err = codec->decoder->Decode(&bits, output,
                                        codec->decoder->mConfig.frameLength,
                                        static_cast<uint32_t>(channels),
                                        &decoded_frames);
  *out_frames = decoded_frames;
  return err == 0;  // ALAC_noErr, see ALACBitUtilities.h
}
