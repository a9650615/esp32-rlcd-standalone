# audio

Stage 1 of onboard audio: an ES8311 codec and its I2S TX path, enough to play
a short, quiet tone through the board's speaker for an alarm or a
notification. See `modules/README.md` for the module contract this follows
(`CONFIG_AUDIO_ENABLE`, compiles to nothing when off, one-way dependency on
core).

## Hardware

| Signal | Pin | Note |
| --- | --- | --- |
| I2S MCLK | GPIO16 | |
| I2S BCLK | GPIO9 | |
| I2S LRCLK | GPIO45 | strapping pin, see below |
| I2S DOUT | GPIO8 | to the codec / speaker |
| I2S DIN | GPIO10 | microphone; wired but unused - no capture in this stage |
| Amplifier enable | GPIO46 | strapping pin, see below; active HIGH (see polarity note below) |

The codec is an ES8311 at I2C address `0x18` on the board's one shared I2C
bus (SDA13/SCL14, owned by `components/board_rlcd/board_i2c.cpp`). A physical
speaker plugs into the MX1.25 header next to it. A boot-time I2C scan on the
real board found `0x18 0x40 0x51 0x70` - the codec, an ES7210 mic ADC (not
driven by anything here), the PCF85063 RTC, and the SHTC3, all sharing the
bus.

## Why GPIO46 must default low and only rise while playing

GPIO45 and GPIO46 are ESP32-S3 strapping pins, sampled at reset: 45 selects
the VDD_SPI voltage, 46 controls ROM boot-message printing. LRCLK on GPIO45
is driven by the I2S peripheral itself only once I2S starts, well after reset
sampling, so it does not need special handling. The amplifier enable on
GPIO46 is different - it is a plain GPIO fully under this module's control,
which means nothing else guarantees its level across a reset except this
code.

`audio_init()` writes GPIO46 low before it ever configures the pin as an
output, and `audio_play_tone()` is the only thing that ever raises it, for
exactly the duration of a tone. Two separate reasons this matters:

- **Strapping safety.** A pin observed high during the next boot's reset
  sampling can change how that boot behaves.
- **Battery accounting.** This is a battery-powered device with active
  battery-drain tracking. An amplifier stuck on would both burn real current
  and corrupt that history with load it wasn't supposed to see.

## The pop-free enable/disable sequence

Flipping GPIO46 while the I2S line is idle, or mid-transition, is what
produces an audible turn-on/turn-off thump. `audio_play_tone()` in
`audio.cpp` avoids that by keeping the I2S channel already flowing known
samples on both sides of the GPIO transition:

1. Open the codec and I2S channel.
2. Write a short run of silence (`kSilenceLeadMs`, 20 ms).
3. Raise GPIO46.
4. Write the tone (see below).
5. Write another short run of silence (`kSilenceTrailMs`, 20 ms).
6. Drop GPIO46.
7. Close the codec/I2S channel.

The amplifier only ever transitions while the DMA is already carrying silence
on the other side of that transition, not while it is starting up or mid
first-sample.

Every write in that sequence is checked (`esp_codec_dev_write()` returns an
error code, not just a log line to ignore), and the amplifier is guaranteed
to end up low - and the codec closed - on every exit path, including a
write failure partway through.

**The DMA queue does not drain when a write returns.** `i2s_channel_write()`
(called by `esp_codec_dev_write()`) only copies data into a free DMA
descriptor; it does not wait for that descriptor to actually be clocked out.
`I2S_CHANNEL_DEFAULT_CONFIG`'s `dma_desc_num`/`dma_frame_num` (6 x 240 frames)
give the channel roughly 90 ms of buffering, so the trailing silence write
returning is not proof the trailing silence has reached the pin yet. `audio.cpp`
sets those two values explicitly (`kDmaDescNum`/`kDmaFrameNum`) and derives
`kDrainMs` from them, then waits that long after the last write before
dropping GPIO46 or closing the channel - otherwise the amplifier can drop
mid-fade-out, which is exactly the click the whole sequence exists to avoid.

## The tone itself

A clean sine, not a square wave, and well under full scale even before the
codec's own volume control: `kToneAmplitude` in `audio.cpp` is 50% of int16
full scale (`kToneAmplitudeScale = 0.5f`) - the same ceiling the diagnostic
sweep below is capped at. The tone buffer also carries its own `kFadeMs`
(5 ms) linear fade in and out, so the waveform itself starts and ends at
zero - without that, the sine's own start/stop step is a click, and a small
speaker turns that click into something louder than the tone.

## The volume default

`kOutputVolumePercent` in `audio.cpp` is `50` (0-100, forwarded to
`esp_codec_dev_set_out_vol`). That number has a narrow, specific origin, and
the comment on it is deliberately honest about what it does and does not
mean: it is a conservative default picked during a night-time bring-up
session, specifically to avoid startling anyone at 1am while the sweep
staircase was first tried on the actual speaker. It is known to be clearly
audible in a quiet room at close range - nothing more. **It has not been
validated for the use case that actually matters**: an alarm or
notification that has to be heard across a room, or over background noise,
may well need considerably more than this. Read it as "safe to leave
running unattended", not as "the right level for a real alarm".

The earlier value (30%, with a 0.25 amplitude scale) was tried first and was
too quiet to hear at all; the operator then picked 50% purely to be
conservative, not by evaluating it against the actual use case. Do not treat
either number as validated - treat `kOutputVolumePercent` as a placeholder
until someone deliberately re-evaluates it for what an alarm needs to do.

Nothing here requires a rebuild to change: `POST /beep?vol=NN` overrides the
volume for that call (and persists it as the new default - see
`audio_set_volume()`), and `audio::audio_set_volume(percent)` does the same
from other core code. That is the cheap way to re-evaluate this number - use
it rather than "optimising" the compiled-in default back to a guess.

## GPIO46 polarity: active-high, checked against Waveshare's own config

The active-HIGH assumption above was re-checked, not just carried forward,
after real hardware played a tone with no reported error but no audible
sound: Waveshare's own ESPHome reference configuration for this board
enables the speaker with a plain `platform: gpio` switch on `GPIO46` and no
`inverted: true`, i.e. their own firmware also drives it high to enable and
low to disable. No evidence was found for active-low, so the polarity was
left unchanged rather than "corrected" on a guess.

## Diagnosing "no error, but no sound"

A tone can report success end to end - `POST /beep` returns 200, the log
shows the codec opening and every write succeeding - while nothing reaches
the ear. Three separate things can cause that, and telling them apart needs
evidence, not another guess:

1. **Too quiet**, or too low a frequency for a coin-sized driver to move air
   well. **Confirmed on real hardware, and the actual cause**: 2 kHz at 20%
   was already clearly audible, and the top of the original 20/40/60/80%
   staircase came back as "a bit loud" - see "The volume default" above and
   the staircase below, which has since had its ceiling brought down
   accordingly.
2. **The amplifier is not actually enabling** - wrong polarity, or GPIO46 not
   doing what this code assumes. **Ruled out, with evidence, not just
   because sound came out** - see the verdict below.
3. **The ES8311 DAC path is not actually producing output** - muted, wrong
   clock, or an analog path left disabled. The codec answering on I2C and
   opening without error only proves the *control* path, not the *audio*
   path.

### Verdict: GPIO46 genuinely gates the amplifier

The first hardware run logged `GPIO46 (amp enable) reads 0 after raising` on
every step despite audible sound, which fit two different explanations with
opposite consequences: either the readback itself was lying, or the enable
line was not actually gating the amplifier and it was on continuously in
hardware - a real defect on a battery-powered device that tracks its own
drain curve. Two independent pieces of evidence settled which:

- **The readback was the broken half, and the reason is documented, not
  guessed.** `configure_amp_gpio()` used to configure the pin with
  `GPIO_MODE_OUTPUT`, and ESP-IDF's `gpio_config()`
  (`esp_driver_gpio/src/gpio.c`) calls `gpio_input_disable()` on a pin
  whenever `GPIO_MODE_DEF_INPUT` is not set in the requested mode -
  output-only genuinely disables the input path, so `gpio_get_level()` on
  such a pin reads a frozen/meaningless value regardless of what is
  actually being driven. The pin is now configured `GPIO_MODE_INPUT_OUTPUT`
  instead, which keeps the output driver and additionally enables the input
  path. **Confirmed fixed on hardware**: the log now reads `reads 1 after
  raising` / `reads 0 after dropping` on every step.
- **The decisive, independent test: `POST /beep-sweep?bypass_amp=1`**, which
  plays the identical staircase through the identical audio path but never
  touches GPIO46 at all - a check that does not depend on the readback being
  trustworthy. Run twice on real hardware, it produced **no sound at all**
  both times, while the normal staircase is clearly audible. That is a
  negative result on the exact test that would have caught an amplifier
  wired to be always-on: if GPIO46 did nothing, a bypassed tone would sound
  identical to a normal one, and it does not.

**Conclusion: the "amplifier only while playing" design is real.** GPIO46
does gate the amplifier in hardware, the amplifier is off except during
`write_tone_step()`'s enabled window, and the battery-drain history is not
being polluted by a continuously-on amplifier. The readback is now correct
and can be relied on for future diagnosis.

Two pieces of evidence, both logged by `audio.cpp`, remain useful for
telling cause 1 and cause 3 apart on any future "no error, but no sound"
report:

- **`ES8311 regs 0x%02x-0x%02x: ...` (several lines, 0x00 through 0x45)** -
  logged once per `POST /beep-sweep` call, before any tone plays. This is
  the evidence for cause 3: it shows the codec's actual clock manager, DAC
  mute, and DAC volume register contents, rather than what `setup_codec()`
  believes it configured.
- **`sweep step N: <freq> Hz at <vol>% (amplifier enabled|bypassed)`** -
  logged once per step, so the log and what the operator actually heard can
  be lined up afterward, including which mode each step used.

### `POST /beep-sweep`

Debug-only, same gating as `/beep`. Plays a fixed staircase - 2 kHz at 20%,
30%, 40%, 50% codec volume, then 1 kHz and 3 kHz at 50% (brought down from
an original 20/40/60/80% ceiling once hardware confirmed 80% was too loud) -
each ~400 ms with ~300 ms of silence between steps, amplitude capped at the
same 0.5 ceiling as the normal tone. Runs on its own task (see "Non-blocking
playback" below) - the request returns as soon as the staircase has
*started*, not once it has finished several seconds later.

```bash
curl --fail-with-body -sS -m 15 -X POST "http://$(./scripts/remote.sh ip)/beep-sweep"
```

`?bypass_amp=1` runs the identical staircase without ever raising GPIO46 -
see the verdict above for what this settled:

```bash
curl --fail-with-body -sS -m 15 -X POST "http://$(./scripts/remote.sh ip)/beep-sweep?bypass_amp=1"
```

## Non-blocking playback

`audio_play_tone()`/`audio_play_diagnostic_sweep()` block for the tone's or
staircase's full duration - up to several seconds for a sweep - and the
portal's HTTP server is single-task, so a handler blocked that long made
*every other route* unreachable for as long as it ran, including `/shot`
and `/restart`. That is the same class of problem the OTA handler had, and
it has the same fix: `audio_play_tone_async()` and
`audio_play_diagnostic_sweep_async()` run the existing blocking functions on
their own short-lived task (`xTaskCreate`, same pattern as
`ota::start_pull()`'s `pull_task`) and return as soon as that task exists,
not once it finishes. `POST /beep` and `POST /beep-sweep` call the `_async`
versions and report "Beep started." / "Sweep started." - there is no
"finished" signal over HTTP; the log is still the record of what actually
happened.

**One I2S channel, one codec, no internal locking in either.** Reading
`esp_codec_dev.c` (the managed component): `esp_codec_dev_open()`/`close()`
guard against a redundant call only with a plain, unprotected bool on the
`codec_dev_t` struct - there is no mutex anywhere in it. Two genuinely
overlapping playbacks (a second request reaching this module from a
separate task while the first's task is still running) would race that bool
with no protection at all - a real, if latent, risk worth closing off by
construction rather than leaving as a bug waiting for the right timing.

The fix is a busy guard, not a queue: `audio_play_tone_async()` and
`audio_play_diagnostic_sweep_async()` try to claim a binary semaphore with
`xSemaphoreTake(..., 0)` (non-blocking - either the slot is free right now
or it is not) before creating the task, and the task releases it when
`audio_play_tone()`/`audio_play_diagnostic_sweep()` returns. A second
request while one is already running gets `ESP_ERR_INVALID_STATE` back
immediately (portal.cpp answers `409 Conflict`) - refused cleanly, not
queued and not run concurrently. **Confirmed working on hardware**: a
concurrent request is refused with `409` and logs `audio_play_tone_async
refused: playback already in progress`.

### `i2s_common: i2s_channel_disable(...): the channel has not been enabled yet`

This line appears in the log on every single tone, including the very
first one after a fresh boot with no other caller anywhere in sight - a
single-caller, single-task capture that a concurrency race cannot explain
(an earlier version of this document wrongly attributed it to the busy-guard
scenario above; that explanation did not survive checking against a
single-request, first-boot log capture, and is corrected here).

The real mechanism, found by reading `audio_codec_data_i2s.c`'s
`_i2s_data_set_fmt()` (the function `esp_codec_dev_open()` calls, via
`data_if->set_fmt()`, before it calls `data_if->enable(..., true)` to
actually start the channel): for an output-only device like this one, it
unconditionally calls `_i2s_drv_enable(i2s_data, true, false)` - i.e.
`i2s_channel_disable()` on the TX channel - as a defensive "make sure it's
disabled before reconfiguring the slot/clock" step, on *every* call, not
just the first. Since this module's own `write_tone_step()` always leaves
the channel disabled (`I2S_CHAN_STATE_READY`) between tones, that defensive
disable call finds an already-disabled channel on every single open - which
is exactly what `i2s_channel_disable()` in `i2s_common.c` logs an error
for (`ESP_GOTO_ON_FALSE(handle->state > I2S_CHAN_STATE_READY, ...,
"the channel has not been enabled yet")`) rather than silently accepting.

**This is benign.** `_i2s_data_set_fmt()` discards this call's return value
before the reconfigure and re-enable that follow it, which both succeed
normally regardless - the tone plays correctly either way, as every hardware
test has shown. It is upstream noise from a third-party managed component's
own defensive-programming pattern, not a lifecycle bug in this module: every
one of this module's own open/close call sites is individually balanced
one-to-one (`audio_play_tone()`, `audio_play_diagnostic_sweep()`), and this
call happens *inside* `esp_codec_dev_open()` itself, on the disable side, a
single millisecond into every request - not from anything this module's own
code does before or after.

**Left alone deliberately, not suppressed.** Silencing the `i2s_common` log
tag (or this specific message) to make it disappear would also hide a
genuine I2S fault reported through the same channel later. If this ever
needs to go away cleanly, the fix belongs upstream in `esp_codec_dev` (skip
the defensive disable when the channel is already `I2S_CHAN_STATE_READY`),
not in this module.

## Verifying the tray's speaker indicator

Non-blocking playback is what makes this possible at all: with the old
blocking handler, `/shot` could not be served while a tone played, so there
was no way to capture the icon on screen. Now:

```bash
curl --fail-with-body -sS -m 10 -X POST "http://$(./scripts/remote.sh ip)/beep?vol=0&ms=3000"
curl --fail-with-body -sS -m 10 "http://$(./scripts/remote.sh ip)/shot" > /tmp/shot-during.txt
sleep 4
curl --fail-with-body -sS -m 10 "http://$(./scripts/remote.sh ip)/shot" > /tmp/shot-after.txt
```

`vol=0` is deliberate, not incidental: it silences the tone completely while
still opening the codec, raising GPIO46, and toggling this module's tray
indicator exactly as a normal-volume call would - the amplifier enabling
into silence is inaudible, not inert. This is the check to use whenever the
board must not make sound (someone asleep next to it); a `vol` above 0
exercises the identical code path and is only needed to also confirm the
tone is heard.

`ms=3000` gives a 3 s window: the first `/shot`, issued immediately, should
land while the tone is still playing - `audio_init()` registered this
module's icon with `app_core::register_tray_indicator()` once at startup,
and `write_tone_step()` calls `app_core::set_tray_indicator_active(handle,
true)` right before raising GPIO46 - and show the icon; the second, a few
seconds after the tone has ended (`set_tray_indicator_active(handle,
false)`, called unconditionally on every exit path right after GPIO46
drops), should show it gone. The tray updates the icon **in place, on the
LVGL timer's own ~100 ms tick**, the same reserved-object mechanism the
network and battery icons already use - not by waiting for the page to next
fully rebuild. `render_tray()` in `components/ui/render_shared.cpp` reserves
a cell for every *registered* slot (a session-static fact, set once at
module init) regardless of whether it is currently active, and a separate
per-tick pass (`update_tray_indicators()`) toggles each reserved icon's
opacity from the *active* flag.

**Found and fixed: the registry was written correctly and never read in
time.** A third hardware run - `POST /beep?vol=0&ms=4000` then `/shot` -
still showed no icon, but this time the receiving side's diagnostic log
(below) had exactly one line in the whole session: `slot 0 -> inactive`, at
boot, and nothing during or after the tone. That ruled out the drawing code
and the registry itself (registration plainly worked) and pointed at
everything between "audio calls set_tray_indicator_active" and "the tray
notices" - which turned out to be `update_tray_indicators()` living inside
`update_visible_fields()`, itself only called when `(published_updated ||
clock_minute_changed)` in `ui_app.cpp`'s `timer_callback` - true maybe twice
a minute, not every ~100 ms tick the way this function's own comment
claimed. A tone lasting a few seconds routinely starts and finishes entirely
inside the gap between those two events, so the registry's `active` flag
flipped correctly on both sides and nothing on the receiving side ever
polled it in time to notice, log, or draw - success reported, nothing
visible, one level removed from where the first two attempts looked.
**Fixed** by pulling `update_tray_indicators()` out into its own function,
called on every tick unconditionally (see its own comment in
`render_shared.cpp` and the call site in `ui_app.cpp`'s `timer_callback`),
independent of the publish/clock-minute gating `update_visible_fields()`
still uses for everything else.

**The diagnostic log, added specifically because this feature failed
silently on hardware three times before this fix.** Every tick,
`log_tray_indicator_state_changes()` (`components/ui/render_shared.cpp`,
called from `update_tray_indicators()`) walks every registered slot in
`app_core::tray_registry` and logs `tray_indicator slot %d ->
active`/`inactive` under the `ui_tray` tag, but only on a genuine transition
(file-scope "last logged" statics, not once per ~100 ms tick) - and it runs
unconditionally, even before the UI has anything ready to draw into,
specifically so a real board's log can answer "did the flag even arrive
here" before anyone looks at a screenshot to ask "did it draw". This log
alone is what found the bug above: it is receiving-side only, so it could
prove "nothing arrived" but not which half was broken. `write_tone_step()`
now logs the same transition from the *sending* side too (`tray indicator:
requesting slot %d active=true/false`, right next to the
`app_core::set_tray_indicator_active()` calls themselves), and
`set_tray_indicator_active()` itself logs loudly
(`set_tray_indicator_active ignored: invalid handle`) rather than quietly
returning if the handle it was given is invalid - so a future break shows up
as a gap between the two sides' logs on the very first hardware run, not
after three.

**What is verified without hardware, and what still needs a screenshot to
confirm.** Host tests prove `ui::system_tray_layout()`'s geometry - that
each indicator's rect is well-formed, does not overlap, and does not move
the anchored network/battery cells, for one indicator and for several at
once, including which get dropped when the tray is too narrow for all of
them (`tests/host/test_tray_layout.cpp`) - and prove the registry's own
contract: fixed capacity enforced, registration past it refused, an invalid
handle's `set_tray_indicator_active()` a documented no-op (loud, now, not
silent). Confirmed on hardware, from the log evidence above: registration
reaches the registry, and (once the fix above ships) the registry is polled
every tick regardless of what else changed. **Still unconfirmed**: that
toggling a slot's `active` flag actually flips real LVGL canvas opacity on
the real display driver, that the `ui_tray` log line actually appears
alongside the new sending-side log on the next hardware run exactly as
expected, and that the 16x12px procedural bitmap `audio.cpp` builds in
`build_icon_bitmap()` renders as a recognizable "speaker" shape rather than
a garbled or blank rectangle - the I1-format canvas byte layout was derived
by reading LVGL source, not by looking at a rendered pixel. All three remain
screenshot-only claims; the two-request sequence above (with `vol=0` when
the board must stay silent) is what settles them.

## Core touch points

Three, per the module contract's rule 5 - grep for these and that is the
entire footprint this module has on core:

- `main/app_main.cpp` calls `audio::audio_init()` once at startup, right
  after the shared-bus I2C scan, and logs success or failure - a failure is
  never fatal, since this board's primary job is the display.
- `components/wifi_provision/portal.cpp` registers `POST /beep` and
  `POST /beep-sweep`, gated by the same `#ifndef NDEBUG` debug-build guard
  as the existing `POST /restart` route: unauthenticated, disabled in
  release builds, and safe to leave that way because neither can corrupt
  anything and the board has no cable to press a button over.
- `audio_init()` calls `app_core::register_tray_indicator()` once, with a
  16x12px 1-bit bitmap this module builds procedurally
  (`build_icon_bitmap()` in `audio.cpp`), and `write_tone_step()` calls
  `app_core::set_tray_indicator_active(handle, true)` right before raising
  GPIO46 and `set_tray_indicator_active(handle, false)` right after it
  drops, on every exit path. Both are direct calls into `app_core`'s
  `tray_registry.hpp` - no handler indirection, unlike `ota::
  set_progress_handler`'s function pointer for `wifi_provision::set_ota`,
  because `app_core` has no dependents of its own that a direct call back
  could ever be circular with.

  The registry (`app_core::tray_registry`, `kMaxTrayIndicators = 4`) is
  core's, and any module may call it directly: a module registers once, as
  data - a bitmap plus width/height, not code - and core renders whatever
  it is handed onto a generic LVGL canvas, with no per-module knowledge or
  switch anywhere in `app_core` or `ui`. This module only ever calls
  `register_tray_indicator()`/`set_tray_indicator_active()` with its own
  handle; a second source of tray activity later (a future AirPlay module,
  say) registers its own bitmap and gets its own handle, with no change to
  this module's call sites or to core.

  A short `/beep` can still start and finish between two ~100 ms ticks and
  never surface on the tray at all - that is a gap in how often the render
  tick runs, not staleness (the registry's `active` flag is read fresh on
  every tick, see `render_shared.cpp`'s `update_visible_fields()`). The
  indicator is for activity that persists across a tick or two, an alarm or
  streamed audio, not a diagnostic click test.

## Triggering it remotely

The board has no cable attached in normal operation, so the trigger is HTTP,
the same way `scripts/remote.sh restart` reaches `POST /restart`:

```bash
curl --fail-with-body -sS -m 10 -X POST "http://$(./scripts/remote.sh ip)/beep"
```

That starts the default 2000 Hz, 300 ms tone at the compiled-in (or
last-set) volume and returns immediately, once playback has *started* - see
"Non-blocking playback" above. Every parameter is an optional query string
override, so a volume or frequency experiment never needs a rebuild:

```bash
curl --fail-with-body -sS -m 10 -X POST "http://$(./scripts/remote.sh ip)/beep?vol=70&freq=2000&ms=400"
```

`vol` (0-100) is applied via `audio_set_volume()` and persists as the new
default for later calls that omit it; `freq` and `ms` are clamped to
`audio_play_tone()`'s existing safe ranges (`kMinFrequencyHz`-`kMaxFrequencyHz`,
`kMinDurationMs`-`kMaxDurationMs` in `audio.cpp`) rather than duplicating
those limits in the HTTP layer. A request while a tone is already playing
gets `409 Conflict` back rather than being queued or run alongside it.
