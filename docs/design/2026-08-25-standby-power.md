# Standby power, 2026-08-25

Written the night standby went from "seems short" to a number. The point of
this file is that nobody has to re-derive any of it: what was measured, how,
how much to trust it, what was ruled out and on what evidence, and what is
left.

The short version: standby was costing about 34 hours from full, essentially
all of it a CPU that never idled down. The display - the obvious suspect on a
display board - had already been fixed and is not the cause.

---

## 1. What standby actually costs

Three measurements, in the order they were taken. They are not equally good
and the differences matter.

| When | Method | Result | Trust |
|---|---|---|---|
| Before any change | 69 samples over 34.5 min, cable out, near 4.2 V | -40 mV/h -> **-2.9 %/h**, ~34 h | Weakest |
| DFS only, 1 h in | estimator's 2-hour window, 3.85 V plateau | **-2.48 %/h**, ~40 h | Low |
| DFS only, 13.7 h | end to end, 60% -> 33% on one boot | **-1.97 %/h**, ~51 h | Best |
| Light sleep, 6.6 h | end to end from 4140 mV, cable out, top of curve | **-3.6 mV/h = -0.30 %/h** | Good |

**Why the first is weakest.** It is a 34-minute voltage capture converted to
percent through the steepest part of the discharge curve, where 4060-4200 mV
covers 10%. It is the series kept in `tests/host/test_battery.cpp`, recorded
for a different purpose. It is good enough to say "about 34 hours" and not
good enough to be the baseline for judging anything.

**Why the second is low.** One 2-hour window, taken in the plateau where
3790-3820 mV covers 10% - 3 mV per percent, so voltage noise is amplified into
percent noise by roughly five times what it is near 4.2 V. It was also taken
while a wait loop polled the log every five minutes, and every poll replays a
128 KB ring over TCP, which is real Wi-Fi load charged to the measurement.

**Why the third is the one to use.** 27 percentage points across 13.7 hours of
a single boot, so it averages over everything and does not care where the
curve's breakpoints fall. **-1.97 %/h is the baseline light sleep had to beat.**

**The fourth beat it by about a factor of ten, and the instrument stopped
being able to see it.** 4140 -> 4116 mV over 6.6 hours with the cable out, in
the same 4.2 V region both the -2.9 %/h baseline and this were measured in:
-3.6 mV/h against -40 mV/h. The estimator, independently, reported
`trend=3 0.00%/h known=0` - `PowerTrend::Steady`, meaning the fit came in
under its own 0.4 %/h threshold. Two instruments, one saying -0.30 %/h and the
other saying "below 0.4", agreeing.

Two honest limits on that number. Percent has 1-point resolution, so -2 points
over 6.6 hours carries about +/-0.15 %/h. And every sample is from the top 6%
of the discharge curve, so turning -0.30 %/h into a full-charge-to-empty
figure is an extrapolation across regions this has not been measured in. **The
ratio is the solid part; the right summary is that standby went from hours to
days.** Measuring the total honestly would take a week of running and would
not change any decision.

### The estimator is the instrument, with one caveat

`estimate_runtime()` fits the newest two hours and is handed only the slots
recorded since boot, so the `history: ... %/h` log line an hour after
unplugging is a direct readout. It replaced guessing, and it is what any
future power change should be judged by.

The caveat is real and was seen: four consecutive readings went -1.62, -1.78,
-1.91, -2.01 %/h over twenty minutes at constant load. That is not noise. The
cell was crossing from the 3790-3820 mV segment (3 mV per percent) into
3700-3790 (9 mV per percent), and a piecewise-linear curve makes the fitted
%/h move as its breakpoints pass through the window. **A single window reading
is not the number.** Percent measured end to end over hours is.

---

## 2. Where the power was going

Before 2026-08-25, on an idle board:

| Source | Rate |
|---|---|
| FreeRTOS tick, `USE_TICKLESS_IDLE=n` against `HZ=1000` | 1000 /s |
| LVGL tick `esp_timer`, purely to call `lv_tick_inc()` | 200 /s |
| `lvgl_task`, delay floored at 50 ms | 20 /s |
| battery ADC (slope sampler) | 0.2 /s |
| `net_time`, indoor sensor | 1 /min each |

with `CONFIG_PM_ENABLE=n`, so 160 MHz was pinned and none of those wakeups
could even be cheap. **The longest idle window on the board was one
millisecond.**

### Ruled out, with the evidence, so this is not re-investigated

- **The panel.** `f98824f` and `d83b310` took idle to 0.10-0.19 flushes/sec -
  a handful a minute. The remaining one-flush-per-second happens only during
  AirPlay playback, and `CONFIG_AIRPLAY_ENABLE` is absent from this build.
- **A backlight.** There is none. `board_pins.hpp` carries exactly one
  power-relevant pin, GPIO46, and that is the audio amplifier enable, which
  `modules/audio/audio.cpp` deliberately holds low except while a tone plays -
  its own comment cites battery drain as the reason. The panel is genuinely
  reflective.
- **Wi-Fi power save.** Already `MIN_MODEM`; visible in the boot log as
  `pm start, type: 1`, with the listen interval scaled against DTIM 2.
- **Scheduled network work.** Market every 30 min (5 min for Taiwan inside a
  session), weather every 30 min, and `net_time`'s 60 s loop reads the local
  clock without a query. Estimated at 1-3% of the budget - inferred from
  handshake duration, not measured.
- **The sensors.** SHTC3 at 60 s and the battery ADC at 5 s are noise against
  the wakeup rates above.

### Known and deliberately not touched

**Octal PSRAM at 80 MHz, always clocked.** Worth a few mA. The only knob is
40 MHz, which halves the bandwidth of a 240 KB draw-buffer flush that already
costs 26-28 ms of cache thrashing (see `2026-08-20-open-items.md` item 5).
That is the wrong trade; the way to get this back is power-down during sleep,
not a slower clock.

---

## 3. What shipped

- `c7e35ce` - `CONFIG_PM_ENABLE` with DFS and `FREERTOS_USE_TICKLESS_IDLE`.
  Frequency scaling only, no sleep, so nothing had to hold a lock it did not
  already hold. Measured: -2.9 -> -1.97 %/h.
- `88a0b88` - the LVGL tick timer deleted rather than slowed. LVGL 9.3's
  `lv_tick_set_cb()` lets it read `esp_timer_get_time()` on demand, so 40
  wakeups a second (and a handle, a create/start pair with two failure paths,
  and its teardown) stopped existing. An earlier attempt slowed the same timer
  from 5 ms to 25 ms, which was the wrong shape: it traded timing granularity
  for wakeups that did not have to be spent at all.
- `8033366` - `esp_pm_configure()` with `light_sleep_enable`, called before
  `display_init()`.

The property that made the last one a call rather than a project: **nothing in
this firmware holds a power management lock of its own.** The panel goes
through `esp_lcd_panel_io_spi`, which takes the APB lock per transaction inside
the driver and releases it; the sensor bus does the same. Checked by grep, not
assumed.

### Verified on hardware

- `power: DFS 40-160 MHz, automatic light sleep enabled`, then wakeup causes
  alternating **11** (`ESP_SLEEP_WAKEUP_WIFI`, sleeping between beacons) and
  **4** (`ESP_SLEEP_WAKEUP_TIMER`). It is genuinely sleeping and Wi-Fi
  survives it.
- Panel alive through sleep: pages rotate, the intraday chart renders, the
  clock advances. This mattered for `88a0b88` specifically - every UI timer in
  this firmware hangs off one `lv_timer` (`ui_app.cpp`), so a broken tick would
  freeze the panel whole, and **the OTA guard would not catch it**: it checks
  that `lv_timer_handler` ran, not that LVGL believes time passed.
- Remote log, screenshot and OTA push all still work through light sleep.
  `reachable=1`, `image marked valid; rollback cancelled`.

### The instrument it carries, and why it stays

The battery task logs `esp_sleep_get_wakeup_cause()` every publish, where 0
means no sleep has ever ended. Printed every time rather than once on first
success, because **a zero has to be visible as a zero**.

Written first as "retire it once the standby figure is trusted", then kept
deliberately once it was. The reasoning that changed: the figure being trusted
is exactly what makes the instrument cheap to keep and expensive to lose. One
line per 30 s against a board that now discharges at -0.30 %/h costs nothing
measurable, and it is the first thing anyone would want to read the day light
sleep stops working - which is a silent failure, with no symptom other than a
battery that empties in hours again. Recording the decision rather than the
default, per this repo's own rule about instruments.

Expected costs, so they are not a surprise: downlink latency grows to about the
Wi-Fi listen interval (~400 ms at this AP's DTIM), and the RTC slow clock is
the internal RC rather than a crystal, so the sleep guard windows are wider
than they would be with one - it works, it just saves less.

One unexpected cost, which changes how the board is operated: **`remote.sh
logs` is roughly fifteen times slower to reach live.** The retained ring is
replayed over TCP on connect, and TCP throughput is now bounded by how often
the radio wakes. Before, an 8-12 second capture reached the live tail; now it
takes 150-200 s, and a shorter one silently returns *old* lines - the newest
entry it shows is simply wherever the replay got to. That is a measurement
trap in its own right: two consecutive short captures returned tails at
different, non-monotonic uptimes, which looks like a board that has stopped
logging. It has not. Capture for 200 s, and prefer `tail -n` over a `grep`
pipeline, which buffers and loses the race more often.

---

## 4. What is left, in order

1. ~~**Measure it.**~~ Done: -0.30 %/h, about ten times better than the
   baseline, with the estimator reading Steady because the drain is now under
   its detection floor. Eight hours of uptime across the measurement with no
   reboot, no watchdog, and the panel, portal and OTA push all still working,
   which is the stability half of the same check. The wakeup-cause instrument
   stays - see above for why the decision went the other way once the number
   was in.
2. **Wi-Fi `MAX_MODEM` with a longer listen interval.** This was not worth
   doing while the CPU was awake regardless. It is now: one of the two observed
   wakeup causes is the radio. Costs roughly another second of downlink
   latency.
3. **Pause LVGL's refresh timer when nothing is invalidated.** `lvgl_task`'s
   20 wakeups a second is the CPU-side floor now. Real win, real risk of a
   missed invalidation, and pointless to attempt before item 1 says how much
   the current change bought.

**Done looks like:** ~~a standby figure measured with the cable out that is
clearly better than -1.97 %/h, and the wakeup-cause instrument removed.~~
Both settled: -0.30 %/h measured, and the instrument kept on purpose.

Worth saying plainly before anyone spends more on this: at -0.30 %/h the
remaining items are optimising something that is no longer the constraint.
Item 2 and item 3 are worth doing if standby matters again for a reason that
does not exist today. The cell, at this rate, is now out-lived by almost
nothing else on the board.

---

## 5. The bug class worth remembering

Found while reconciling this work: `kChargingSlopeWindow` was 132 samples at a
5 s interval, and a window spans `count - 1` intervals, so it covered 655 s
against `kChargingSlopeMinSpanSeconds = 660`. A full window was refused.
`voltage_is_falling()` **could never once fire on hardware**, so
`charging = level_high && !falling` degenerated to `level_high`, and the fix
that function existed for - a charging icon shown for an hour after the cable
came out - never engaged at all.

Two things about how it hid:

- The failure mode is **a bool that is quietly always false**. There is no
  crash, no log line, no wrong number on the panel. The feature simply is not
  there, while every piece of code implementing it is.
- **Its own test passed on the refusal rather than on the fit.** The test fed
  132 samples 5 s apart, exactly as the sampler does, and asserted "not
  falling" for a held CV voltage. It got `false` from the span guard, never
  reached the arithmetic, and went green.

The guard now is a `static_assert` tying the window, its interval and the
minimum span together, placed in the one file that knows all three
(`main/app_main.cpp`), plus a host test that asserts the span in the units the
sampler feeds. That covers this instance. The general shape - **two constants
that must agree, each individually plausible, failing into silence** - does
not have a general guard, and is worth suspecting whenever a feature seems
present in the code and absent in behaviour.
