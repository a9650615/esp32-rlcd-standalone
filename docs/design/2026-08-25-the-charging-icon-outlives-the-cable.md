# The charging icon outlives the cable, 2026-08-25

**Status: fixed, and the fix went further than the report.**

Found by the operator minutes after unplugging a fully charged board: the tray
still showed the charging bolt. Recorded rather than fixed at first, because a
standby measurement was running on that boot and reflashing would have reset
the slope window and the per-boot slot counter. The measurement landed, and
then a second request arrived - "plugging and unplugging should update the
display immediately" - which the fix below does not deliver on its own. See
section 6 for the part that does.

The analysis is left as written, because the mechanism is the durable half.

## What was observed

The voltage signal was right, immediately. One publish either side of the
cable coming out:

```
mV=4202 percent=100  charging=1 (level_high=1 falling=0 rising=0)
mV=4140 percent=96   charging=0 (level_high=0 falling=1 rising=0)
```

The trend was wrong, for up to two hours:

```
history: 576 slots (16 this boot), trend=1 11.45%/h
history: 576 slots (17 this boot), trend=1  9.59%/h
history: 576 slots (18 this boot), trend=1  7.90%/h
```

`trend=1` is `PowerTrend::Charging`. It is decaying, not stuck - the charged
slots are ageing out of the estimator's two-hour window one at a time - and it
stays positive until the last of them leaves.

## Mechanism

`ui::battery_is_charging()` (`components/ui/include/ui_data.hpp`) is:

```cpp
return battery.charging || trend == app_core::PowerTrend::Charging;
```

Two signals with very different latencies, combined with OR. `battery.charging`
is a measurement over the last eleven minutes; `trend` is a least-squares fit
over the last two hours. **An OR means the slower one can only ever extend a
"charging" claim and never end one.** The fast signal was correct within a
single 30 s sample and was overridden by a fit describing a cable that is no
longer attached.

Three symptoms, one cause:

- the tray draws the bolt instead of the level bar (`render_shared.cpp`)
- the Settings runtime row says "Charging" instead of a projection
- `battery_percent_trustworthy()` is false, so the Settings battery row shows
  millivolts and "Charging" instead of a percentage

## Why it was not caught

The OR was correct when it was written, and stopped being correct on
2026-08-25 without anyone editing it. Before, the fast signal was
`voltage_suggests_charging()` alone, which only ever fired within ~50 mV of a
full cell - blind through the entire climb from empty. The trend was what
covered that blind spot, so ORing them added real information.

`voltage_is_rising()` now covers the same span directly and in eleven minutes
rather than two hours, which makes the trend's contribution to this decision
almost entirely redundant - and leaves only its cost. The commit that added
`voltage_is_rising()` did not revisit the combining rule. That is the actual
mistake: a new signal was added beside an old one without asking what the old
one was still for.

## The fix

Prefer the measurement when it has one, and fall back to the trend only when
it does not. What "has one" means is already computed:
`battery_voltage_slope()` returns false when the window spans less than
`kChargingSlopeMinSpanSeconds`, which is exactly the state in which
`falling` and `rising` are both false for want of data rather than because the
cell is flat.

So: carry that bit on `BatteryData` - `direction_known`, set by the sampler
from the same call that produces `falling` and `rising` - and make the rule

```cpp
if (battery.direction_known) return battery.charging;
return battery.charging || trend == app_core::PowerTrend::Charging;
```

Eleven minutes of direct measurement beats a two-hour fit whenever both have
an opinion. The trend still answers for the first eleven minutes of a boot,
which is the only window where nothing else can.

Note what this deliberately does *not* do: it does not stop
`PowerTrend::Charging` from suppressing a runtime projection in
`estimate_runtime()`. A positive slope must still mean "no projection", because
projecting an emptying time from a rising fit is how a battery page claims a
negative runtime. That is a different question from "is the cable in".

The residual ambiguity after the fix is honest and bounded: a cell unplugged
under so light a load that its terminal voltage neither drops below 4150 mV nor
falls measurably reads as charging for up to eleven minutes. On this board the
measured unplug step is about -62 mV, so in practice `level_high` alone ends it
within one sample.

**Done looks like:** unplug a full board, and the bolt is gone by the next
publish - within 30 s, not within two hours - with the Settings rows agreeing.
A host test in `test_ui_data.cpp` pinning "fast signal says no, trend says
charging, direction is known" to not-charging.

Both shipped: `direction_known` on `BatteryData`, the rule inverted in
`ui::battery_is_charging()`, and
`a_measured_direction_outranks_a_two_hour_old_trend` in `test_ui_data.cpp`.

---

## 6. "Immediately" needs more than that fix

The fix above makes *unplugging* correct within one 30 s publish, because
unplugging already moves the voltage far enough for `level_high` and `falling`
to change on the same sample. It does nothing for *plugging in*, which still
waits eleven minutes for `voltage_is_rising()` to have a window - and eleven
minutes is precisely the span during which somebody is standing at the board
having just plugged it in, looking at the screen.

Nothing that needs a span of time can answer faster. What can: the step
itself. A charger changes the terminal voltage the instant it is attached or
removed, its current across the cell's internal resistance, and that is the
one piece of evidence about the cable that does not need time to accumulate.
It was measured here in passing, on the unplug that started this report:
**4202 -> 4140 mV between consecutive publishes, 62 mV in one step.**

So the sampler now compares each 5 s reading against the previous one, and a
change of `kCableStepMillivolts` (50 mV) or more is a cable event: it decides
`charging` directly and publishes immediately rather than waiting out the rest
of the 30 s period.

Three things about the shape, each of which was a choice:

- **The threshold sits well above jitter, not close to the measured step.**
  Consecutive raw readings have been seen 31 mV apart with nothing happening.
  Missing a real step costs the eleven-minute behaviour this replaces;
  inventing one costs an icon that is wrong for eleven minutes. Only one of
  those degrades gracefully, so the threshold is biased towards missing.
- **The step's verdict expires rather than being cleared by a condition.** It
  outranks the slope for `kChargingSlopeWindow` ticks - written as the window
  itself, so the two cannot drift apart - which is exactly how long the slope
  needs to refill with samples from after the event. A false step is then
  self-healing, and there is no cross-condition to get wrong.
- **It publishes out of band, which no other tick does.** `set_battery()`
  repaints the panel and a repaint is expensive enough to have caused audio
  stalls. A cable event is rare; every other tick still waits its turn.

This is the second attempt at a step detector. The first, during the merge
that started this work, was a peak/valley hysteresis latch, and it was
correctly rejected: its *release* was a 60 mV fall from the peak, which on
this board's measured -0.66 mV/min unplugged discharge takes about ninety
minutes. The difference now is that the step is only the attack. Release is
the slope's job, and the slope is good at it.

**Done looks like:** plug in at any state of charge and the bolt appears
within about five seconds; pull the cable and it goes within about five
seconds.
