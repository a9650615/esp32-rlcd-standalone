# ESP32-S3-RLCD-4.2 Handoff

Date: 2026-08-15 (Asia/Taipei)

Audience: Claude or another implementation agent taking over this board.

## Mission

Continue from the working five-page RLCD carousel and implement the standalone device features requested by the user:

1. phone scans a QR code and configures Wi-Fi through the board's own AP/captive page;
2. persist Wi-Fi credentials and reconnect without a proxy;
3. network time plus periodic onboard SHTC3 temperature/humidity updates;
4. direct, no-key intraday Taiwan/US broad-index data where a reliable source permits it;
5. current and seven-day weather;
6. feed all live data into the existing dynamic carousel without reopening the approved layout unnecessarily.

Networking is **not implemented yet**. The current firmware contains deterministic mock market/weather/indoor data and a read-only RTC probe only.

## Work in the correct checkout

The implementation is in a linked worktree, not the main checkout:

```text
Worktree: /Users/birdyo/Projects/esp32-s3-rlcd-4.2/.worktrees/layout-carousel
Branch:   feature/layout-carousel
UI commit: fcff026a220601a6c3596b1cbd6cb6d3abfbbfda
Main checkout: /Users/birdyo/Projects/esp32-s3-rlcd-4.2 at ad70198
```

Start with:

```bash
cd /Users/birdyo/Projects/esp32-s3-rlcd-4.2/.worktrees/layout-carousel
git branch --show-current
git status --short
```

Expected branch is `feature/layout-carousel`. Four untracked host-build directories belong to the existing work session and were deliberately preserved:

```text
build-host-review-red/
build-host-task7-calendar/
build-host-task7-green/
build-host-task7-red/
```

Do not delete them as incidental cleanup and do not commit them.

## Read these before changing hardware or UI

1. `.agents/skills/esp32-s3-rlcd-dev/SKILL.md`
2. `.agents/skills/esp32-s3-rlcd-dev/references/official-development.md`
3. `docs/hardware/first-layout-checklist.md`
4. `docs/superpowers/specs/2026-08-15-rlcd-layout-carousel-design.md`
5. `docs/superpowers/plans/2026-08-15-rlcd-layout-carousel.md`

The first two files are the board development skill and pinned first-party research. Keep the skill updated only with verified, reusable findings.

## Non-negotiable flash and recovery contract

- Connected port at handoff: `/dev/cu.usbmodem1101`.
- Board: ESP32-S3-WROOM-1-N16R8, 16 MiB QIO Flash, 8 MiB Octal PSRAM.
- Full factory backup path: `firmware/backups/waveshare-factory-full-flash-2026-08-15.bin`.
- Required backup size: 16,777,216 bytes.
- Required backup SHA-256: `68db31b92d8a37bd321101d9ffb093bf2f3213d3e0bf111368e9a8f59919650f`.
- Treat the backup as sensitive; it is ignored and must not be committed.
- Run `./scripts/verify-factory-backup.sh` immediately before every write.
- For application-only changes use `./scripts/idf.sh -p /dev/cu.usbmodem1101 app-flash`.
- Preserve bootloader and partition table. Do not use `erase-flash` for connection trouble.
- GPIO0/BOOT remains input plus pull-up. Never drive it or take ownership during early boot.
- PWR is hardware power management, not a general application GPIO.
- If the serial port disappears: power off, hold BOOT, power on, keep BOOT held about one second, then enumerate ports. Do not erase.

The current flashed app binary is 716,960 bytes with SHA-256:

```text
91b4ee79e74014df6bfe296c484baccfa338e3fbef3e2547c06489344c4bba7b
```

It was written only at `0x10000`, and esptool's post-write hash check passed.

A fresh rebuild after commit `fcff026` still produces 716,960 bytes but has SHA-256 `f1d6cc75026f58a8c29db27073aec6eeef0fe614fd1f3caa89558797960f8d79` because the embedded app version changed. That post-commit artifact has **not** been flashed; keep it distinct from the flashed-image evidence above.

## What is already working

- ESP-IDF 5.5.2 and LVGL 9.3.0 project-local toolchain.
- Vendor-derived ST7305 port with 400 x 300 landscape UI.
- QIO 16 MiB Flash and 8 MiB Octal PSRAM boot gates.
- Five-page carousel: Home, Taiwan market, US market, weather, indoor.
- Home dwell 30 seconds; data pages 12 seconds; manual mode pauses auto for 60 seconds.
- Clock Hero home and permanent time mast on data pages.
- PCF85063 read-only probe with compile-time fallback; the connected board often reports RTC absent/invalid.
- KEY/GPIO18 short release = Previous.
- BOOT/GPIO0 short release = Next.
- Both inputs use a 10 ms sample period, 30 ms stable threshold, one event on release, no repeat.
- Transient navigation overlay: `KEY < AUTO > BOOT`.
- 6 px safe canvas, 1 px label padding, minimum label height `font.line_height + 2`.
- 2 px data/history lines; 1 px separators and dotted grids.
- Right tiles use tested, non-overlapping title/value/detail rectangles.
- Unsupported punctuation is replaced by compiled-font-safe ASCII.
- `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`; measured 4,188 bytes free after first render.

The implementation commit is `fcff026 fix: harden RLCD layout and navigation`.

## Evidence and open acceptance gaps

Fresh verification before handoff:

- Host CTest: pass, 0 failures.
- ESP-IDF build: pass; application partition 77% free.
- Factory backup size/hash gate: pass.
- Project skill validation: pass.
- Luna code review: no Critical/Important blocker.
- Serial boot: QIO, 16 MiB Flash, 8 MiB PSRAM, PSRAM memory test OK.
- Four complete automatic five-page cycles observed without reset.

Still require physical confirmation; do not silently mark these complete:

- current post-fix photos of all five pages;
- `°` glyph and small-text stroke quality on the actual RLCD;
- KEY physically moves Previous exactly once;
- BOOT physically moves Next exactly once;
- manual timeout behavior after a physical press;
- ten complete cycles/ten minutes;
- read-only ROM downloader entry test.

Use `docs/hardware/first-layout-checklist.md` as the acceptance record.

## User decisions already made

- The board must work standalone. There is no proxy/backend service.
- The phone should scan a QR code and reach a board-hosted Wi-Fi settings page.
- A physical application gesture should open settings.
- Preserve the firmware recovery opening above all else.
- The user considers the screen layout substantially converged; focus on functionality and avoid broad visual churn.
- Time must remain visible; Home remains clock-dominant.
- Carousel pages can expose whatever data is available; missing data should degrade cleanly.
- Start market support with the current day's broad-market/index trend, avoiding API keys where practical.
- Keep project skill and handoff documentation current as verified pitfalls are found.

## One unresolved design decision

The user has not answered the last question about setup-AP security. The proposed default was a per-device WPA2 password encoded in the QR code, followed by a captive portal. Alternatives are an open setup AP or another provisioning transport.

Keep the clarification short and then record the approved choice. Do not present the proposed WPA2 choice as already approved.

There are three physical buttons, but only two are application inputs: KEY and BOOT. PWR stays under the hardware power circuit. The safest proposed settings gesture is a KEY long press while retaining KEY short = Previous. BOOT long-press ownership should remain unused because GPIO0 is the ROM strap. This gesture is also proposed, not yet approved.

## Recommended next slice

Write and get approval for a short Wi-Fi provisioning spec before implementation. Keep this slice independent of market/weather providers:

1. Pure, host-tested configuration model and form validation.
2. NVS schema for SSID/password and explicit credential clearing.
3. Wi-Fi state machine: saved STA attempt, bounded retry, AP fallback, connected state.
4. SoftAP plus board-hosted HTTP settings page.
5. Captive-portal behavior based on ESP-IDF's local official example.
6. LVGL setup page with a scannable Wi-Fi QR and clear status/error text.
7. Approved KEY gesture to enter/exit setup without changing BOOT recovery behavior.
8. App-only flash and phone-based acceptance before adding SNTP or live providers.

Useful local first-party/reference code already discovered:

```text
.tools/esp-idf/examples/protocols/http_server/captive_portal/
.tools/esp-idf/examples/provisioning/wifi_prov_mgr/
managed_components/lvgl__lvgl/src/libs/qrcode/
```

LVGL QR support exists in the managed LVGL source but is disabled in current config:

```text
# CONFIG_LV_USE_QRCODE is not set
```

The partition table has a 24 KiB NVS partition at `0x9000` and one 3 MiB factory app at `0x10000`; there is no OTA app slot. Preserve that topology unless the user separately approves a partition migration and restore plan.

## Architecture boundary for live data

The UI currently copies one `AppSnapshot` at startup. Do not let Wi-Fi, sensor, or HTTP tasks mutate LVGL objects directly. Introduce a tested update boundary so provider tasks publish value data and the LVGL timer applies it under the LVGL lock. Keep provider/network ownership outside `components/ui`.

The RTC probe currently creates and deletes I2C bus 0 inside `main/app_main.cpp`. SHTC3 shares SDA13/SCL14 with PCF85063, so extract a board-owned shared I2C service before periodic sensor reads instead of creating competing buses.

Suggested order after provisioning is accepted:

1. STA connectivity and SNTP/RTC synchronization.
2. Shared I2C service and periodic SHTC3 acquisition.
3. Snapshot update channel and rerender policy.
4. Weather provider with cache, timeout, stale state, and offline fallback.
5. Taiwan/US index providers only after confirming a direct-device source's format, rate limits, terms, and TLS footprint.

## Reproducible commands

From the linked worktree:

```bash
./scripts/verify-factory-backup.sh

cmake -S tests/host -B build-host
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure

./scripts/idf.sh build

./scripts/idf.sh -p /dev/cu.usbmodem1101 app-flash
./scripts/idf.sh -p /dev/cu.usbmodem1101 monitor
```

After every flash require QIO, 16 MiB Flash, 8 MiB PSRAM, clean first render, at least 2 KiB measured main-stack headroom, no reset loop, every registered page once, and GPIO0 recovery preservation.

## Handoff completion criteria

Claude can consider the next slice complete only when:

- the approved provisioning flow is documented and implemented test-first;
- a phone can use the displayed QR to join the board AP and load the settings page;
- valid credentials persist and reconnect after reboot;
- invalid credentials return to an understandable setup state;
- no proxy or cloud service is required for provisioning;
- KEY/BOOT short navigation and ROM downloader recovery still work;
- backup, build, app-only flash, boot, phone, and display evidence are recorded;
- new verified board pitfalls are added to the project skill and acceptance checklist.
