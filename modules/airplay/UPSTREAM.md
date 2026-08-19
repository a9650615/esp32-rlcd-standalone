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
- **`src/audio_buffer.c`: `audio_output_task()`'s stack raised to 8192, its
  two `MAX_FRAME_SIZE` scratch buffers moved out of internal RAM, and the task
  instrumented.** Upstream creates this task with a 4096-byte stack and keeps
  two 2048-byte scratch arrays (`silence`, `tap_data`) in automatic storage
  inside it. It overflowed on the first real stream this receiver ever
  decoded - `***ERROR*** A stack overflow in task audio_output has been
  detected`, reproducible on every attempt, at any stream length, landing
  immediately after RECORD.

  The depth is not in this file. It is in `output_cb()`, which leads into this
  project's own ES8311/I2S sink (`modules/audio`), and it only runs once
  playback starts - which is why the crash arrives with RECORD rather than
  with SETUP. Measured after the fix, the task's high water mark settles at
  3,764 bytes free of 8192, so it uses about 4,428: upstream's 4096 could
  never have been enough, whatever the scratch buffers did.

  The buffers were moved anyway, because they should not sit in internal RAM
  at all. `silence` is now `static const` and lives in flash, costing no RAM
  of any kind - the callbacks take a const pointer and the contents are
  zeroes that never change. `tap_data` is now a PSRAM allocation made once in
  `audio_buffer_init()` beside the frame ring, following this file's own
  convention for large buffers. Making them `static` was tried first and
  rejected on measurement: it moved 4 KiB from the stack to `.bss`, which is
  the same internal RAM, and cost exactly as much (79,571 free after startup
  before, 71,055 after). Between them, the two moves pay for the 4 KiB the
  stack grew by.

  `uxTaskGetStackHighWaterMark()` reporting was added at the same time,
  matching what `raop.c` and `rtp.c` already do, so the next shortage in this
  task is visible before it is fatal rather than after. It is what turned
  8192 from a guess into a number with a measurement behind it.

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
- **The measurement came back; two of the three sizes were wrong.** With the
  logging above running on hardware through a real session, the free-stack
  minima were: `rtsp_thread()` 7,668 bytes free of 8,192, `audio_output`
  3,764 of 8,192, `rtp_thread_func()` **1,556 of 4,096**, `search_remote()`
  **736 of 3,072**. `RTSP_STACK_SIZE` stays at 8 KiB - the 8 KiB guess above
  turned out to be generous rather than reckless, and it is not cut further
  because the one path that dominates its depth, the `mbedtls` RSA-2048
  operation, does not run at all in a test-sender session, so 7,668 is an
  upper bound on free space and not necessarily the one an iPhone produces.
  The other two were raised: `RTP_STACK_SIZE` 4 -> 6 KiB and
  `SEARCH_STACK_SIZE` 3 -> 5 KiB. Neither was failing, which is the point -
  736 bytes of margin does not fail, it makes every later measurement on
  this system ambiguous between "the fix did nothing" and "something else
  overflowed". `rtp_thread_func()`'s depth in particular is not bounded by
  anything in this module: it calls `data_cb` straight down into the audio
  sink, so a change three modules away moves it. Both stacks are struct
  members, so this adds 2 KiB of internal RAM to `rtp_t` and 2 KiB to
  `raop_ctx_s`; `rtp_init()` now reports the free and largest-block figures
  explicitly if its allocation ever cannot be met.
- **`src/rtp.c`: `rtp_t`'s RTP task stack (`xStack`) split out of the struct
  into its own `heap_caps_malloc()`.** The `RTP_STACK_SIZE` raise above (4 ->
  6 KiB) pushed `sizeof(rtp_t)` back up to 12,888 bytes, and because the
  stack was still an embedded `StackType_t[]` member, `rtp_init()`'s single
  `heap_caps_calloc(sizeof(rtp_t), ...)` needed all 12,888 as one contiguous
  block again. A second AirPlay SETUP after one completed session found
  51,263 bytes free but the largest block only 12,288 - 600 bytes short,
  reproducing the exact allocation failure the two entries above already
  fixed once, from the other direction. Same total RAM, two allocations
  (`xStack` and the rest of `rtp_t`) instead of one, freed together in
  `rtp_end()` regardless of which allocation failed. Kept
  `MALLOC_CAP_INTERNAL` - a static FreeRTOS task stack must live in internal
  RAM, never PSRAM. `raop_ctx_s` was checked for the same problem and left
  alone: it allocates once per receiver at boot (`raop_create()`), not once
  per session, so it does not accumulate the fragmentation repeated
  SETUP/TEARDOWN cycles cause. Measured at that allocation: 13,468 bytes
  needed, 22,528 largest block, 41,275 free - comfortable margin, no split
  needed there.
- **`src/rtp.c`: `buffer_put_packet()`'s first-frame `playtime` no longer
  trusts `ctx->synchro.{time,rtp}` before a sync packet has set them.** DATA
  and CONTROL/TIMING arrive on separate UDP sockets with no ordering
  guarantee, so the first DATA packet can legitimately reach
  `buffer_put_packet()` before any 0x54 sync packet has been successfully
  processed (measured: the one sync packet that arrived first was itself
  discarded by the existing `remote_gap > 10000` sanity check, so
  `RTP_SYNC` was still unset). `playtime = ctx->synchro.time +
  ((rtptime - ctx->synchro.rtp) * 10) / (RAOP_SAMPLE_RATE / 100)` against
  zero-valued `synchro` fields produced `rtptime * 10 / 441` - measured
  6,364,753 ms, 106 minutes in the future - which `audio_buffer_set_start_
  time()` (this project's `audio_buffer.c`, downstream of `RAOP_INT_PLAY`)
  took at face value: the consumer task waited for a playtime that would
  never arrive, the ring filled, and every frame was dropped for the rest of
  the session - audio never reached the speaker despite the RTSP/RTP session
  otherwise completing normally. Now falls back to `gettime_ms()` (play
  immediately) when `RTP_SYNC` is not yet set, at both call sites in this
  function; a real sync packet arriving moments later corrects
  `ctx->synchro` for every subsequent frame exactly as before. Found and
  fixed while verifying the two-SETUP-in-a-row fix above actually produces
  audible output, not just a successful `SETUP` - "no 503" and "the
  amplifier turns on" turned out to be two different, independently
  reachable failure points in this pipeline.
- **`src/audio_buffer.c`: `audio_buffer_set_start_time()`'s confirmation log
  raised from `ESP_LOGD` to `ESP_LOGI`, and a rate-limited (first 5 only)
  warning added to `audio_output_task()`'s `start_time == 0` wait.** Both are
  what made the bug above provable rather than merely suspected: the
  "buffer full, dropping frame" spam it produces looks identical whether
  `audio_buffer_set_start_time()` was never called, called with a stale/zero
  value, or called correctly but with a value that will never satisfy
  `now >= frame->playtime`. One line per session each, not per frame - kept
  rather than reverted.

### AirPlay 1's own minimum-latency case still drops every frame

**Not fixed - out of scope for this session, recorded so the next person
does not have to rediscover it.** `scripts/raop-test-sender.py` deliberately
sets its sync packets' `rtp_now_latency` so the receiver's computed
`ctx->latency` lands exactly on `MIN_LATENCY` (11,025 samples / 250 ms - see
that script's own comment, "a no-op" against the clamp). With the two fixes
above landed and a real sync established quickly, `buffer_push_packet()`
still discards essentially every frame as arriving after its own `playtime`
(`now > playtime`), by a gap that grows over the session (a few tens of ms
observed, not shrinking) rather than settling - so `feed_audio()` /
`audio_stream_write()` is never reached and the amplifier (GPIO46) never
raises, even though the RTSP/RTP session completes cleanly with no errors.
Not chased further here: it needs real measurement of where the 250 ms
budget is actually spent (this receiver's own processing, the ALAC decode,
`data_cb`'s path down into `modules/audio`, or something else), which is a
different, larger job than the two allocation-contiguity bugs above.

### The sink watchdog was shorter than the protocol's own startup latency

Not an upstream change - a bug in *our* `modules/audio` that made upstream
look broken, recorded here because the next person to debug silent AirPlay
will start in this directory.

`audio_stream_open()` arms an abandoned-stream watchdog, originally 2,000 ms,
on the reasoning that "real audio does not have multi-second gaps between
chunks". That is true between two chunks mid-stream and false for the gap
that actually matters: the one between `open()` and the **first** chunk.
RAOP opens the sink as soon as the sender sends RECORD, then holds every
frame until its scheduled `playtime` arrives. That hold is the sender's
declared latency, clamped in `rtp.c` to `MAX_LATENCY` = 120 * 44100 * 2 / 100
= 105,840 frames = **2.4 s**. The watchdog therefore expired, during normal
operation, before one sample was ever written - it closed the stream, the
ring filled, frames dropped, and a receiver that was working end to end
presented as a receiver that produced silence. The symptom reported was
"connects but will not play", which is exactly what this produces.

Raised to 5,000 ms. Any value below ~2.4 s is not a tuning preference, it is
a guaranteed failure on every session.
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

- **`src/raop.c`: one diagnostic log line at the `SET_PARAMETER` entry.**
  A panel test with two different senders (an iPhone and an Apple TV) showed
  a live progress bar and no track title at all. The upstream code logs only
  on a *successful* DMAP parse, so every other outcome - the sender not
  sending metadata, the body being dropped by `util.c`'s 8192-byte ceiling
  before the handler sees it, or the branch conditions rejecting a body that
  did arrive - is indistinguishable from the log. The added line names the
  `Content-Type` and whether a body survived, which separates those cases in
  one read. No behaviour changes; it is `LOG_INFO` beside the existing ones.

- **`src/raop.c`: the DMAP metadata condition was inverted.** Upstream tested
  `if (!dmap_parse(&settings, body, len))`, but `dmap_parse()` returns 1 on
  success and 0 on every failure path (`dmap_parser.c`: `return 1` after
  `parse_value()`; `return 0` for a short buffer, an unknown tag, or a size
  that disagrees with the payload). The metadata callback therefore fired
  only when parsing had failed, and every correctly parsed track title was
  discarded. Symptom on hardware: an iPhone and an Apple TV both produced a
  working progress bar and a permanently empty title, with no error logged
  anywhere - the body was arriving intact and parsing fine. Now `if
  (dmap_parse(...))`.

- **`src/dmap_parser.c`: the string callback never received its tag.**
  `parse_value()` called `settings->on_string(ctx, NULL, NULL, buf, len)` -
  the second parameter is the four-character DMAP code, and passing NULL made
  the callback unable to tell a title from an artist from an album.
  `raop.c`'s own `on_dmap_string()` starts with `if (!code || !buf) return;`,
  so every string was discarded on arrival. The tag was already in hand one
  frame up the stack (`item_tag` in the container walk); it is now threaded
  through `parse_value()` as a `const char *tag` parameter and passed to the
  callback. Found immediately after fixing the inverted `dmap_parse` test
  above: with that corrected the callback finally fired, and reported
  `meta=yes title='' artist='' album=''` - the event arriving with every
  field empty is what pointed here.

- **`src/raop.c` / `include/esp_raop_receiver.h`: added `raop_get_remote_
  hostname()`.** A real session showed the progress bar and volume overlay
  frozen for the whole 35 s a track played after the first few packets - an
  AirPlay 1 sender only pushes progress at track start and on a seek, so
  `airplay.cpp` needed a periodic republish to derive elapsed time and pick
  up volume changes on its own clock, and while making that publish path
  periodic anyway it also picks up the sender's mDNS hostname
  (`search_remote()`'s existing `LOG_INFO("found remote ...")`, already
  logging it) for the now-playing page's source line, in place of the
  literal `"AIRPLAY"`. New module-level `g_remote_hostname[64]` in `raop.c`,
  written once per session by `search_remote()` when it resolves a remote,
  and the new getter declared in the public header. Implemented in `raop.c`
  rather than `raop_core.c` (which implements every sibling getter there)
  deliberately: `raop_core.c` was out of scope for this change, and
  `struct raop_handle_s`'s fields are private to that file, so there was no
  way to plumb a per-handle value through it without editing it anyway - a
  parameterless getter backed by a plain global sidesteps that entirely,
  consistent with `raop_core.c`'s own single-connected-receiver assumption
  (`s_handle`). No behaviour change to anything upstream already did; this
  only adds a new symbol.
