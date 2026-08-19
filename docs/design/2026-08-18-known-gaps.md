# Known gaps, 2026-08-18

Started as a list of things found while getting the charging icon onto the
panel. Those four are now closed; what remains below are the ones found
while preparing the AirPlay bring-up, plus one piece of book-keeping the
charging-icon work left behind.

## Closed

- **`bind_i1_canvas()` ignored the caller's background** — fixed in
  `773ed2f`, along with `render_dither_card.cpp`'s `swatch_canvas()`, which
  was the last place still writing pixels from byte zero. Proving it needed
  LVGL's computed style read back, so there is now an opt-in
  `ui_theme_lvgl_tests` host target (`-DUI_THEME_LVGL_TESTS=ON`, off by
  default) that builds real LVGL for host.
- **`scripts/svg-to-bitmap.py`'s four silent limitations** — fixed in
  `6eec521`. The winding-rule one was worse than recorded: matplotlib's
  `contains_points()` implemented neither even-odd nor nonzero for a
  compound path with a genuine hole, so it was replaced outright rather
  than reconfigured, which also drops the dependency.
- **Silent no-ops instead of build failures** — `static_assert`s added in
  `773ed2f`. See the caveat below; they are narrower than they look.

## The display's SPI flush failed in a burst at startup — fixed

Closed. It was real, but not what this file said it was, and the difference
mattered more than the bug.

Recorded here as continuous: "from about 6.2 s after boot, every ~256 ms, for
as long as the board runs." Measured over net_log on 0.1.2, it was five
failures between 6,648 ms and 7,930 ms and then nothing — silent for the
remaining 60 s of that capture, and silent across a separate 40 s capture of a
board that had been up two and a half hours. The claim that it never stopped
was never true, or stopped being true before anyone re-measured.

The cause was contention, not exhaustion. The errors interleave exactly with
TLS work:

    E 6648  panel_io_spi_tx_color(395): spi transmit (queue) color failed
    I 6657  taiwan refresh ok=1 ...
    E 6997  panel_io_spi_tx_color(395) ...
    I 7000  esp-x509-crt-bundle: Certificate validated
    I 7079  esp-x509-crt-bundle: Certificate validated
    E 7306  E 7614  E 7930
    I 8672  us refresh ok=1 ...

All three HTTPS providers were released by `wait_for_station_ip()` at the same
instant, so their handshakes overlapped. mbedTLS takes handshake buffers from
internal RAM, and `esp_lcd_panel_io_spi.c:394` passes `portMAX_DELAY` to
`spi_device_queue_trans()` — which rules out a full queue, because with that
timeout a full queue blocks rather than returning. The remaining error paths
are all configuration errors that would fail identically from the first frame;
only `setup_priv_desc()`'s `heap_caps_aligned_alloc(..., MALLOC_CAP_DMA)` can
fail as a function of time. Internal RAM measured 75,187 free with a 31,744
largest block after startup, so the pressure was a transient dip during the
handshakes, not a standing shortage.

Fixed by spacing the first fetch of each provider (`kProviderStaggerMs`,
main/app_main.cpp) rather than by finding memory. Nobody perceives weather
arriving eight seconds later. Verified by pushing 0.1.3 over the network:
fetches now land at 5,284 / 10,463 / 17,372 ms and the error count is zero.

Two corrections worth keeping, because both were process failures rather than
technical ones:

- The severity was inherited, not measured. This file called it "the most
  serious thing on this list" and a later reader repeated that without
  checking. The actual impact is about five dropped frames during startup.
- `/shot` was blamed for hiding it. That was right about the mechanism —
  reading the framebuffer cannot tell you what reached the glass — but the
  conclusion drawn from it, that the display might not be updating at all, was
  wrong and was contradicted by simply looking at the board.


## AirPlay will run out of internal DRAM before it runs out of crypto

The previous version of this file predicted the first thing to break would
be the RSA session-key decrypt. Measured, that is wrong, and the reason is
worth keeping: the prediction only looked at `raop_create()` and never
followed the call into `rtp_init()`.

Both figures below were measured through the target compiler
(`xtensa-esp32s3-elf-gcc`) with a deliberate type-mismatch probe, not
computed on the host — pointer and alignment widths differ. `StackType_t`
is `uint8_t` on this target (`portmacro.h:88`, non-SMP FreeRTOS confirmed by
`CONFIG_FREERTOS_SMP` being unset), which is what puts these in the 27 KB
family rather than the 108 KB one.

| When | Needs | Where |
| --- | --- | --- |
| `airplay_init()` | 27,804 contiguous bytes | `raop.c:82`, `MALLOC_CAP_INTERNAL` |
| RTSP SETUP | a further 25,600 contiguous bytes, first block still held | `rtp.c:216`, `MALLOC_CAP_INTERNAL` |

Both structs embed their task stacks as member arrays, which is why the
requirement is contiguous rather than merely available. Against the board's
last boot diagnostic — `free=55,399`, `largest_free_block=31,744` — the
first allocation fits with about 12% headroom and the second one very
likely does not: it would have to come from a different block, and the
largest block is the one the first allocation just consumed.

The PSRAM side is not the problem and is already documented: `raop_core.c:46`
allocates the 540,672-byte RTP jitter buffer from `MALLOC_CAP_SPIRAM` and
passes it into `rtp_init()` as an external pointer, so it is not part of
`rtp_t`'s own footprint. `modules/airplay/README.md`'s "Buffer sizing"
section covers that allocation and does not mention this one at all.

### What the first bring-up actually found

Two things, neither of them the RSA decrypt, and the second one only after
the first was fixed.

**It was never reached, because of a startup-order bug.** `raop_init()`
resolves the station's own address and fails immediately if it is still
0.0.0.0. It was called about 95 lines before `wifi_provision::start()`, so
AirPlay could not have worked in any build that shipped. Fixed in `652eb91`
by moving `raop_init()` into a task created after `wifi_provision::start()`
and gated on `wait_for_station_ip()`. Tray registration stayed at the old
call site, because the tray reserves a slot's cell width from `registered`
at the first full page rebuild.

That failure also logged under the wrong name: `ESP_ERR_RAOP_BASE` and
`ESP_ERR_HTTP_BASE` are both `0x7000`, so `esp_err_to_name()` answered for
`esp_http_client`. The log said `ESP_ERR_HTTP_WRITE_DATA` for something
esp_http_client had no part in.

**Then it hit the memory wall - but at a different point than predicted, and
much harder.** The table above compared 27,804 against `largest_free_block`
*after startup*. `raop_init()` now runs later than that, after the monitor
tasks have taken their stacks:

| point | free | largest_free_block |
| --- | --- | --- |
| at boot | 194,047 | 98,304 |
| after display/LVGL/audio | 43,371 | 31,744 |
| where `raop_init()` runs | 14,443 | **5,120** |

So the shortfall is not the ~480 bytes the earlier analysis implied but
about 23 KB, and AirPlay is not what consumed it: `weather_monitor` (16,384),
`taiwan_market_monitor` (8,192) and `us_market_monitor` (8,192) take 32 KB of
internal stacks between the second row and the third. `update_check_task`
already takes its 16,384 from PSRAM via `xTaskCreateWithCaps`, so the
precedent for moving them is in the same file.

The lesson worth keeping: a contiguous-allocation budget is only meaningful
paired with *when* the allocation happens. The original analysis was
arithmetically fine against the wrong snapshot.

### Where an iPhone actually stops, measured

The prediction here was that `RSA_MODE_KEY` - decrypting `rsaaeskey` at
ANNOUNCE - would be the first RSA path to break, on the grounds that it had
never executed. Both halves were wrong.

An iPhone connecting produces this and nothing else:

    got RTSP connection 45
    received OPTIONS
    received OPTIONS      (18 s later)
    received OPTIONS
    received OPTIONS
    disconnected on the other end

It never sends ANNOUNCE. The gate is `RSA_MODE_AUTH` - the `Apple-Challenge`
signature in the OPTIONS response - which happens one method *earlier* than
the prediction. ANNOUNCE is never reached, so `rsaaeskey` is still untested.

The RSA path is also not untested any more, and it works: sending an OPTIONS
with an `Apple-Challenge` header by hand returns a well-formed 256-byte
`Apple-Response` signature. The code signs correctly. It signs with a key
that is not Apple's, so the sender's verification against Apple's public key
fails, and iOS retries three times and gives up. Nothing downstream is
broken; nothing downstream has been reached.

Sending the same OPTIONS *without* `Apple-Challenge` returns a clean 200 OK
with no `Apple-Response`, which confirms at the protocol level what the
source already implied: a sender that omits the challenge never touches the
RSA path and can proceed to ANNOUNCE. `rsaaeskey` is conditional in the same
way. So the unencrypted route is open to a non-Apple sender with no key at
all - and it is the only way to exercise the parts of this module that have
genuinely never run.

### What has never run

Worth stating plainly, because the discoverable-in-the-picker milestone
flatters the real state: not one byte has passed through RTP receive, the
ALAC decoder, or the codec sink. That whole chain - jitter buffer timing,
sample rate, PSRAM buffer read/write, backpressure into
`audio_stream_write()`, whether 384 frames is enough - is untested, and it is
where this module's remaining risk is concentrated.

Obtaining the RSA key does not shorten that work. It decides which senders
can reach it, nothing more.

Ordering for the rest of the bring-up:

1. With the throwaway key, an iOS sender stops at OPTIONS, above. The
   throwaway key does reach `airplay_init()`, so it tested that allocation
   for real, and mDNS, and the RTSP layer.
2. With a real key, or with a sender that omits the challenge, ANNOUNCE and
   SETUP become reachable and the second contiguous allocation - `rtp_t`,
   10,840 bytes since `f928d00` - gets its first test.

If the first allocation fails, `raop_create()` returns `NULL` before
`mdns_service_add()` ever runs, and `raop_core.c` collapses that into the
same `ESP_ERR_RAOP_NETWORK_FAILED` it returns for a failed IP resolution or
socket bind. The symptom is "the device is not in the AirPlay picker",
indistinguishable from mDNS being broken. `0440376` makes that log line
carry `free` and `largest_free_block` so the numbers are visible without
guessing; it deliberately does not assert the cause, because the error code
cannot distinguish the paths.

Three ways out, none taken, because they trade against each other and the
choice is the owner's: shrink the vendored stack sizes (edits upstream
source, so `UPSTREAM.md`'s provenance record needs updating), free internal
DRAM elsewhere (already done once, moving the check and audio task stacks to
PSRAM — how much is left is unknown), or accept that AirPlay does not fit
alongside the current feature set and say so in the module's README.

## The 72 KB core leak did not exist

An earlier version of this file recorded that `modules/README.md`'s
byte-count acceptance test had drifted 72,192 bytes with no explanation, and
guessed that `lv_canvas` had dragged LVGL's draw-buffer and layer machinery
into core.

Both halves were wrong, and the way they were wrong is the useful part. The
acceptance test requires **every** module off; the measurement behind that
figure was taken with `CONFIG_AUDIO_ENABLE=y`, so it counted the audio
module's own ~59 KB as core. Measured correctly — audio and AirPlay both off
— HEAD is 1,541,040 bytes, and the real growth over the recorded baseline is
12,992 bytes, every byte of it attributable to named core changes. LVGL grew
by 20 bytes, so the guess was disproven outright.

Reading a leak into a number nobody had checked, and then writing it down as
a finding, cost more than the measurement would have. The corrected baseline
and a per-contributor breakdown are now in `modules/README.md`.

One real gap did come out of it: rebuilding the exact commit that recorded
1,528,048 produces 1,531,104 instead. Toolchain, ESP-IDF and pinned
dependency versions were all ruled out; the 3,056-byte difference is
unexplained. It does not threaten a test meant to catch tens of KB, but it
means the baseline is only reproducible to about 3 KB, which is now stated
where the number lives.

AirPlay's own incremental cost is unaffected by any of this and still
matches what its README records: +88,288 bytes (86.2 KiB) with
`CONFIG_AIRPLAY_ENABLE=y`, plus 4,420 bytes of static DRAM.

## The new static_asserts are narrower than they appear

`g_tray_indicator_storage`'s assert checks against a duplicated `16x12`
literal rather than importing a module's real icon constants, because
pulling `modules/audio`'s `kIconWidth` into core would break the core/module
decoupling this file's own comments describe. So it pins the documented
claim, not reality: a future module registering a larger icon still gets
caught at runtime by the bounds check, not at build time. That is the
intended trade, but it is not what "static_assert" usually promises.

`g_charging_bolt_bitmap`'s assert is the stronger of the two — it uses the
real compile-time tray battery-cell geometry.

Neither could be extended to `render_dither_card.cpp`: `i1_canvas_stride()`
and `i1_canvas_storage_bytes()` call into LVGL and are not `constexpr`, so
the asserts above use a `constexpr` mirror of LVGL's stride formula instead.
If `LV_DRAW_BUF_STRIDE_ALIGN` ever changes, the mirror reads it by name and
follows; if LVGL changes the formula itself, the mirror silently diverges.
