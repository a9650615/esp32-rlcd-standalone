# ESP32-S3-RLCD-4.2 First Layout Acceptance

Date: 2026-08-15 (Asia/Taipei)

Port: `/dev/cu.usbmodem1101`

Status: Partial pass; manual visual/button/recovery checks remain open

## Protected artifacts

- Factory full-Flash backup: 16,777,216 bytes
- Factory backup SHA-256: `68db31b92d8a37bd321101d9ffb093bf2f3213d3e0bf111368e9a8f59919650f`
- Flashed application image: 716,960 bytes
- Application image SHA-256: `91b4ee79e74014df6bfe296c484baccfa338e3fbef3e2547c06489344c4bba7b`
- Write scope: factory app partition at `0x10000` only; bootloader and partition table untouched
- Esptool result: post-write data hash verified

## Automated and serial evidence

| Gate | Result | Evidence |
| --- | --- | --- |
| Host logic/UI tests | PASS | 39 cases; CTest 100% |
| ESP-IDF build | PASS | 716,960-byte app; 77% of 3 MiB app partition free |
| Skill validation | PASS | `quick_validate.py`: `Skill is valid!` |
| Code review | PASS | Luna follow-up found no Critical/Important blocker |
| Flash mode | PASS | `qio_mode` enabled; runtime `flash io: qio` |
| Flash | PASS | 16 MiB |
| PSRAM | PASS | 8 MiB at 80 MHz; memory test OK |
| Recovery pin configuration | PASS (code/boot) | `GPIO0=input/pull-up`; no output ownership |
| First render stack | PASS | 4,812 bytes before UI; 4,188 bytes after render |
| Automatic carousel | PASS | Four full five-page cycles; no reset loop |

## RLCD visual changes in this revision

- Labels reserve 1 px padding and at least `font.line_height + 2 px` height.
- Only 14 px small text requests a 1 px outline; larger bitmap fonts are not outlined.
- Data polylines and indoor history use 2 px strokes; separators/dotted grids remain 1 px.
- Right tiles use non-overlapping title/value/detail rows; market tiles no longer draw lines through value text.
- Unsupported punctuation uses ASCII: `40-60` and `KEY < AUTO > BOOT`.
- KEY/GPIO18 maps to Previous; BOOT/GPIO0 maps to Next.

## Manual checks still required

- [ ] Capture current Home, Taiwan, US, Weather, and Indoor pages after this exact flash.
- [ ] Confirm no clipped top/bottom glyph strokes, tofu boxes, stale pixels, or chart discontinuities.
- [ ] Confirm the `°` glyph exists and is intact in the compiled Montserrat bitmap font.
- [ ] Press KEY: exactly one Previous transition and a matching overlay/log.
- [ ] Press BOOT: exactly one Next transition and a matching overlay/log.
- [ ] Confirm 60-second manual timeout returns to Home and auto mode resumes.
- [ ] Observe ten complete cycles/ten minutes without corruption or reset.
- [ ] Enter ROM downloader by holding BOOT during power-on, run read-only `chip-id`, then boot normally.

Do not mark the first layout slice fully accepted until every manual item above is checked. The next functional slice may be developed independently, but it must preserve this recovery and display regression checklist.

## Wi-Fi provisioning

Physical acceptance for `docs/superpowers/specs/2026-08-15-rlcd-wifi-provisioning.md`. Do not tick any item until observed on the physical board.

- [ ] `CONFIG_LV_USE_QRCODE=y` firmware builds and the Setup page renders a QR code (not text-only fallback) on hardware.
- [ ] Setup AP `RLCD-XXXXXX` is open (no password); a phone joins with no password prompt.
- [ ] A phone camera scans the on-screen QR directly into a join prompt.
- [ ] Android auto-prompts to open the captive portal after joining the AP.
- [ ] iOS auto-prompts to open the captive portal after joining the AP.
- [ ] Submitting a valid SSID/password on the settings page saves credentials and the board reaches `Connected` with an IP.
- [ ] Submitting an invalid form re-shows the form with a readable inline error, no crash, no redirect loop.
- [ ] Wrong password: board retries and falls back to the Setup page with a readable error.
- [ ] AP out of range: board retries and falls back to the Setup page with a readable error.
- [ ] Reboot with previously-good saved credentials reconnects with no AP/portal ever starting.
- [ ] Reboot with no saved credentials starts directly in setup mode with no button gesture required.
- [ ] KEY long press (~2 s) enters setup mode from normal operation.
- [ ] KEY long press (~2 s) while already in setup mode exits it back toward normal operation.
- [ ] KEY short press still moves Previous and BOOT short press still moves Next, unaffected by the long-press addition.
- [ ] GPIO0 ROM-downloader recovery (hold BOOT during power-on) still works end to end after this slice.

## System tray and battery

Physical acceptance for `docs/superpowers/specs/2026-08-15-rlcd-system-tray-and-battery.md`. The Wi-Fi provisioning rows above predate the WPA2-PSK setup AP revision (see that spec's Decision 1) and are left unchanged here; the QR/WPA2 rows below supersede them for physical acceptance going forward. Do not tick any item until observed on the physical board.

- [ ] The Setup page QR at its current on-screen size scans reliably from a phone camera at normal arm's-length distance, under normal room lighting, on the reflective (non-backlit) panel.
- [ ] Scanning the QR joins the phone to the open setup AP directly (no manual network pick) and lands on the setup page with the session password already supplied by the QR's query string, so no password is typed by hand.
- [ ] Android auto-prompts to open the captive portal after joining the open setup AP.
- [ ] iOS auto-prompts to open the captive portal after joining the open setup AP.
- [ ] Reaching the setup page without the QR (typing the address directly) still demands the session password, confirming the page gate and not the AP is what protects it.
- [ ] Entering real home Wi-Fi credentials through the phone connects the board and the setup AP shuts down; the credentials survive a power cycle.
- [ ] The system tray (time / network status / battery) is legible at normal desk viewing distance on TaiwanMarket, UsMarket, Weather, Indoor, and Setup.
- [ ] Home shows no system tray and keeps its full-height Clock Hero, with no leftover tray-band artifact from an adjacent page.
- [ ] Battery percentage shown in the tray is within a reasonable margin of a multimeter reading taken across the installed cell's terminals.
- [ ] The `CONFIG_BATTERY_CALIBRATION_PERMILLE` calibration step (reported mV vs. multimeter mV, per the spec's Calibration procedure) has been performed at least once on this board and its resulting value recorded.
- [ ] After installing the cell and connecting Type-C first to start the power path, the board continues running normally once Type-C is unplugged (battery-only operation).
- [ ] Watching the panel across a battery sample (30 s cadence) shows no visible full-screen repaint/flash attributable to the battery publish alone.

## Live-data layout, on the panel

Everything below has been checked as geometry only. None of it has been seen on
the physical reflective panel.

- [ ] Each of the four weather silhouettes is distinguishable from the others at desk distance, and the drawn icon matches the condition text beside it.
- [ ] Home's single tall tile reads as intentional at full height, with its content visibly centred rather than drifting toward the top.
- [ ] The tray's page dots show the right count and mark the current page, including when auto-rotation skips a page.
- [ ] A `NO DATA` placeholder is legible and unmistakably not a number, on both a market page and the weather page.
- [ ] The stacked high/low temperatures do not collide with each other or with the tile's detail line.
- [ ] `NO INTRADAY DATA` on the Taiwan page reads as a deliberate state, with no chart, grid, or axis drawn behind it.
- [ ] The IP-geolocated city on the weather page is actually where the board is. If it is not, note the correct coordinates: the override setter exists but has no settings-page field yet.

## OTA rollback guard

The guard itself cannot be exercised from a `factory` boot: that slot has no
otadata state, so it correctly reports `readable=0 pending_verify=0` and
exits. Everything below needs an image actually written to `ota_0`.

- [x] Factory boot leaves the guard inert: `ota guard: slot=factory readable=0 pending_verify=0`, no rollback attempted. (Observed 2026-08-16.)
- [ ] An image written to `ota_0` boots as PENDING_VERIFY, the panel shows `VERIFYING UPDATE`, and the guard logs the LVGL loop counter advancing and marks it valid.
- [ ] After that confirmation, a power cycle stays on `ota_0` rather than reverting.
- [ ] An image deliberately broken so the LVGL loop never turns is rolled back by the guard within ~35 s without any button press, and the board returns on the previous slot showing `UPDATE ROLLED BACK`.
- [ ] A startup that reaches `fatal_loop` while PENDING_VERIFY rolls back instead of halting, and a `factory` boot reaching `fatal_loop` still halts for diagnosis rather than boot-looping.
- [ ] `DO NOT POWER OFF` and the phase text are legible at desk distance on the panel, and no tray or page dots appear on the OTA page.
- [ ] KEY and BOOT do nothing at all while the OTA page owns the screen, including the KEY long-press setup gesture.

## OTA partition table migration

Physical acceptance for `docs/hardware/ota-partition-migration.md`. Layout-only
change: no OTA client or rollback code exists yet. Do not tick any item until
observed on the physical board.

- [ ] `./scripts/verify-factory-backup.sh` passes before the new table is written.
- [ ] `rm -f sdkconfig && ./scripts/idf.sh build` succeeds and prints the new six-partition table (`nvs`, `phy_init`, `otadata`, `factory`, `ota_0`, `ota_1`).
- [ ] The generated `sdkconfig` still shows `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, `CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`, and `CONFIG_LV_USE_QRCODE=y` after the regeneration, diffed against the prior `sdkconfig`.
- [ ] The full four-range write (`bootloader`, `partition-table`, `ota_data_initial`, `app`) completes with esptool's post-write hash verified for each range.
- [ ] Clean serial boot after the write: no reset loop, no memory error.
- [ ] 16 MB flash and 8 MB PSRAM still detected; QIO gate still passes.
- [ ] The app runs and completes at least one full carousel cycle from the new `factory` slot at `0x20000`.
- [ ] Previously saved Wi-Fi credentials are still present: the board reconnects on its own without entering Setup mode.
- [ ] GPIO0 ROM-downloader recovery still works end to end (hold BOOT during power-on, `chip-id` responds, then normal boot resumes).
- [ ] `python3 .tools/esp-idf/components/partition_table/gen_esp32part.py build/partition_table/partition-table.bin` output matches `partitions.csv` exactly, confirming no stale cached table was flashed.
