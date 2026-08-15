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
- [ ] Scanning the QR joins the phone to the WPA2-PSK setup AP directly (no separate manual password entry), and the joined network shows WPA2 security in the phone's Wi-Fi settings, not open/none.
- [ ] Android auto-prompts to open the captive portal after joining the WPA2 setup AP.
- [ ] iOS auto-prompts to open the captive portal after joining the WPA2 setup AP.
- [ ] The system tray (time / network status / battery) is legible at normal desk viewing distance on TaiwanMarket, UsMarket, Weather, Indoor, and Setup.
- [ ] Home shows no system tray and keeps its full-height Clock Hero, with no leftover tray-band artifact from an adjacent page.
- [ ] Battery percentage shown in the tray is within a reasonable margin of a multimeter reading taken across the installed cell's terminals.
- [ ] The `CONFIG_BATTERY_CALIBRATION_PERMILLE` calibration step (reported mV vs. multimeter mV, per the spec's Calibration procedure) has been performed at least once on this board and its resulting value recorded.
- [ ] After installing the cell and connecting Type-C first to start the power path, the board continues running normally once Type-C is unplugged (battery-only operation).
- [ ] Watching the panel across a battery sample (30 s cadence) shows no visible full-screen repaint/flash attributable to the battery publish alone.
