# AirPlay and light sleep on one board, 2026-08-26

**Status: both shipped and working together. Discovery confirmed from an
iPhone.**

Turning `CONFIG_AIRPLAY_ENABLE` on for the first time after automatic light
sleep shipped surfaced three separate problems in a row, none of which was the
one predicted. Written down because the wrong prediction cost the most time and
the real constraint is invisible from the source.

---

## 1. The wrong prediction, recorded first

The board stopped appearing in AirPlay, and the hypothesis was that light sleep
was dropping the multicast that mDNS discovery needs - a classic, and the
Kconfig help for this very module even says AirPlay "disables Wi-Fi power save
while a session is open", with discovery necessarily happening before any
session exists.

**It was wrong, twice.** AirPlay was not running at all, so nothing was ever
advertised; and once it did run, discovery worked from an iPhone with automatic
light sleep enabled. On this board, at this AP's DTIM, multicast survives the
sleep. That result is worth keeping precisely because the opposite is what
everyone expects.

The discipline that caught it: look at the log before designing the fix. One
`grep -i airplay` said `raop_init failed` at boot, which no amount of reasoning
about multicast would have produced.

---

## 2. The real constraint: contiguous internal RAM

`raop_create()` takes **13,468 bytes of contiguous internal RAM** in one
allocation - `raop_ctx_s` embeds the RTSP and "search remote" task stacks as
member arrays, so total free is not the number that decides it.

`CONFIG_PM_ENABLE` costs **9,856 bytes of static DIRAM**, measured by building
both ways:

| | DIRAM used | DIRAM remaining |
|---|---|---|
| PM on | 274,547 | 67,213 |
| PM off | 264,691 | 77,069 |

That cost belongs to **esp_pm itself, not to light sleep** - it moves sleep
code into IRAM, which on the S3 comes out of the same SRAM as the heap, and
`light_sleep_enable` is only a runtime argument to `esp_pm_configure()`. So
"DFS only, no sleep" would have hit exactly the same wall. The choice was never
"light sleep or AirPlay"; it was "esp_pm or AirPlay" until the budget was
found.

### What was actually eating the block

Not PM directly. The **mDNS task's stack**, created from internal RAM
immediately before `raop_create()` and splitting the one block it needed:
startup diagnostics showed a 31,744-byte largest block, and by the time raop
asked, 9,216.

Its own component offers SPIRAM instead, and both prerequisites
(`FREERTOS_TASK_CREATE_ALLOW_EXT_MEM`, `SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY`)
were already enabled - so **no vendored code was touched**:

```
CONFIG_MDNS_TASK_CREATE_FROM_SPIRAM=y
```

That alone made it start, but only on some boots - same firmware, different
heap layout, which is worse than failing because it looks fixed. Wi-Fi's
buffers bought the headroom that made it reproducible:

```
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6      # was 10
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16    # was 32
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=16    # was 32
```

Internal RAM free after startup went **42,759 -> 49,435**, and AirPlay came up
on three consecutive boots instead of roughly every other one.

**Both settings are invisible to `idf.py size`.** Wi-Fi buffers are allocated
from the heap at `esp_wifi_init()`, so the static figure does not move at all -
only the board shows the difference. An hour was spent measuring `size` before
that registered.

The cost is Wi-Fi throughput under sustained load, which this board has little
of. Raise them back if an OTA push starts taking noticeably longer, and expect
that to trade against AirPlay starting.

---

## 3. A firmware write must not compete for that RAM

The write is the recovery path for broken firmware. It is the one thing that
must not fail for want of a few kilobytes, and AirPlay resident holds 13.5 KB
of exactly the kind it needs.

`modules/airplay` now registers an `ota` quiesce hook. Measured on hardware,
either side of a push:

```
airplay: quiesced for OTA: receiver torn down (ESP_OK);
         internal RAM free=27967 largest_free_block=13824
ota: quiesced 2 module(s) in 138 ms before the write
```

From ~14 KB free / 7.7 KB largest block to 27,967 / 13,824.

Three details that were decisions, not accidents:

- **The session teardown is shared, not copied.** `release_session()` is called
  both by `RAOP_EVENT_DISCONNECTED` and by the hook, so a write arriving
  mid-song leaves the panel, the amplifier and Wi-Fi power save exactly where a
  normal disconnect would. The two paths drifting apart is the failure that
  would not show up until the day somebody pushes firmware mid-song.
- **Registration order is load-bearing and already correct.** audio registers
  during `audio_init()` and therefore runs first, which is the order the
  amplifier needs: GPIO46 drops before anything stops feeding the codec. See
  `2026-08-20-quiescing-modules-before-an-ota-write.md`.
- **`ota_quiesce.hpp` already claimed this existed.** Its comment said "two
  registrations exist today (modules/audio and modules/airplay)" while
  `modules/airplay` registered nothing. Aspirational text describing an
  unimplemented thing reads exactly like documentation of a finished one.

### The quiesce was being spent on writes that never started

With rollback enabled, `esp_ota_begin()` refuses while the running image is
still pending verification - and it refuses **after** the hooks have run.
Observed: a push sent inside the verification window returned
`ESP_ERR_OTA_ROLLBACK_INVALID_STATE` with AirPlay already torn down, and the
receiver does not come back until a reboot.

A refusal that routine must not cost a working receiver, so `open_slot()` now
asks first and returns the same error code with nothing dismantled. The log
says `Nothing was quiesced`, and verified on hardware, no quiesce lines follow
it.

The receiver still does not come back after a real write, which is deliberate:
a successful write reboots, and a failed one is a situation someone is already
watching.

---

## 4. What it costs

| | Rate | From full |
|---|---|---|
| Before any power work | -2.9 %/h | ~34 hours |
| Light sleep, no AirPlay | -0.30 %/h | ~14 days |
| Light sleep + AirPlay resident | **-0.63 +/- 0.01 %/h** | **~4.7 days** |

About twice the idle drain for an always-listening RTSP task and mDNS
responder. Measured by the estimator's own whole-run fit over 513 slots; an
independent end-to-end figure over 22 hours gave -0.82 %/h, but that window
included several 1.7 MB OTA pushes and heavy log capture, so it is an upper
bound.

---

## 5. What is left

Nothing blocking. Two things deliberately not done:

1. **Moving `raop_ctx_s` to PSRAM.** It would free 13.5 KB and was the obvious
   route, but it means editing vendored upstream, and that struct embeds task
   stacks - a stack in PSRAM that runs while the flash cache is disabled
   crashes, and it would crash during playback rather than at boot. The config
   route above got the same result with none of that risk. This is the
   fallback if the budget gets tight again.
2. **Widening the Wi-Fi buffers back.** Only if a push gets slow, and knowing
   it trades directly against AirPlay starting at all.

**Done looks like:** what it currently does - `airplay=ready` on consecutive
boots, discoverable from an iPhone with light sleep enabled, a firmware write
that quiesces it first, and a refused push that does not.
