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
  left unchanged *at the time this entry was first written*: those size a
  small internal-RAM bookkeeping array (`abuf_t` pointers/counters inside
  `rtp_t`, itself `MALLOC_CAP_INTERNAL`), not a PSRAM buffer, so they were not
  what "23 seconds of PSRAM" was about. `BUFFER_FRAMES_MAX` was later tied to
  `RAOP_BUFFER_FRAMES` directly - see the entry below for why.
- **`src/rtp.c`: `BUFFER_FRAMES_MAX` now derives from `RAOP_BUFFER_FRAMES`
  instead of a separate `(RAOP_SAMPLE_RATE * 10) / 352` (10 s) constant.**
  Measured on hardware: `raop_create()` needs one contiguous 27,804-byte
  internal-DRAM block (`raop_ctx_s`, see the two entries below) when only
  17,408 is available at that point, and RTSP `SETUP` then needs a second
  contiguous 25,600-byte block (`rtp_t`, dominated by this array) while the
  first is still held - the largest contiguous internal-DRAM block measured
  right after boot is 31,744 and does not grow by freeing more memory, so
  both allocations could never coexist regardless of what else was trimmed
  elsewhere. `rtp_t`'s `audio_buffer[BUFFER_FRAMES_MAX]` (an array of
  `abuf_t`, 17 bytes each, packed) was upstream's `1252`-frame 10 s ceiling,
  but `buffer_alloc()` in the same file only ever fills entries for as long
  as the PSRAM frame-data buffer backing them (`raop_core.c`'s
  `RAOP_INT_SETUP` allocation, `RAOP_BUFFER_FRAMES` frames) has bytes left -
  so 1252-384 = 868 of those entries (17 bytes each, packed - ~14.4 KiB)
  could never be reached; pure waste sitting in the one struct that most
  needed to shrink. Rather than
  hardcode a second, smaller frame count that could silently drift from
  `RAOP_BUFFER_FRAMES` again in the future, `rtp.c` now `#include`s
  `audio_buffer.h` (same directory, same component) and sets
  `BUFFER_FRAMES_MAX` to `RAOP_BUFFER_FRAMES` directly, so the bookkeeping
  array and the PSRAM buffer it indexes into can never disagree on frame
  count again. Measured result: `sizeof(rtp_t)` 25,600 -> 10,840 bytes.
- **`src/raop.c`: `RTSP_STACK_SIZE` cut from 24 KiB to 8 KiB, PROVISIONAL.**
  This `StackType_t` array is embedded as a member of `raop_ctx_s` (not a
  separate allocation), so it was the single largest contributor to that
  struct's 27,804-byte size and the main reason `raop_create()` could not
  find a large enough contiguous internal-DRAM block. Upstream's 24 KiB was
  never measured against what `rtsp_thread()` actually uses (HTTP
  header/DMAP parsing, one `mbedtls` RSA-2048 sign or decrypt per session, no
  deep recursion); 8 KiB is a deliberate guess sized to fit under the
  measured 17,408-byte ceiling with room to spare while still being generous
  for that workload, **not a final value** - `rtsp_thread()` now logs its
  `uxTaskGetStackHighWaterMark()` every time a new minimum is observed, and
  the final size will come from real iPhone-connected hardware runs, not
  from this comment. `SEARCH_STACK_SIZE` (3 KiB, the `active_remote` mDNS
  search task's stack, same embedding problem but a much smaller
  contributor) was left unchanged - already small enough that cutting it
  further wasn't needed to clear the ceiling, and it gets the same
  high-water-mark logging in `search_remote()` regardless, in case
  measurement says otherwise. Measured result: `sizeof(raop_ctx_s)` 27,804 ->
  11,420 bytes.
- **`src/raop.c` / `src/rtp.c`: added `uxTaskGetStackHighWaterMark()` logging
  to `rtsp_thread()`, `search_remote()`, and `rtp_thread_func()`.** Each
  tracks the smallest high-water-mark it has seen and logs (`LOG_INFO`) only
  when a new minimum is observed, rather than on every loop iteration (all
  three loops wake on ~100 ms-3 s timeouts even when idle) or only once at
  task exit (none of these tasks exit while a session is active, so an
  exit-time log would never fire). This is the measurement the two stack-size
  entries above are provisional pending - it is not itself a permanent
  feature and can be removed once the sizes above are set from real
  hardware numbers.
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
