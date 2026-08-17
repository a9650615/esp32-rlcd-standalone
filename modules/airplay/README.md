# airplay

A vendored AirPlay 1 (RAOP) receiver: `airplay_init()` starts a real
RTSP/RTP/mDNS/ALAC receiver and feeds the decoded PCM into `modules/audio`'s
streaming sink for the session's lifetime, using the RAOP receiver's own
connected/disconnected events as the session boundary. It registers its own
tray indicator (active only while a session is open) and disables Wi-Fi
power save for the session's duration. See `modules/README.md` for the
module contract this follows (`CONFIG_AIRPLAY_ENABLE`, default `n`, compiles
to nothing when off, one-way dependency on core - here, `airplay -> audio`
and `airplay -> app_core`, never the reverse). See `UPSTREAM.md` for exactly
what was vendored, from where, and what was changed.

## What this is, precisely

Two vendored upstreams (`UPSTREAM.md` has the full provenance):

- `esp-raop-receiver/` - the AirPlay 1/RAOP protocol itself: RTSP handshake,
  RTP audio reception, DMAP metadata parsing, mDNS advertisement. From
  `codeberg.org/Edu_Coder/esp-airsync`, GPL-3.0.
- `alac/` - Apple's own open-source ALAC decoder (decode-only), from
  `github.com/mikebrady/alac`, Apache-2.0, plus `alac/alac_wrapper.cpp` (new
  code written for this repository) bridging it onto the C API
  `esp-raop-receiver` expects. This replaces upstream's prebuilt
  `libalac.a` - an unauditable 169 KB opaque binary has no place in an
  open-source repository.

`airplay_init()`/`airplay_deinit()` in `include/airplay.hpp` are the only
public surface. Calling `airplay_init()` starts mDNS advertisement and the
RAOP receiver for real - RTSP listener, RTP reception, ALAC decoding of
actual streamed audio - and its `audio_output_cb` (`airplay.cpp`'s
`feed_audio()`) writes every decoded chunk straight into
`audio::audio_stream_write()`. The session's open/close boundary comes from
the RAOP receiver's own `event_cb` (`handle_event()`): `RAOP_EVENT_CONNECTED`
calls `audio::audio_stream_open(44100)`, disables Wi-Fi power save, and
raises the tray indicator; `RAOP_EVENT_DISCONNECTED` closes the stream,
restores Wi-Fi power save, and lowers the tray indicator. Every other RAOP
event (`BUFFERING`/`PLAYING`/`STOPPED`/`PAUSED`/`VOLUME`/`METADATA`/
`ARTWORK`/`PROGRESS`/`STALLED`) is a sub-state within one still-open session
and is deliberately left unhandled - confirmed by reading `raop_core.c`'s
`internal_cmd_cb()` dispatch that `CONNECTED`/`DISCONNECTED` are the only two
that fire once each, at RTSP SETUP/TEARDOWN, rather than repeatedly within a
session.

## What this depends on, and what depends on it

`main/app_main.cpp` calls `airplay::airplay_init()` once, alongside (and with
the same non-fatal treatment as) `audio::audio_init()` - no `#ifdef` at that
call site; `airplay.hpp`'s inline no-ops make the call correct whether or not
`CONFIG_AIRPLAY_ENABLE` is on. That is this module's only core touch point;
see "Core touch points" below.

The dependency direction the module contract requires (rule 4: modules point
at core, never the other way; and for this module specifically, `airplay ->
audio`, never `audio -> airplay`) now holds as a real, exercised dependency
rather than merely "upheld by construction": this module's `CMakeLists.txt`
lists both `audio` and `app_core` in `PRIV_REQUIRES`, `airplay.cpp` includes
`audio.hpp` and `tray_registry.hpp`, and calls
`audio::audio_stream_open()`/`_write()`/`_close()` and
`app_core::register_tray_indicator()`/`set_tray_indicator_active()`
directly. Neither `audio` nor `app_core` includes anything from this module
- registering a second tray indicator needed zero changes to
`components/app_core/tray_registry.*` or anywhere else in `components/`,
which is the acceptance test that registry design was built to pass.

## The RSA key situation

AirPlay 1's authentication handshake requires a specific RSA private key -
the one from Apple's AirPort Express hardware, extracted and now
long-public across many open-source AirPlay implementations. It both signs
the `Apple-Challenge` a sender presents (`RSA_MODE_AUTH` in `raop.c`'s
`rsa_apply()`) and decrypts the AES session key a sender sends in
`rsaaeskey` (`RSA_MODE_KEY`, same function). There is no way to implement
AirPlay 1 without a copy of this key somewhere in the build; it is
structurally part of the protocol, not an implementation choice.

**This repository does not ship it and will not.** This is Apple's key, not
this project's to redistribute, and this project is public, GPL-3.0, under
the author's real name - see the top-level project context for why that
matters here specifically. `esp-raop-receiver/src/raop.c` upstream hardcodes
it as a string literal (`super_secret_key`); this vendored copy strips that
out entirely (see `UPSTREAM.md`'s "Intentional changes").

**What replaces it**: `raop.c` now declares
`raop_private_key_pem_start`/`raop_private_key_pem_end` as `extern` symbols,
generated at build time from a PEM file you supply yourself at:

```
modules/airplay/secrets/raop_private_key.pem
```

That exact path is gitignored (`modules/airplay/.gitignore`, scoped to this
directory rather than the repo root, deliberately - the rule belongs with
the module that needs it). `CMakeLists.txt` checks for that file at CMake
configure time, only when `CONFIG_AIRPLAY_ENABLE=y`, and fails the build
with an explanatory message - not a mysterious missing-symbol link error -
if it is absent:

```
CMake Error ... CONFIG_AIRPLAY_ENABLE=y needs Apple's AirPort Express RSA
private key, PEM-encoded, at:
    .../modules/airplay/secrets/raop_private_key.pem
This repository does not ship that file and will not: ...
```

If present, `target_add_binary_data(${COMPONENT_LIB}, ..., TEXT)` embeds it
into the firmware image directly (the same ESP-IDF mechanism `EMBED_TXTFILES`
uses), null-terminated - `mbedtls_pk_parse_key()`'s PEM path requires that
terminator to be included in the length it is given, which is why `TEXT`
mode (not `BINARY`) matters here.

**Where to get a copy, if you want to build this on.** This key is widely
published across open-source AirPlay projects (shairport-sync and its many
derivatives all carry a copy under this same name, `super_secret_key`) -
finding one is not the point of this module's design; keeping this specific
repository from being the thing that ships it is.

## Buffer sizing: a few seconds, not upstream's ~23

Upstream sizes its RTP jitter buffer and decoded-PCM ring at 512 352-sample
ALAC frames each (~4.1 s / ~2.7 MB combined PSRAM, per upstream's own
README, which describes "~23 seconds" and "~2.7MB" for these two buffers
together) - headroom for multi-room sync, which this receiver does not
implement. Both buffers now come from one named constant,
`RAOP_BUFFER_FRAMES` in `esp-raop-receiver/src/audio_buffer.h`, set to **384
frames (~3.05 s)** - a few seconds of headroom against Wi-Fi jitter, nothing
more, at roughly half upstream's combined PSRAM cost (~1.3 MB instead of
~2.7 MB). See the comment on that constant, and `UPSTREAM.md`, for the full
reasoning and what was deliberately left alone (`rtp.c`'s own
`BUFFER_FRAMES_MAX`/`MIN`, which size a small internal-RAM array, not a PSRAM
buffer).

## Power

AirPlay needs Wi-Fi power save disabled and sustained CPU for the RTSP/RTP/
ALAC/mDNS work - there is no low-power path through "wait for a packet,
sleep, wake" the way this board's other network activity (a portal request,
an OTA check) can get away with. That makes this realistically a
mains-powered feature on a battery device, which matters *here specifically*
because this firmware records its own battery-drain history: enabling
AirPlay and leaving it advertised is not a cost this project's existing
battery-drain tracking has ever accounted for, and would show up in that
history as a real, sustained load increase, not noise.

## How to enable it (and what that currently proves)

```
CONFIG_AIRPLAY_ENABLE=y
```

plus a real `modules/airplay/secrets/raop_private_key.pem` (see above) to
get past the CMake check. Measured with a throwaway, self-generated,
non-functional key (deleted immediately after, purely to satisfy the
CMake file-existence/PEM-parse check - never Apple's real key, never
committed):

- Flash: `CONFIG_AIRPLAY_ENABLE=y` is **1,684,608 bytes** (0x19b480) versus
  **1,596,448 bytes** (0x185c20) with it off - about **86.1 KiB** for the
  vendored RAOP receiver, ALAC decoder, and this module's own wiring, now
  that something actually calls into all of it (previously `=y`/`=n`
  measured identically, because the linker's `--gc-sections` stripped the
  whole module when nothing referenced it).
- PSRAM: **not** consumed at `airplay_init()`/`raop_init()` time - both of
  the buffers in "Buffer sizing" below are allocated at `RAOP_INT_SETUP`,
  i.e. only once a client actually completes the RTSP SETUP handshake. Once
  that happens: 384 frames * `sizeof(audio_frame_t)` (2,060 bytes on this
  32-bit target) = 791,040 bytes for the decoded-PCM ring
  (`audio_buffer.c`), plus 352*4*384 = 540,672 bytes for the RTP jitter
  buffer (`raop_core.c`) - **1,331,712 bytes (~1.27 MiB) combined**, matching
  this module's own "~1.3MB" estimate in `audio_buffer.h`'s comment. This is
  a static figure derived from the vendored source's own allocation sizes,
  not something observed under a real session - see "What is unverified"
  below.

This proves the vendored RAOP receiver, ALAC decoder, and the wiring into
`modules/audio`, the tray, and Wi-Fi power save all compile and link
end-to-end, including the previously-untested `CONFIG_AIRPLAY_ENABLE=y`
build with `CONFIG_AUDIO_ENABLE=y`. It does not prove any of it works: no
real RSA key has ever been supplied, no client has connected, and no audio
has ever reached the speaker - see "What is unverified" below.

## Core touch points

One: `main/app_main.cpp` calls `airplay::airplay_init()` once, right after
`audio::audio_init()`, logging readiness or unavailability but never
treating failure as fatal. No other file in `components/` or `main/`
references this module - the tray indicator and the audio sink are both
reached through the same registries `modules/audio` already uses
(`app_core::register_tray_indicator()`, `audio::audio_stream_open()`), not
through any new core surface. That a second module's tray icon needed zero
changes to `components/app_core/tray_registry.*` is exactly what that
registry design set out to prove.

## What is unverified

Everything that needs a real key and a real AirPlay client, since neither
has ever existed against this build:

- **No real RSA key has ever been supplied.** Every build in this module's
  history, including the one that produced the flash-size figure above, used
  either no key (build fails, by design) or a throwaway self-generated key
  that lets CMake's check pass but cannot complete AirPlay 1's actual
  handshake. Whether `raop_init()` succeeds against a real key on this board
  - mDNS starts, the RTSP listener binds, the task stacks it allocates fit -
  is unverified.
- **No AirPlay client has ever connected to this receiver.** The RSA
  handshake, RTP reception, ALAC decode path, and DMAP metadata parsing are
  all vendored as-is (bar the two changes in `UPSTREAM.md`) but have not
  been exercised end-to-end from a real sender against this vendored copy.
- **`RAOP_EVENT_CONNECTED`/`DISCONNECTED` firing `audio_stream_open()`/
  `_close()` for real, under a real session, is unverified.** The event
  dispatch was confirmed by reading `raop_core.c`'s `internal_cmd_cb()`
  source, not by observing a real SETUP/TEARDOWN pair drive this module's
  `handle_event()`.
- **The `EndianPortable.c` little-endian fix (see `UPSTREAM.md`) has not
  been confirmed on real hardware.** It is a correctness fix against a real,
  identified defect in how the file was ported (reasoned from source, not
  observed), not something a decoded-audio-on-a-speaker test has confirmed.
- **`alac_wrapper.cpp`'s bridge onto `ALACDecoder` has never decoded a real
  ALAC frame.** The `BitBuffer` bounds constant (`kMaxInputBytes`), the
  `Init()`/`Decode()` call shapes, and the `mConfig` field readback were all
  derived by reading `ALACDecoder.h`/`.cpp` and cross-checking against how
  `esp-raop-receiver/src/rtp.c` already calls this API - not by decoding a
  real stream and hearing correct audio out the other end.
- **`RAOP_BUFFER_FRAMES = 384` (~3.05 s) has not been tuned against real
  Wi-Fi jitter on this board.** It is a reasoned reduction from upstream's
  512-frame default (see "Buffer sizing" above), not a value validated
  against dropouts on this board's actual network environment.
- **PSRAM/stack cost under real streaming load is unmeasured.** The figure
  above is a static sum of the vendored source's own allocation sizes; the
  RTSP/RTP/decode tasks' actual stack high-water marks, and whether PSRAM
  fragmentation or concurrent allocations from other modules matter under a
  real session, remain unmeasured.
- **Audio actually reaching the speaker has never happened.** `feed_audio()`
  writing into `audio::audio_stream_write()` is verified by build/link only;
  whether real decoded PCM at 44.1kHz/16-bit/stereo plays back correctly -
  and whether a notification tone requested mid-session is cleanly refused
  rather than corrupting the stream (see `modules/audio/README.md`'s
  streaming section) - needs a real session to confirm.
