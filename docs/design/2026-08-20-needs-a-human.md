# Needs a human, 2026-08-20

Things that are built, measured, and on `main`, whose remaining check cannot be
done from a shell. Each says exactly what to do and what result to look for, so
the check takes a minute rather than a re-derivation.

Nothing here is known to be broken. Everything here is **claimed to work on
evidence that stops short of the thing the user actually experiences** — a
signal that reached the codec is not a sound anyone heard, and a GPIO that went
low is not silence.

---

## 1. A firmware push during playback should be silent

**Built and verified up to the last step.** `7744e11`.

The hook runs, in the right order, in 6 ms:

```
W audio: OTA quiesce: closing any open stream before the write
I ota:   quiesced 1 module(s) in 6 ms before the write
I ota:   writing 0.1.46 image into slot ota_1 (1742656 bytes expected)
```

What that does not establish is that it is *quiet*. The mechanism it relies on
is `audio_stream_close()`'s teardown order, which every stream today exercises,
but "the amplifier was de-energised before the codec stopped" and "the operator
heard nothing" are different claims.

**To check:** start playing to the board, and while it is playing run
`./scripts/remote.sh push`. Listen.

**Pass:** the music fades out and goes quiet before the write starts. **Fail:**
any crack, pop, or roar. The previous behaviour was loud enough that it was
noticed from across the room without anyone looking for it.

---

## 2. A full cell, unplugged, should stop showing the charging icon

**Built, host-tested, and measured — except the case it was written for.**
`149a2aa`.

`voltage_suggests_charging()` compared against an absolute 4150 mV threshold,
which cannot separate a charger at its CV setpoint from a full cell just
unplugged. Measured discharge on this board is −40 mV/hour, so from a full
4210 mV the old signal kept saying "charging" for about an hour.

Now it also requires the voltage not to be falling, fitted over an
eleven-minute window at 5 s sampling.

**Verified on hardware:** with the charger connected and a full window,
`level_high=1 falling=0 charging=1` — so it does not suppress the icon while
genuinely charging, which was the risk this change introduced.

**Not verified:** the case it exists for. The cable came back before any window
had filled on battery.

**To check:** unplug USB and leave the board alone for **fifteen minutes**
(eleven for the window, a few for margin). Then:

```
./scripts/remote.sh logs 20 | grep -a "battery charging="
```

**Pass:** `charging=0 (level_high=1 falling=1, 132 of 132 slope samples)` — note
`level_high` stays 1, because the voltage really is still high; it is `falling`
that has to become 1. **Fail:** `charging=1` with a full 132/132 window.

---

## 3. AirPlay should not stutter, and audio should match video

**Built, measured, never listened to.** `bcc596b` and earlier.

The RAOP drift correction now observes instead of acting. Its two previous
settings were both measured broken:

- At ±50 ms it fired 46 times in 97 evaluations, discarding 56–104 ms of music
  every two seconds. That was the stutter.
- Widened to ±500 ms it stopped that and inserted hundreds of milliseconds of
  silence at session start instead, which is audio lagging video.

The error it now merely reports was measured flat at about −127 ms across 96
evaluations of a 150 s stream, with window maxima of 133/136/129 ms — a
constant offset, not accumulation. That was checked specifically, because an
accumulating error would make switching the correction off the wrong call.

Every claim above is a number. None of them is a listening test.

**To check:** play a track from the Apple TV app — that source stutters where
music apps did not, so it is the harder case — for a couple of minutes. Then:

```
./scripts/remote.sh logs 30 | grep -a "drift:"
```

**Pass:** no stutter, audio matches video, and every `drift:` line reports an
error without the words `skipping` or `inserting`. **Fail:** either of those
words appearing means the observe-only change did not take. Audible stutter
with no correction firing means the cause is elsewhere — most likely the
display flush, item 5 in `2026-08-20-open-items.md`.

---

## 4. The battery divider has never been calibrated

**Not built. Needs a multimeter, and nothing here can substitute for one.**

`CONFIG_BATTERY_CALIBRATION_PERMILLE` does not appear in `sdkconfig`, so the
code uses 1000 — no trim. Real dividers run several percent off nominal, which
means `battery_overvoltage_warning()` and `battery_overvoltage_danger()` compare
against millivolts that are not the cell's actual voltage. On a lithium cell an
overvoltage threshold that is wrong in the permissive direction is the one that
matters.

It also gates item 2 above: the −40 mV/hour discharge rate and the 4150 mV
threshold are both figures about a reading nobody has confirmed is the voltage.

**To check:** measure the cell across its terminals. Compare against the
millivolts in `./scripts/remote.sh logs 20 | grep -a "battery valid="`. Then set

```
CONFIG_BATTERY_CALIBRATION_PERMILLE = 1000 * multimeter_mV / reported_mV
```

The procedure is written at the top of `components/app_core/app_snapshot.cpp`.

---

## Why this file exists separately

`2026-08-20-open-items.md` lists what is still broken. This lists what is
claimed fixed on evidence that stops one step short — and that step is always
the same one: a person perceiving the thing the code is about.

Six defects were found on this codebase in a day and the pattern was that they
were invisible rather than intermittent. The instruments added since have
closed most of that gap, but they measure signals, not experiences. A charging
icon, a stutter, and a pop are all things only someone in the room can confirm,
and leaving them implicitly confirmed is how the last set of silent bugs got in.
