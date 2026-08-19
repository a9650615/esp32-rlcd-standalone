# Quiescing modules before an OTA write

Status: decision recorded, not implemented. Nothing in the tree does this yet.

## What happened

A firmware push was sent while an AirPlay session was actively playing. Two
things went wrong at once:

- **The speaker produced loud noise.** The operator's words: "剛剛部署時還在
  播放 聽到了很驚恐的噪音". Not a glitch or a dropout — noise loud enough to
  be alarming, out of a coin-sized speaker held to a desk.
- **The push failed outright.** `curl: (56) Recv failure: Operation timed
  out`, and `remote.sh` correctly reported `push did NOT install - the board
  is still on its previous firmware`. The board then stopped answering ping
  and HTTP for a period afterwards.

The board was, in that moment, decoding ALAC, feeding I2S through a DMA
channel, holding a TCP stream from the sender, running the LVGL task, and
being asked to receive 1.7 MB over HTTP and erase and write it into a flash
partition. Flash writes on ESP32-S3 stall the cache, which is what the audio
path is most sensitive to; `modules/audio`'s own notes already record that
cache traffic, not CPU time, is what starves it.

So this is not "OTA is a bit noisy". Playing audio and writing flash are
actively hostile to each other, and doing both concurrently degraded both.

## Why the obvious fix is not allowed

The obvious fix is for the OTA write path to call
`audio::audio_stream_close()` before `esp_ota_begin()`. It cannot.

`modules/README.md` rule 4: dependencies point one way, module → core. No
component under `components/` may include a module header. `components/ota`
is core; `modules/audio` and `modules/airplay` are optional modules that can
be compiled out entirely. An `#include "audio.hpp"` in `ota_session.cpp`
would invert that, and would not link in a build with the modules disabled.

`tray_registry.hpp` records what happens when this rule is bent: an
`app_core::TrayActivity` enum naming `Speaker` existed and was removed for
exactly this reason.

`main/app_main.cpp` is the one place that legitimately sees both sides, but
it cannot help here either: an OTA write starts from an HTTP handler in
`components/wifi_provision/portal.cpp`, not from anything `app_main` is on
the stack for. There is no point in `main` at which to insert the call.

## Options considered

**A. A pre-write hook registry in core.** `components/ota` exposes
`register_pre_write_hook(void (*)())`; modules register at init; the OTA
session calls every registered hook before `esp_ota_begin()`. Same shape as
`tray_registry.hpp` and the `media_registry.hpp` added for the now-playing
page — core reserves the mechanism and knows nothing about who fills it.

**B. Refuse to start an OTA while a module says it is busy.** Core asks
rather than tells; modules register a "may I be interrupted" predicate. The
push fails cleanly instead of degrading.

**C. Have modules poll `AppSnapshot::ota`.** Modules may read core, so this
respects the rule. But the phase is published on a snapshot cadence, and a
module would need its own task to watch it; by the time it noticed, the
erase would already be underway.

**D. Do nothing in firmware; the operator pauses playback first.** Zero code.

## Decision

**A, with D as the interim.**

A is chosen over B because refusing the write is a worse outcome than
performing it safely — an operator who cannot push firmware while music is
playing will simply be blocked at the moment they most want a fix, and the
board's own OTA page already handles "the write is happening, do not power
off" as a first-class state. B also leaves the noise problem unsolved for
every other reason a module might be mid-operation.

C is rejected: the latency is wrong. The hook has to run before the erase,
synchronously, and a polled snapshot cannot guarantee that.

D is what we do until A exists, and it is not sufficient — it depends on the
operator remembering, and this incident is what happens when they do not.

### Shape of A

- `components/ota` gains the registry. Value-only, no LVGL, no module types,
  host-testable in the same style as `tray_registry` and `media_registry`.
- A hook is a plain `void (*)()`, called on the task that is about to write,
  before `esp_ota_begin()`. It must return quickly and must not itself
  initiate network I/O.
- `modules/audio` registers a hook that closes any open stream and drops the
  amplifier. **The order matters and is the whole mechanism behind the noise:**
  GPIO46 has to go low *before* the codec stops being fed, or the amplifier
  sits enabled with an undriven input, which is what the operator heard.
  `audio_stream_close()` already sequences this correctly - trailing silence,
  drain, GPIO46 low, codec closed - so the hook can most likely just call it
  and wait, rather than needing teardown logic of its own. (Mechanism supplied
  by the session that owns `modules/audio`; not measured here.) A hook that
  merely stops writing PCM would leave the amplifier live and reproduce the
  fault it was added to prevent. `modules/airplay` registers one that tears down the RAOP session
  so the sender is told, rather than left with a socket that stops answering.
- Both registrations are named in each module's own README, as
  `modules/README.md` rule 5 requires.
- A hook that is slow or that hangs must not be able to prevent the write —
  the write is the recovery path for broken firmware, and must stay the
  highest-priority operation on the board.

## Consequences

An OTA push during playback becomes: audio stops, the session ends, the
sender sees a clean disconnect, then the write proceeds. The operator loses
their music, deliberately and quietly, instead of hearing noise.

Modules gain a second obligation beyond their own feature — they must behave
when the firmware is being replaced. That is a real cost, and it is the right
one: a module that cannot be interrupted safely is a module that can brick a
board.

This does not address whether the board should be receiving 1.7 MB over HTTP
while doing anything else at all. The failed push above may have been network
contention rather than flash contention, and quiescing audio may not fix it.
That is a separate question and is not answered here.

## Not yet known

- Whether the disconnect-then-write sequence is enough on its own, or whether
  the write also needs to wait for the I2S DMA to drain rather than merely be
  asked to stop.
- Whether the board's post-failure unreachability was a reboot, a rollback,
  or a Wi-Fi stack problem. Not diagnosed; it recovered on its own.
