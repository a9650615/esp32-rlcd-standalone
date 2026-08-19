# Open items, 2026-08-20

Written at the end of the day AirPlay started working from an iPhone. Six
things are open. Each says what is measured, what is inferred, and what
"fixed" would look like, so none of it has to be re-derived.

Ordered by what I would do first, which is not the order they were found in.

---

## 1. An OTA push during playback makes the speaker roar

**Status: mechanism understood, decision recorded, nothing built.**
See `2026-08-20-quiescing-modules-before-an-ota-write.md`.

A firmware push sent while audio was streaming produced loud noise from the
speaker and then failed outright. Found by a peer session because the operator
happened to be listening; firmware was pushed more than thirty times that day
and nobody had an instrument pointed at the speaker during a write.

The mechanism is that the amplifier stays enabled with an undriven input.
GPIO46 has to drop *before* the codec stops being fed, and a naive "stop
writing on quiesce" hook gets that backwards — which is the trap worth
recording, because such a hook looks finished and is not.

`close_stream_locked()` in `modules/audio/audio.cpp` already sequences it
correctly: trailing silence, drain wait sized by sample rate, GPIO46 low, tray
indicator cleared, `esp_codec_dev_close()`. Verified against the code, not
remembered. So the pre-write hook most likely just calls it and waits, rather
than needing new teardown logic.

**Done looks like:** a push started during playback fades out and goes quiet
before the write begins, and the write succeeds.

---

## 2. The battery divider has never been calibrated

**Status: open since before this work. Safety-relevant.**

`CONFIG_BATTERY_CALIBRATION_PERMILLE` does not appear in `sdkconfig` at all, so
the code falls back to 1000 — no trim. Real dividers run several percent off
nominal, which means `battery_overvoltage_warning()` and
`battery_overvoltage_danger()` are comparing against millivolt figures that
are not the cell's actual voltage. On a lithium cell, an overvoltage threshold
that is wrong in the permissive direction is the one that matters.

This needs a person and a multimeter; nothing here can measure it.

**Done looks like:** measure the cell across its terminals, read the reported
millivolts from the tray or the log, and set
`CONFIG_BATTERY_CALIBRATION_PERMILLE = 1000 * multimeter_mV / reported_mV`.
The procedure is already written at the top of
`components/app_core/app_snapshot.cpp`.

---

## 3. Decide which of the day's instruments stay

**Status: all four are in production and printing.**

One capture window contained 317 instrument lines:

```
flush #            59
stream write gap  125
decoded audio     127
drift:              6
```

They are not equal in value:

- **`decoded audio` (rms / zero-crossings, after volume) and `stream write
  gap` earn their place.** They are the only two things in this system that
  distinguish faults which sound identical: RTP inserting silence, an I2S
  underrun, a wrong AES key, and a volume of zero all present as "no sound" or
  "bad sound" and have completely different evidence. Both were decisive
  today.
- **`flush #` has largely done its job.** It found the 157 ms conversion. It
  is worth keeping only until the render format question (item 5) is settled,
  since that is the number that would prove it.
- **`drift:` should stay while the correction is switched off.** It is the
  only thing that would show the error starting to accumulate.

**Done looks like:** a deliberate decision per instrument, not a sweep. Rate
limits or log levels, not deletion, for anything still carrying information.

---

## 4. A constant -127 ms timing offset, unexplained

**Status: measured, bounded, not understood.**

`RAOP_INT_TIMING` in `raop_core.c` computes

```c
error = head_playtime - (now + buffer_duration_ms)
```

Over a 150 s stream this sat flat:

```
evaluation 64   error=-122 ms   window max 133 ms
evaluation 80   error=-133 ms   window max 136 ms
evaluation 96   error=-127 ms   window max 129 ms
```

A constant offset with about ±10 ms of noise. It does not accumulate — that
was checked specifically, because if it did, leaving the drift correction
switched off would trade a stutter for a lip-sync problem that worsens.

It is not new. The old correction saw -47 to -111 ms and never made it
smaller, which is precisely why it fired forever without converging: it was
built to remove drift and what is there is an offset.

127 ms is inside the range where audio lagging video becomes noticeable, so
this may be the residual desync the operator reported. Worth understanding.
Nothing about it argues for switching the old correction back on.

**Done looks like:** either an explanation of where the bias comes from in
that formula, or a demonstration that it is inherent to the sink's own
latency (the silence lead, the I2S DMA depth, the codec) and therefore
correct.

---

## 5. The display flush still costs 28 ms of cache-thrashing CPU

**Status: reduced 6x, root cause untouched.**

157 ms per frame at the start of the day, 26-28 ms now, repeating every
~190 ms. The remaining cost is streaming the 240 KB RGB565 draw buffer out of
PSRAM, which is far larger than the cache both cores share, so a single sweep
evicts whatever the other core was using. That is why bypassing the flush
entirely took codec writes more than 20 ms apart from 74 in 5,632 down to 1 in
1,152.

Converting in 64-row bands with a yield between them reduced it. Codec write
gaps still reach ~15 ms.

**The real fix is rendering at 1 bit.** `CONFIG_LV_COLOR_DEPTH_16=y` today and
`LV_DRAW_SW_SUPPORT_I1` is available. The draw buffers would go from 240 KB to
15 KB, a 16x reduction in the traffic that causes this, and the conversion
would nearly vanish. The current path already thresholds at 0x7fff and throws
the anti-aliasing away, so the visual result should be close to identical.

This changes how every pixel in the UI is produced. It is not a change to make
while chasing a bug, which is why it has not been made.

**Done looks like:** flush conversion in single-digit milliseconds, and codec
write gaps that stay under one 8 ms chunk.

---

## 6. Nobody has listened to the drift change

**Status: on main, supported by numbers, unheard.**

`raop_core.c`'s `RAOP_INT_TIMING` case observes and no longer acts. The
evidence for it is strong — at ±50 ms it fired 46 times in 97 evaluations,
discarding 56-104 ms of music every two seconds, and widening the threshold to
±500 ms produced audio/video desync instead by inserting hundreds of
milliseconds of silence at session start. Both directions measured broken.

But every claim about how it *sounds* is inference. A capture confirms it is
not acting (`grep -a "skipping\|inserting"` returns zero); no one has confirmed
it is good.

**Done looks like:** one person, one track, from the Apple TV app, listening
for both stutter and lip sync.

---

## The pattern worth keeping

Six defects were found on this codebase in one day, across two sessions, and
the striking thing is not that they were intermittent. It is that they were
**invisible**:

- The audio output task's creation result was never checked, so a NULL task
  handle meant only the first session after each boot produced audio, silently.
- `raop_create`'s log printed post-allocation figures in wording that reads as
  a live failure, which nearly cost 13.5 KB of permanent memory spent fixing a
  working path.
- `worst_error` never reset, so it could only grow, and a later capture
  looked like a trend.
- `dmap_parse`'s condition was inverted, so a successful parse and a discarded
  one produced identical silence.
- lwIP dropped UDP packets at the socket, which from outside is
  indistinguishable from a bad radio link. It was blamed on the network three
  times.
- The OTA noise had no instrument at all.

None would have been caught by running more tests, because there was nothing
to assert on. Every one needed an instrument that did not exist yet. That is
the lesson to carry, more than any individual fix above.
