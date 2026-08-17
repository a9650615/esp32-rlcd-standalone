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

Ordering, so a bring-up is not misread:

1. With the throwaway key, ANNOUNCE's RSA decrypt fails before SETUP is ever
   reached, so the 25,600-byte allocation is not exercised. What the
   throwaway key *does* reach is `airplay_init()`, which means it tests the
   27,804-byte allocation for real.
2. With a real key, RSA passes and SETUP is where this is expected to bite.

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

## The `modules/` acceptance test has drifted

`modules/README.md` records 1,528,048 bytes (0x1750f0, dated 2026-08-17) as
the modules-off baseline its byte-count acceptance test compares against.
Measured at `ada6042`, modules off, that number is now **1,600,240** — the
test cannot pass, so it currently proves nothing.

The gap is +72,192 bytes, and it is not explained. It arrived with
`e7f097a`/`ada6042`, whose stated subject is a charging icon and some canvas
fixes; the bitmap itself is 28 bytes. The likely candidate is `lv_canvas`
being linked in for the first time and dragging LVGL's draw-buffer and layer
machinery with it, but that is a guess and has not been checked.

The baseline was deliberately not updated to the new figure. Recording a
number that nobody understands would convert a broken test into a
misleading one. Establish where the 72 KB went first, then update it.

AirPlay's own incremental cost is unchanged and still matches what its
README records: +88,288 bytes (86.2 KiB) with `CONFIG_AIRPLAY_ENABLE=y`,
plus 4,420 bytes of static DRAM.

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
