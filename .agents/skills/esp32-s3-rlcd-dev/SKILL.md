---
name: esp32-s3-rlcd-dev
description: Develop, build, flash, diagnose, and recover projects for the Waveshare ESP32-S3-RLCD-4.2 (ESP32-S3-WROOM-1-N16R8, ST7305 reflective LCD). Use for board bring-up, ESP-IDF or Arduino setup, LVGL/U8g2 display work, onboard peripheral integration, serial/BOOT recovery, factory firmware backup, and safe firmware restore on this exact board.
---

# ESP32-S3 RLCD Development

Develop this board from measured hardware facts and pinned first-party sources. Preserve a verified full-Flash backup before the first write.

## Start safely

1. Confirm the target says `ESP32-S3-RLCD-4.2`; do not reuse this pin map for similarly named Touch-LCD boards.
2. List serial ports before and after connecting the board. Prefer `/dev/cu.usbmodem*` on macOS.
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

## Verify every hardware change

After flashing, capture evidence for:

1. clean serial boot without reset loops or memory errors;
2. detected 16 MB Flash and 8 MB PSRAM;
3. a full display refresh with correct orientation and monochrome mapping;
4. KEY and BOOT application input behavior;
5. I2C discovery/read of the peripherals touched by the change;
6. battery ADC, audio, RTC, or SD smoke tests when those subsystems changed.

For a carousel UI, observe every registered page once and the wrap back to Home. A successful Home render alone does not exercise the other renderers. Treat any stack-overflow message or software-reset loop as a failed flash even when all QIO/Flash/PSRAM gates passed.

Do not claim completion from a successful compile alone. Preserve the first failing log when diagnosing USB, PSRAM, LVGL, or peripheral issues.

## Recover without making damage worse

If the serial port disappears, do not erase Flash. Long-press PWR to turn off, hold BOOT, press PWR to turn on, keep BOOT held for about one second, then enumerate ports again.

Restoring or erasing is destructive. Perform it only when the user explicitly asks, after checking the selected image size and SHA-256 against its manifest.

- Restore this device's complete dump from offset `0x0` with `write-flash`, then use `verify-flash`.
- Restore the pinned Waveshare merged Factory image from offset `0x0`.
- Do not use `erase-flash` merely to fix connection trouble.
- Never use esptool `--force` against unknown Secure Boot or Flash Encryption state.

Use the exact recovery commands and pinned official Factory image hash in [references/official-development.md](references/official-development.md).
