---
name: esp32-s3-rlcd-dev
description: Develop, build, flash, diagnose, and recover projects for the Waveshare ESP32-S3-RLCD-4.2 (ESP32-S3-WROOM-1-N16R8, ST7305 reflective LCD). Use for board bring-up, ESP-IDF or Arduino setup, LVGL/U8g2 display work, onboard peripheral integration, serial/BOOT recovery, factory firmware backup, and safe firmware restore on this exact board.
---

# ESP32-S3 RLCD Development

Develop this board from measured hardware facts and pinned first-party sources. Preserve a verified full-Flash backup before the first write.

## Start safely

1. Confirm the target says `ESP32-S3-RLCD-4.2`; do not reuse this pin map for similarly named Touch-LCD boards.
2. Resolve the port by MAC, never by remembering a port number: `PORT="$(./scripts/find-board-port.sh)"`. `/dev/cu.usbmodem1101` is assigned in enumeration order, so any other USB serial device can take that name, and a flash aimed at the wrong target is destructive and irreversible. This board is `a4:cb:8f:df:88:d0`, which is also its USB serial number. The helper probes each candidate with `read_mac --after no_reset`, which is read-only and will not disturb whatever is running on the other ports.
3. Run `esptool --port "$PORT" chip-id` and `flash-id`. Expect ESP32-S3, 16 MB Flash, and 8 MB embedded PSRAM.
4. Check for a 16,777,216-byte full dump and its SHA-256 manifest under `firmware/backups/`.
5. If no verified backup exists, run the read-only helper before any build is flashed:

```bash
python3 .agents/skills/esp32-s3-rlcd-dev/scripts/backup_factory_flash.py \
  firmware/backups/factory-full-flash-$(date +%F).bin \
  --port "$PORT"
```

The helper detects Flash size, uses the ESP32-S3 ROM loader with 1 MiB chunks, creates a SHA-256 manifest, and independently rereads three samples. It never erases or writes the board. Treat dumps as sensitive because NVS can contain credentials or device data.

## Select the development path

- Default to **ESP-IDF 5.5.x** for a maintained application, audio, power management, testing, and precise hardware control.
- Use **Arduino-ESP32 >= 3.3.0** for quick experiments or when an Arduino library is decisive.
- Use **LVGL 9.3.0** for a new structured GUI; keep **LVGL 8.3.11** only when extending the matching official example.
- Use **U8g2** for a small monochrome text/icon dashboard with minimal dependencies.
- Do not mix LVGL major-version examples, drivers, `lv_conf.h`, or libraries.

Read [references/official-development.md](references/official-development.md) before changing pins, memory configuration, display transport, audio, SD, power, or recovery behavior. It contains the complete first-party pin map, version matrix, official repo snapshot, and source links.

## Preserve the board contract

- Module: ESP32-S3-WROOM-1-N16R8; Flash QIO 16 MB; PSRAM Octal/OPI 8 MB at 80 MHz.
- Display: ST7305 monochrome reflective LCD, native 300 x 400, normally presented as 400 x 300 landscape.
- Display SPI: SCK 11, MOSI 12, DC 5, CS 40, RESET 41, TE 6; mode 0; no MISO.
- The board has no backlight and no touch controller. Never add a touch driver unless external touch hardware is explicitly added.
- Shared I2C: SDA 13, SCL 14. KEY is GPIO18 active-low; BOOT is GPIO0 active-low.
- GPIO46 enables the speaker amplifier and is also a strapping pin. Set it high only after application startup; never externally force it high during reset/download.
- microSD uses SDMMC 1-bit on CLK 38, CMD 21, D0 39. Do not silently move it onto the display SPI bus.
- Battery sense is GPIO4 / ADC1 channel 3 behind the board's onboard 3x divider. The divider reads a few percent off nominal per unit — do not trust the raw `adc_mv * 3` value as the cell voltage. Expose a calibration knob (this project's `CONFIG_BATTERY_CALIBRATION_PERMILLE`, default 1000/no-trim) so it is tuned per board with a multimeter (`1000 * multimeter_mV / reported_mV`) instead of a firmware edit. Also treat any reading below roughly 2500 mV as "no battery installed/connected," not as a real low-charge percentage — a Li-ion cell never legitimately reports that low, and showing a computed 0-ish percent for an absent battery is misleading.

## Build from official references

Pin upstream work to a commit or release. The researched snapshot is Waveshare commit `eb1f63427d735a22b9c30e22fa63ebddae1834d3`.

For ESP-IDF, start from the smallest official example that exercises the required peripheral. Use `10_FactoryProgram` as a BSP reference, not as the final application architecture. Separate board support (`display`, `i2c`, `audio`, `sd`, `buttons`, `power`) from UI and domain logic.

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p "$PORT" flash monitor
```

Build/flash gates:

- With ESP-IDF 5.5.x, `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y` may legitimately produce `--flash_mode dio` in generated `flash_args`; judge the post-flash serial handoff, not that argument alone. Require **all** exact QIO/Flash/PSRAM log gates in [references/official-development.md](references/official-development.md), and stop on any missing gate, reset loop, or non-QIO runtime.
- A full backup in a main checkout is not automatically present in a linked worktree. Copy/link the ignored `.bin` into the verifier's expected active-worktree relative path before running that verifier, or use the separate absolute-path size/hash check. A manifest alone is not proof that the binary exists.
- A component directory and successful project build do not prove that its sources compiled or linked. Force a clean component target build, capture object/target evidence, then require dependency, final ELF/map or runtime-call, and hardware evidence; unchanged binary size is a warning, not proof.
- A full 400 x 300 LVGL tree rendered synchronously from `app_main` can overflow ESP-IDF's 3584-byte default main-task stack even when host tests and the firmware build pass. Set `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`, log `uxTaskGetStackHighWaterMark(nullptr)` before and after the first render, and require at least 2 KiB of measured headroom plus a clean first carousel cycle. The measured reference for this project is 4108 bytes free after the first render.

For Arduino, configure `ESP32S3 Dev Module`, USB CDC enabled, Hardware CDC/JTAG, QIO 80 MHz, 16 MB Flash, OPI PSRAM, and the official 16 MB partition scheme. Start at 921600 upload baud only if stable; lower it after a reproducible serial error.

## Design for RLCD pixels

Treat the ST7305 output as a 1-bit pixel surface, not a scaled web mockup. Use integer coordinates on the 400 x 300 landscape canvas and keep every object inside the 6 px safe margin.

- For the bundled Montserrat bitmap fonts, give each label 1 px internal padding and a height of at least `font.line_height + 2 px`. A 1 px LVGL text outline may be requested only for the 14 px small-text tier; bitmap glyphs may ignore outline styling, so the real fix for an insufficient stroke is a verified heavier 1-bit font. Do not outline medium, large, or hero numerals.
- Data polylines and mini-history lines are at least 2 px wide. Hairline separators and dotted grids may remain 1 px. Keep data strokes outside every text rectangle.
- Give each right-side tile three non-overlapping rows: title, value, detail. Derive and host-test the rectangles; do not repair overlap by relying on opaque labels or paint order.
- **`font_hero()` is ten digits and a colon.** No letters, no `%`, no space, no decimal point. Only the clock may use it. Everything else renders the missing characters as empty boxes and nothing warns - a missing glyph is neither an overflow nor an out-of-bounds object. This has bitten twice: the sensor page lost its temperature to it, and the OTA page drew `WORKING` as five boxes. Use `font_large()` when a big number needs a unit or a sign.
- Every non-ASCII character must exist in the compiled font, and the build enforces it: `scripts/check-cjk-font.py` runs on every build and fails when a string literal under `components/ui` uses a glyph the generated subset lacks. Add Chinese, run `./scripts/build-fonts.sh`, commit `components/ui/fonts/`. A missing glyph is otherwise invisible - LVGL draws an empty box and says nothing - and this project shipped 32 characters of that before the gate existed.
- Labels are `LV_LABEL_LONG_DOT`, never `LONG_CLIP`. Clipping deletes characters silently, which on a panel that otherwise refuses to show unmeasured numbers is the same class of defect: `Up to date` clipped to `pdate to date` reads as a message rather than as damage. Debug builds log every overflow with its measured and available widths, so a too-narrow box is a line of serial output rather than something to notice by eye.
- A CJK glyph is full-width - exactly 14px at font 14 - against roughly 7px for average Latin. Every box here was sized from Latin metrics, so translated strings meet the edges far sooner. The tight ones are the Setup text column (168px usable) and the market sidebar cell (108px, 66px for a value once the icon gutter is taken).

## Working without a cable

This board is developed remotely: firmware by push OTA, diagnosis by network
log, layout by screenshot endpoint. Nothing below needs USB.

```bash
./scripts/remote.sh ip            # resolve by MAC, not by remembering an address
./scripts/remote.sh logs 60       # stream the log port for 60 s
./scripts/remote.sh shot out/     # PNG of the panel right now
./scripts/remote.sh push          # build/layout_carousel.bin -> the board
```

`push` still needs one press on the board to accept the offer. That prompt is
the only authorisation a push has; do not add a way past it.

Three traps this arrangement has already hit:

- **`timeout` is GNU coreutils and macOS does not ship it.** `timeout N nc ...`
  fails as "command not found" with the whole line quoted back, which reads like
  a syntax error. `nc -w` is not the fix either - it measures idle time, which
  never elapses against a board that logs every few seconds. Background the
  reader and kill it.
- **net_log takes its ring mutex with a zero-tick try-lock**, so a line that
  arrives while the sender holds it is dropped rather than blocking the caller.
  That is right for logging and wrong for the screenshot dump, which emits 157
  lines back to back - it cost exactly one line of a frame, and the decoder
  correctly refused to make a picture of it. The dump yields between lines now.
- **`find-board-port.sh` is a USB tool and stops the application** (see below).
  Never reach for it to answer "where is the board" - `remote.sh ip` reads the
  ARP table and sweeps the subnet if the entry has aged out.

`CONFIG_NET_LOG_ENABLE=y` lives in `sdkconfig.defaults`, not in the component's
Kconfig default, which stays `n`. It streams every log line unauthenticated to
anyone on the LAN. The rule that keeps that survivable predates it and must
hold: the Wi-Fi password and the setup-page password are never logged.

Changing `sdkconfig.defaults` alone changes nothing - ESP-IDF only reads it when
`sdkconfig` does not exist. Delete `sdkconfig` and rebuild, then confirm the
value actually landed in the generated file before believing the build has it.

## Look at what you changed

Debug builds photograph themselves, and the picture comes back over the network:

```bash
./scripts/remote.sh shot out/     # PNG of what is on the panel right now
```

That is `GET /shot`, answered on demand. There is also a once-per-boot dump of
every page into the log stream, which is what to read when the question is
about a page you cannot get the carousel to sit on, or about boot order. Both
emit the same `SHOT <base64>` lines, and `scripts/decode-screenshots.py` reads
either without being told which.

Two things about the boot-time dump that each cost a cycle to rediscover: the
page already on screen at boot does not emit until the carousel brings it back
around, so a capture shorter than ~90 s silently returns a subset that reads as
"those pages are broken"; and `/shot` returns **403 while the setup page is
showing**, deliberately, because that page prints the portal password and a
screenshot route that answered would hand it to the whole LAN.

Use it for anything about arrangement: clipping, overlap, alignment, a value that never arrived, a page still carrying another page's content. Three defects that no geometry check could see were found in the first two captures - a temperature rendered in a digits-only font and coming out as an empty box, a sensor page still drawing a market sidebar, and seven forecast columns all truncated to `Thun...`.

It is not a substitute for looking at the board. The PNG is what was drawn, not what the panel shows: stroke weight, contrast in real light, glyph legibility at distance and ghosting are properties of a reflective display and only a photograph settles them. Screenshots answer "is this laid out correctly", photographs answer "can this be read".

The capture is taken at the LVGL flush and waits for the flush completing the bottom-right corner. A full-screen redraw arrives as several flushes across several handler passes, so reading one tick after the render returns a frame two page transitions stale - which looks like a working tool producing wrong pictures.

So a display change is accepted on both: screenshots for every page, and a photograph of every page checking glyph strokes, contrast, diagonal continuity, stale pixels and a complete carousel wrap.

## Push firmware over the network

The upload endpoint listens whenever the board is online, so nothing has to be
pressed to make a push possible:

```bash
curl -X POST --data-binary @build/layout_carousel.bin http://<board-ip>/ota
```

The board then shows `UPDATE OFFERED`, the pushing machine's address, and
`KEY cancel  accept BOOT`. Press BOOT within five minutes - long enough to
start the push from another room and then walk to the board, which is the
actual situation. Nothing is erased and nothing is written while it waits, so a
rejection or a timeout leaves the running firmware untouched. Verified both
ways: accepted in 12 s, written to `ota_0`, rebooted into it; and left
unanswered, releasing at the timeout with `Not confirmed on the device` and the
running image intact.

That prompt is the authorisation. Do not add a way to skip it - a device that
reflashes itself because a request arrived is one anyone on the LAN can
reflash. The session password is the alternative and only exists during setup
mode.

Two things this arrangement depends on, both easy to undo by accident:

- `constant_time_equal("", "")` is true, and the session password is cleared
  once connected. Every check must go through `portal_password_ok()`, where an
  unset password authorises nothing. Without that, leaving the portal up past
  setup mode opens the Wi-Fi credential form to the whole network.
- The confirmation is a **binary** semaphore with one pending request at a
  time. A counting one banks an answer that arrived with nothing waiting and
  applies it to the next push.

### One downloader, three routes

A push, `POST /ota-url`, and the settings menu's update row all run
`ota::start_pull` into the same `ota::Session`, so they share one set of header
checks, one progress screen and one rollback path. Add a fourth route by
calling `start_pull`, never by writing another download task - the copies drift
and only one of them gets fixed.

The settings row is one row doing two jobs: it checks until something is found,
then installs what it found. It does **not** raise the confirm prompt, and that
is not an oversight - the prompt exists because a push has no other
authorisation, while this install was started by someone holding the board.
Asking them to confirm the button they just pressed adds a press and no
assurance. The offer is cleared on every entry to the menu, so a URL found days
ago is re-checked rather than installed on trust.

After an OTA the board boots from `ota_0`, and `app-flash` still writes to
`factory` - see the trap under "Verify every hardware change". `idf.py flash`
avoids it without otatool: it writes `ota_data_initial.bin` at 0x10000, which
resets the boot slot to factory.

`find-board-port.sh` reads the MAC, which means entering the ROM downloader. It
now hard-resets afterwards; if that ever goes back to `no_reset`, every probed
port is left with its application not running - which presents as a board that
is on USB, flashes fine, and answers neither serial nor network.

## Two buttons, three contexts

The buttons change meaning with what is on screen, which is only usable because the screen says what they currently do - the bottom band carries page dots on a carousel page and a button legend on the settings menu. Keep that pairing: a context that reassigns a button without saying so is worse than one that offers less.

| | KEY (GPIO18, middle) | BOOT (GPIO0, right) |
| --- | --- | --- |
| Carousel | short: previous page, long ~2s: Wi-Fi setup | short: next page, long ~2s: settings menu |
| Settings menu | short: next item, long: leave | short: select |
| Writing flash | ignored | ignored |

Both are active-low and emit once after a debounced release; a long press suppresses its own release so one press never means two things. BOOT long presses are safe despite GPIO0 being the download strap - the strap is sampled at reset, not while running - but GPIO0 must still stay input-only with a pull-up, and must never be driven or initialised early in a way that can block ROM download recovery.

The measured layout values, photo-derived failure modes, and acceptance checklist are in [references/official-development.md](references/official-development.md).

## Verify every hardware change

After flashing, capture evidence for:

1. clean serial boot without reset loops or memory errors;
2. detected 16 MB Flash and 8 MB PSRAM;
3. a full display refresh with correct orientation and monochrome mapping;
4. KEY and BOOT application input behavior;
5. I2C discovery/read of the peripherals touched by the change;
6. battery ADC, audio, RTC, or SD smoke tests when those subsystems changed.

Capture serial non-interactively rather than attaching a monitor, so the log is a file you can grep and quote. Two traps about the interpreter, both of which cost a cycle: a system `python3` generally has no `pyserial`, and `python` is not an `idf.py` action, so `./scripts/idf.sh python foo.py` fails with `No rule to make target 'python'`. Source the environment and use the interpreter it puts on PATH, rather than hardcoding a version-specific path under `~/.espressif`:

```bash
source .tools/esp-idf/export.sh >/dev/null
python capture.py "$PORT" 55
```

For the same reason, invoke esptool as `python -m esptool`: the console script has been named both `esptool.py` and `esptool` across ESP-IDF versions, while the module name has not moved. `scripts/find-board-port.sh` does exactly this.

For a carousel UI, observe every registered page once and the wrap back to Home. A successful Home render alone does not exercise the other renderers. Treat any stack-overflow message or software-reset loop as a failed flash even when all QIO/Flash/PSRAM gates passed.

Do not claim completion from a successful compile alone. Preserve the first failing log when diagnosing USB, PSRAM, LVGL, or peripheral issues.

`sdkconfig.defaults` is applied only when `sdkconfig` is first generated. Adding an option to the defaults file does nothing to an existing `sdkconfig`, and the build still succeeds, so a feature guarded by `#if CONFIG_...` is silently compiled out. This bit `CONFIG_LV_USE_QRCODE`: the build passed, the binary flashed, and the QR simply never existed. After adding any `sdkconfig.defaults` entry, grep the generated `sdkconfig` to confirm the value actually landed; `sdkconfig` is gitignored and regenerating it (delete, rebuild) is safe, but diff the regenerated file against the old one to prove the flash/PSRAM/partition/stack settings survived.

`version.txt` is read by CMake at configure time only. Creating or editing it does nothing to an existing `build/` directory: the build succeeds, the flash succeeds, esptool verifies the hash, and the app descriptor still carries the old version. Delete `build/` to force a reconfigure, then confirm the value in the boot log's `App version:` line before believing it. This is the same shape as the `sdkconfig.defaults` trap above.

`app-flash` writes to the `factory` offset, not to the slot the device is currently booting from. After pointing otadata at an OTA slot - during an update, or with `otatool.py switch_ota_partition` - every subsequent `app-flash` lands in a partition the bootloader will not read, and nothing warns: the write succeeds, esptool verifies its hash, the board reboots, and the old image comes back up looking exactly like a successful flash. Several rounds of "flashed and verified" can pass this way while the change under test never runs. Check `boot: Loaded app from partition at offset` in the boot log against where you wrote, or run `otatool.py erase_otadata` to return to `factory` before iterating with `app-flash`.

Never build or flash while an agent or editor may be mid-edit on `partitions.csv`. `app-flash` writes only the application, at whatever offset the *current* CSV declares, while the device keeps the partition table it was last given. Moving the app from `0x10000` to `0x20000` that way produces `No bootable app partition` and a reset loop within seconds. Recovery is a full `flash` of bootloader, table, otadata and app - not an erase - but the cheaper habit is to run `git status partitions.csv` before any build you intend to flash.

A feature that can fail at runtime and fall back silently needs a log line saying which branch it took. The same session lost a cycle to a QR that was compiled out and to a battery reading that was sampled correctly but never printed, so the documented multimeter calibration procedure had no number to compare against.

## Never burn eFuses

**Do not run `espefuse` in write mode, and do not enable Secure Boot or Flash
Encryption on this board.** eFuses are one-time-programmable: nothing in
software, and no amount of reflashing, can undo a burned bit.

This matters because it is the only line between "recoverable" and "bricked."
The ESP32-S3 ROM bootloader lives in silicon, not in Flash, and this board uses
the chip's native USB Serial/JTAG rather than an external USB-serial bridge. So
a corrupt partition table, a half-written application, or an entirely blank
Flash still enumerates over USB and still accepts esptool — recovery is always
a `write-flash` away. Burning `DIS_DOWNLOAD_MODE`, `DIS_USB_SERIAL_JTAG`,
`DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE`, or `DIS_PAD_JTAG` closes that door
permanently, and Secure Boot or Flash Encryption locks the device to keys that
must then never be lost.

Measured on this unit on 2026-08-16, all unburned and all still writable, which
is the state to preserve:

```text
DIS_DOWNLOAD_MODE                  False (0b0)
DIS_USB_SERIAL_JTAG                False (0b0)
DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE  False (0b0)
DIS_PAD_JTAG                       False (0b0)
SECURE_BOOT_EN                     False (0b0)
SPI_BOOT_CRYPT_CNT                 0b000 (disabled)
```

`espefuse summary` is read-only and safe; use it to confirm the above before
and after any risky work. Any `espefuse burn-*` subcommand is off limits unless
the user asks for it in those words, understands it cannot be reversed, and has
a reason that outweighs losing recovery on a development board. Production
security hardening is not that reason on a board still under development.

Because of this, treat "bricked" claims sceptically and work the recovery
ladder in order rather than escalating straight to an erase:

1. `otadata` corrupt: the ROM bootloader falls back to the `factory` partition
   on its own, with no host involvement.
2. Application broken but USB enumerating: reflash from the host. A reset loop
   caused by a partition-table mismatch was recovered this way, with no button
   pressed.
3. USB not enumerating: long-press PWR off, then press PWR on while holding
   BOOT for about a second to reach the ROM downloader.
4. Everything else: restore the full verified backup from offset `0x0`.

## Recover without making damage worse

If the serial port disappears, do not erase Flash. Long-press PWR to turn off, hold BOOT, press PWR to turn on, keep BOOT held for about one second, then enumerate ports again.

Restoring or erasing is destructive. Perform it only when the user explicitly asks, after checking the selected image size and SHA-256 against its manifest.

- Restore this device's complete dump from offset `0x0` with `write-flash`, then use `verify-flash`.
- Restore the pinned Waveshare merged Factory image from offset `0x0`.
- Do not use `erase-flash` merely to fix connection trouble.
- Never use esptool `--force` against unknown Secure Boot or Flash Encryption state.

Use the exact recovery commands and pinned official Factory image hash in [references/official-development.md](references/official-development.md).
