# ESP32-S3-RLCD-4.2 Handoff

Date: 2026-08-16 (Asia/Taipei)

Audience: whoever picks this board up next. Everything below was measured on
the physical device unless it says otherwise.

## Where the work is

```text
Worktree: /Users/birdyo/Projects/esp32-s3-rlcd-4.2/.worktrees/layout-carousel
Branch:   feature/layout-carousel
Head:     fb41ce2
```

Start with `git branch --show-current` and `git status --short`. Several
untracked `build-host-*` directories are deliberate scratch and are not
committed; the four named `build-host-review-red`, `build-host-task7-*` predate
this session and should be left alone.

## What the board does now

Standalone, no proxy, no cloud, no companion app.

- Five-page carousel plus a Setup page that is not part of the rotation.
- Wi-Fi provisioning: open setup AP, captive portal, page-level session
  password, credentials in NVS, automatic reconnect.
- Real data everywhere: SNTP time, SHTC3 indoor climate, Open-Meteo weather,
  TWSE Taiwan index, a Yahoo endpoint for the US indices, battery from ADC.
- OTA-capable partition layout and a real version number. **The OTA update
  logic itself is not written yet** — only the layout it needs.
- Network log streaming, compiled out by default.

Boot evidence from the current head:

```text
App version:      0.1.0
flash io: qio, Flash 16777216, PSRAM 8388608
main task stack free after first render=4012 bytes
nvs_store: boot: loaded credentials, ssid=<saved>
indoor valid temp_c=29.7 humidity=52
sta got ip: 192.168.3.121
taiwan refresh ok=1 valid=1 value=45811 intraday=0
us     refresh ok=1 valid=1 value=7786  intraday=1
weather refresh ok=1 valid=1 stale=0 temp_c=26.5
net_time: NTP sync landed: 2026-08-16 02:08:17 local
watchdog / panic / geometry faults: 0
```

Host tests: 150 cases, 0 failures. ESP-IDF build: 51% application-partition
free.

## Flash and recovery contract

This is the part to read before touching anything.

- Port at handoff: `/dev/cu.usbmodem1101`.
- Board: ESP32-S3-WROOM-1-N16R8, 16 MiB QIO flash, 8 MiB octal PSRAM.
- Full factory backup: `firmware/backups/waveshare-factory-full-flash-2026-08-15.bin`,
  16,777,216 bytes, SHA-256 `68db31b92d8a37bd321101d9ffb093bf2f3213d3e0bf111368e9a8f59919650f`.
  Treat it as sensitive; it is ignored and must not be committed.
- Run `./scripts/verify-factory-backup.sh` immediately before every write.
- Application-only changes: `./scripts/idf.sh -p "$PORT" app-flash`.
- **Run `git status partitions.csv` before any build you intend to flash.**
  `app-flash` writes the application at whatever offset the current CSV
  declares while the device keeps the table it was last given. This session
  bricked the board exactly that way — building while an agent was mid-edit
  moved the app from `0x10000` to `0x20000` and produced `No bootable app
  partition` within seconds. Recovery was a full `flash` of all four images,
  not an erase.
- Never `erase-flash` for connection trouble.
- GPIO0/BOOT stays input plus pull-up. Never drive it, never take it early.
- PWR is hardware power management, not an application GPIO.
- **Never burn eFuses.** No Secure Boot, no Flash Encryption, no `espefuse
  burn-*`. This is the only line between recoverable and bricked: the ROM
  bootloader is in silicon and the board uses native USB Serial/JTAG, so a
  corrupt partition table, a half-written application, or entirely blank flash
  all still enumerate and still accept esptool. The reset loop this session was
  recovered from the host with no button pressed. Burning
  `DIS_DOWNLOAD_MODE`, `DIS_USB_SERIAL_JTAG` or their siblings closes that
  permanently and nothing undoes it. Measured unburned on 2026-08-16 and that
  is the state to keep; `espefuse summary` is read-only and safe to check with.

Recovery ladder, cheapest tier first — work it in order rather than escalating
to an erase:

1. `otadata` corrupt → the ROM bootloader falls back to `factory` unaided.
2. App broken, USB enumerating → reflash from the host.
3. USB not enumerating → PWR off, then PWR on while holding BOOT ~1 s.
4. Everything else → restore the full verified backup from `0x0`.

Physical buttons, measured this session and now recorded in the board skill
(the official docs give the functions but never the positions):

| Position | Button | Behaviour |
| --- | --- | --- |
| Left | PWR | short press powers on, long press powers off |
| Middle | KEY (GPIO18) | short = previous page, long ~2 s = enter/exit setup |
| Right | BOOT (GPIO0) | short = next page; held during power-on = ROM downloader |

To identify them on another unit without guessing: press each briefly while
watching serial. KEY and BOOT each log `ui_app: button event=`; PWR logs
nothing because it is not an application GPIO, and a short PWR press on a
running board does nothing.

## Partition table

```text
nvs      data nvs      0x9000    24K
phy_init data phy      0xf000     4K
otadata  data ota      0x10000    8K
factory  app  factory  0x20000    3M
ota_0    app  ota_0    0x320000   3M
ota_1    app  ota_1    0x620000   3M
storage  data littlefs 0x920000   1M
```

Roughly 5.9 MiB of the 16 MiB remains unallocated.

`factory` is kept alongside the OTA slots deliberately: if `otadata` is ever
blank or corrupt the ROM bootloader falls back to it with no custom code, which
is a recovery tier ahead of esptool and GPIO0. `nvs` keeps its original offset
and size, which is why saved credentials survived the migration.

Migration and restore procedure: `docs/hardware/ota-partition-migration.md`.

## Architecture

```text
components/app_core       pure snapshot, page registry, carousel, battery math
components/board_rlcd     display, LVGL port, buttons, shared I2C, SHTC3, battery ADC
components/ui             all rendering; consumes AppSnapshot, knows nothing of networks
components/wifi_config    pure credential/QR/passphrase logic, host-tested
components/wifi_provision esp_wifi, NVS, captive portal; owns the one AppSnapshot
components/net_time       SNTP
components/weather        IP geolocation + Open-Meteo
components/market         TWSE + Yahoo
components/net_log        log streaming over TCP, off by default
main                      startup order and one task per provider
```

Two rules hold this together and both were learned the hard way:

**One publisher.** `wifi_provision` owns the single `AppSnapshot` behind a mutex
and is the only caller of `ui::publish_snapshot()`. Provider tasks call its
setters. A second snapshot owner would race it.

**No `lv_*` outside the LVGL thread.** Provider tasks publish; a 100 ms LVGL
timer applies. A publish that does not change page identity updates only the
labels whose text actually differs — `set_label_text_if_changed` compares
before setting, because `lv_label_set_text` reallocates and invalidates even
for an identical string. Four providers on their own timers make this
load-bearing: an unconditional rebuild on publish already cost a watchdog
lockup once, and on a reflective panel a full repaint is visible.

## Honesty rules the UI enforces

These are the point of the last several days of work, and they are easy to
undo by accident.

- A provider with no data leaves `valid` false and the page renders a NO DATA
  placeholder. Nothing reaches the panel as a number unless it was measured or
  fetched.
- Stale is not absent. A real reading that aged shows its real values with an
  ` OLD` marker rather than being discarded.
- `new_york_weather` stays invalid rather than being fed a copy of the other
  location's reading.
- `MarketData::has_intraday` is false for TWSE, which publishes a daily close
  only. The chart, grid and axis are suppressed rather than drawing a flat line
  from a repeated close — the figures would be real but the shape invented.
- An invalid tile draws no icon. A thermometer glyph asserts a reading exists.
- Clock source labels distinguish `SYNC` (SNTP), `RTC`, and `FALLBACK`
  (compile-time). The old code prefixed `DEMO /` onto all three, including the
  two that were real.
- Auto-rotation skips pages with nothing worth dwelling on. It never removes
  them: manual navigation still reaches every page, and a skipped page still
  renders its placeholder when you get there.

## Where the flash goes

`docs/hardware/binary-size-budget.md`, measured with `idf.py size-components`.
Everything this project authored is about 42 KB, 2% of the image. LVGL is
461 KB and 66 KB of scarce internal RAM; the certificate bundle is ~101 KB and
is kept at full deliberately, because a trimmed bundle fails silently months
later when an issuer rotates.

## Verified this session

- Host tests 150/0 and a clean IDF build.
- Factory backup size and hash gate.
- Boot with the new partition table: QIO, 16 MiB, 8 MiB PSRAM, no reset loop.
- Saved credentials survived the migration; the board reconnected unprompted.
- All four providers returning real values.
- KEY short press, KEY long press into setup mode, BOOT short press.
- QR scanned, WPA2 join, iOS captive portal auto-opened (before the AP was
  changed to open — see below).
- Zero watchdog triggers and zero geometry warnings over multi-minute captures.

## Not verified — do not mark these done

- **ROM downloader entry after the partition migration.** Hold BOOT (right
  button) during power-on and run a read-only `chip-id`. This is the last
  recovery tier and the migration rewrote the region it depends on. It is the
  single most important open item.
- The open setup AP and page-password flow end to end on a phone. The WPA2
  variant was verified before the design changed; the current open-AP plus
  `?pw=` QR has only been exercised from a laptop over the LAN.
- Legibility of the new weather icons, the focused Home layout, the tray with
  page dots, and the NO DATA placeholders on the actual panel.
- Battery calibration. `CONFIG_BATTERY_CALIBRATION_PERMILLE` is still 1000.
  Measure the cell with a multimeter, compare against the `battery ... mV=`
  log line, and set `1000 * multimeter_mV / reported_mV`. **Until this is
  done the overvoltage warning thresholds are not trustworthy**, and note that
  the reading rises while USB is charging, so measure on battery.
- Whether the IP geolocation lands on the right city. If it does not, the
  override setter exists but no settings-page field is wired to it yet.

## Known cosmetic noise

`ui_geometry: right tile has a reserved footer band` logs once per Home
render. The debug-only tile check assumes the old three-tile geometry; Home now
has one tall tile. Harmless, but it should be either fixed or removed rather
than left to train people to ignore geometry warnings.

## Next slices, roughly in order

1. **OTA update logic.** The layout and version number exist; the client does
   not. Use `esp_https_ota` writing into the inactive slot, and enable
   `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` with
   `esp_ota_mark_app_valid_cancel_rollback()` so a bad image rolls back by
   itself. Do not build a separate small updater partition: on this platform an
   updater needs Wi-Fi, lwIP and mbedTLS, which is ~580 KB measured, and it
   would itself be unupdatable. A/B plus `factory` is strictly stronger.
2. **Battery history page and runtime estimate**, in the settings-page style.
   The `storage` littlefs partition exists for exactly this; without
   persistence a history page only ever shows the current uptime.
3. **A settings-page field for the weather location override.**
4. **Fix or delete the stale tile footer check.**

## Reproducible commands

```bash
./scripts/verify-factory-backup.sh

cmake -S tests/host -B build-host
cmake --build build-host --parallel
./build-host/host_tests

./scripts/idf.sh build
./scripts/idf.sh -p /dev/cu.usbmodem1101 app-flash
```

Serial capture without an interactive monitor, which is what every diagnosis in
this session used:

```bash
python - "$PORT" 60 <<'PY'
import sys, time, serial
port, seconds = sys.argv[1], float(sys.argv[2])
s = serial.Serial(port, 115200, timeout=0.2)
s.dtr = False; s.rts = True; time.sleep(0.15); s.rts = False
s.reset_input_buffer()
end = time.time() + seconds
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        sys.stdout.write(chunk.decode('utf-8', 'replace')); sys.stdout.flush()
PY
```

## Traps this board has already sprung

All of these are in `.agents/skills/esp32-s3-rlcd-dev/SKILL.md`; they are
repeated here because each cost real time.

- `sdkconfig.defaults` applies only when `sdkconfig` is first generated. Adding
  an option to an existing project does nothing and the build still succeeds —
  `CONFIG_LV_USE_QRCODE` was silently off while a QR feature "shipped".
- `version.txt` is read only at CMake configure time. An existing `build/`
  keeps the old version through a successful build and a verified flash.
- `LV_ASSERT_MSG` calls LVGL's assert handler, whose default body is an
  infinite loop. With `LV_USE_LOG` off, one out-of-bounds object hung the LVGL
  task forever and the only symptom was a watchdog every five seconds. All
  geometry checks in `components/ui` now log and continue.
- `esp_wifi_connect()` while the station is still associated returns
  `ESP_ERR_WIFI_CONN` and silently ignores the new config. Disconnect first.
- ESP-IDF puts the query string in `req->uri`, so logging the URI would have
  leaked the setup page password. `httpd_uri`'s own logging needed silencing
  too.
- A feature that can fail at runtime and fall back silently needs a log line
  saying which branch it took. Four separate defects in this session were
  invisible for exactly this reason.
