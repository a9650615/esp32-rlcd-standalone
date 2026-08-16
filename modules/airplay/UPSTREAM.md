# Module provenance

Two separate upstreams, vendored into two separate subdirectories.

## esp-raop-receiver (AirPlay 1 / RAOP protocol)

- Repository: <https://codeberg.org/Edu_Coder/esp-airsync>
- Pinned commit: `1f2249dbb061871dd27d4a506f924ede84ea159e` (2026-03-21)
- Licence: GPL-3.0 (repository root `LICENSE`) - compatible with this
  repository's own licence, since both are GPL-3.0.
- Vendored path: `components/esp-raop-receiver/` from that repository, into
  `modules/airplay/esp-raop-receiver/` here.
- **Not vendored**: that repository's `main/` (its own app entry point,
  wifi/portal/OTA glue - this project has its own), `components/wled_sync/`
  (WLED audio-reactive sync - out of scope for a receiver skeleton), and
  `components/i2s_output/` (this board's audio output is `modules/audio`,
  the ES8311/I2S path already in this repository).
- **Not vendored**: `components/esp-raop-receiver/lib/libalac.a`, upstream's
  prebuilt 169 KB opaque ALAC decoder archive. Replaced with source - see
  the ALAC entry below.

### Intentional changes

- **`src/raop.c`: Apple's AirPort Express RSA private key removed.**
  Upstream hardcodes it as a string literal (`super_secret_key`) inside
  `rsa_apply()`. That is Apple's key, extracted from hardware, not this
  project's key to hold or distribute - see `README.md`, "The RSA key
  situation", for the full reasoning. `rsa_apply()` now declares
  `raop_private_key_pem_start`/`_end` as `extern` symbols instead, generated
  at build time from a file the builder supplies at the gitignored
  `modules/airplay/secrets/raop_private_key.pem` (`CMakeLists.txt`'s
  `target_add_binary_data(..., TEXT)` call, checked and fatal-erred on
  first, before that call runs, if the file is missing).
- **`src/audio_buffer.h` / `src/audio_buffer.c` / `src/raop_core.c`: buffer
  depth cut from 512 frames to 384.** Upstream sizes both its RTP jitter
  buffer (`raop_core.c`'s `RAOP_INT_SETUP` PSRAM allocation) and its decoded-
  PCM ring (`audio_buffer.c`'s `BUFFER_FRAMES`) at 512 352-sample frames each
  (~4.1 s / ~2.7 MB combined - upstream's own README states "~23 seconds" and
  "~2.7MB" for these two buffers together), sized for multi-room sync
  headroom this receiver does not implement. Both now come from one named
  constant, `RAOP_BUFFER_FRAMES` in `audio_buffer.h`, set to 384 (~3.05 s) -
  see the comment on that constant for the exact reasoning and the ~1.3 MB
  combined result. `rtp.c`'s own `BUFFER_FRAMES_MAX`/`BUFFER_FRAMES_MIN` were
  left unchanged: those size a small internal-RAM bookkeeping array (`abuf_t`
  pointers/counters inside `rtp_t`, itself `MALLOC_CAP_INTERNAL`), not a
  PSRAM buffer, so they are not what "23 seconds of PSRAM" is about.
- **No source changes beyond the two items above.** Everything else -
  RTSP/RTP/DMAP parsing, timing/resend logic, the `esp_raop_receiver.h`
  public API shape - is upstream's as pinned.

## ALAC decoder (Apple Lossless Audio Codec)

- Repository: <https://github.com/mikebrady/alac> (a maintained mirror of
  Apple's own `macosforge/alac` release)
- Pinned commit: `5d8c5db0dfcadd5872f28e665cf4f4303447352a` (2026-04-10)
- Licence: Apache License 2.0 (repository root `LICENSE`/`COPYING`, copied
  into `modules/airplay/alac/LICENSE-APACHE-2.0`) - a separate licence from
  esp-raop-receiver's GPL-3.0 above; Apache-2.0 code may be included in a
  GPL-3.0 work (that direction is permissive-into-copyleft), which is why
  this is licence-compatible with the rest of this repository.
- Vendored path: `codec/` from that repository (decoder-only files), into
  `modules/airplay/alac/codec/` here:
  `ALACDecoder.cpp`/`.h`, `ALACAudioTypes.h`, `ALACBitUtilities.c`/`.h`,
  `EndianPortable.c`/`.h`, `ag_dec.c`, `aglib.h`, `dp_dec.c`, `dplib.h`,
  `matrix_dec.c`, `matrixlib.h`.
- **Not vendored**: the encoder side (`ALACEncoder.cpp`/`.h`, `ag_enc.c`,
  `dp_enc.c`, `matrix_enc.c`) - a receiver only ever decodes - and
  `convert-utility/` (a standalone CLI tool, unrelated to decoding on-device).
- **Why source instead of upstream's prebuilt `libalac.a`**: an
  unauditable 169 KB opaque binary archive has no place in an open-source
  repository under this project's own name. Apple's ALAC decoder being
  Apache-2.0 made vendoring the source directly practical rather than merely
  aspirational - this was not judged impractical.
- **`alac/alac_wrapper.cpp` is new code, not vendored from anywhere.**
  Upstream's `esp-raop-receiver/private_include/alac_wrapper.h` declares a
  small C API (`alac_create_decoder`/`alac_to_pcm`/`alac_delete_decoder`)
  that upstream implements inside the prebuilt `libalac.a` it does not ship
  source for. This file is a thin bridge from that C API onto Apple's
  `ALACDecoder` C++ class next door, written for this repository and
  licensed under this repository's own GPL-3.0 - it does not incorporate any
  Apache-2.0 source itself, it only calls it.

### Intentional changes

- **`EndianPortable.c`: fixed a real little-endian-detection bug for this
  chip.** Upstream's `TARGET_RT_LITTLE_ENDIAN` is only ever defined for
  `__i386__`, `__x86_64__`, or `TARGET_OS_WIN32`. None of those are defined
  on xtensa (ESP32), so the macro fell through *undefined* (`#if
  TARGET_RT_LITTLE_ENDIAN` evaluates that as `0`, i.e. "big endian"), and
  every `Swap*NtoB`/`Swap*BtoN` call in that file silently became a no-op on
  this chip instead of an actual byte swap - upstream never ran this file on
  an actual little-endian, non-x86 target with those swaps exercised. Added
  an `#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ ==
  __ORDER_LITTLE_ENDIAN__` branch (GCC/Clang define `__BYTE_ORDER__`
  universally), which correctly picks up xtensa. This has not been run on
  real hardware yet - see README.md, "What is unverified" - but it is a
  correctness fix against a real defect in the ported file, not a guess.
- No other changes: `matrix_dec.c`'s own `#if TARGET_RT_BIG_ENDIAN` was
  checked for the same class of bug and is fine as-is - that macro is also
  undefined on this chip, but its `#else` branch (taken when undefined) is
  the little-endian-correct byte ordering already, so no fix was needed
  there.
