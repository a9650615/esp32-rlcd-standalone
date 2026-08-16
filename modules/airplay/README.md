# airplay

A vendored AirPlay 1 (RAOP) receiver skeleton: it compiles, and
`airplay_init()` starts a real RTSP/RTP/mDNS/ALAC receiver, but nothing plays
the audio it decodes yet. See `modules/README.md` for the module contract
this follows (`CONFIG_AIRPLAY_ENABLE`, default `n`, compiles to nothing when
off, one-way dependency on core). See `UPSTREAM.md` for exactly what was
vendored, from where, and what was changed.

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
actual streamed audio - but its `audio_output_cb` is `airplay.cpp`'s
`discard_audio()`, which does nothing with the PCM it receives. This proves
the receiver runs; it does not make sound.

## What this depends on, and what depends on it

Nothing calls `airplay_init()` anywhere in this repository yet. Per the
module contract's rule 5, a module names its core touch points explicitly -
this module's list is empty, on purpose: no `main/app_main.cpp` call, no
tray registration, no wiring to `modules/audio`. That integration is a
deliberately separate pass, not an oversight - see the task this module was
built for.

The dependency direction the module contract requires (rule 4: modules point
at core, never the other way; and for this module specifically, `airplay ->
audio`, never `audio -> airplay`) is upheld by construction: this module's
`CMakeLists.txt` does not list `audio` in its `PRIV_REQUIRES`, and no file
here includes `audio.hpp`. Whichever later pass wires decoded PCM somewhere
real is what adds that dependency, deliberately, when it has an actual
callback target to point at.

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
get past the CMake check. This proves the vendored RAOP receiver and ALAC
decoder compile and link. It does not make an AirPlay device appear
anywhere, because nothing calls `airplay::airplay_init()` - see "What this
depends on" above.

## Core touch points

None. Per the module contract's rule 5, this is the complete list: no call
in `main/app_main.cpp`, no route in `components/wifi_provision/portal.cpp`,
no tray registration. That is deliberate for this pass, not a gap to fill in
by accident later without noticing it was intentional.

## What is unverified

Everything runtime-related, since nothing is wired up and nothing has run on
real hardware:

- **`airplay_init()`/`airplay_deinit()` have never been called.** Nothing in
  this repository calls them. Whether `raop_init()` actually succeeds on
  this board - mDNS starts, the RTSP listener binds, the task stacks it
  allocates fit - is unverified.
- **No AirPlay client has ever connected to this receiver.** The RSA
  handshake, RTP reception, ALAC decode path, and DMAP metadata parsing are
  all vendored as-is (bar the two changes in `UPSTREAM.md`) but have not
  been exercised end-to-end from a real sender against this vendored copy.
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
- **PSRAM/stack cost under real streaming load is unmeasured.** The
  `CONFIG_AIRPLAY_ENABLE=y` binary size in this module's build verification
  covers link-time cost only, not the two PSRAM buffers above (allocated at
  `raop_init()` time, which nothing calls) or the RTSP task's actual stack
  high-water mark under a real session.
