# The charging icon outlives the cable, 2026-08-25

**Status: reproduced, mechanism understood, fix known, not built.**

Found by the operator minutes after unplugging a fully charged board: the tray
still showed the charging bolt. Not built yet for one reason - a standby power
measurement was running on that boot, and reflashing resets both the slope
window and the per-boot slot counter, which would destroy it. This is written
so the fix does not have to be re-derived when the measurement lands.

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
