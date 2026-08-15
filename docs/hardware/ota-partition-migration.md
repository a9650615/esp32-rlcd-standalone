# OTA partition table migration

Date: 2026-08-16

Scope: partition layout only. No OTA update client, no rollback handler.
This document exists so the layout can be flashed and verified on hardware
before any OTA code is written on top of it.

## What changed

Old table (single factory app, bootloader/table untouched by routine flashes):

```
nvs,      data, nvs,     0x9000,  0x6000
phy_init, data, phy,     0xf000,  0x1000
factory,  app,  factory, 0x10000, 0x300000
```

New table (`partitions.csv`, verified against `gen_esp32part.py`):

```
nvs,      data, nvs,     0x9000,   24K
phy_init, data, phy,     0xf000,   4K
otadata,  data, ota,     0x10000,  8K
factory,  app,  factory, 0x20000,  3M
ota_0,    app,  ota_0,   0x320000, 3M
ota_1,    app,  ota_1,   0x620000, 3M
```

`nvs` keeps its exact offset and size, so saved Wi-Fi credentials survive a
partition-table rewrite by themselves. But writing this table also rewrites
the app at a new offset (`0x20000`, not `0x10000`) — see "What this write
touches" below, because that part does erase NVS unless you scope the write.

`factory` is kept as a native bootloader fallback: if `otadata` is ever blank
or corrupted, the ROM bootloader boots `factory` automatically, with no OTA
code involved. Cost is one extra 3 MiB app slot; this board has roughly 6.9
MiB free after this table, so the cost is not being paid against anything
scarce.

## What this write touches

Unlike the routine `app-flash` used until now (app only, at `0x10000`),
writing a new partition table means writing **bootloader + partition table +
otadata + app** in one pass, because the app partition itself has moved.
`idf.py build` prints the exact command; use it verbatim rather than
retyping offsets:

```bash
cd /Users/birdyo/Projects/esp32-s3-rlcd-4.2/.worktrees/layout-carousel
./scripts/verify-factory-backup.sh
rm -f sdkconfig && ./scripts/idf.sh build
./scripts/idf.sh -p "$PORT" flash
```

`idf.py flash` writes exactly the four ranges below (confirm they match the
build log before trusting them):

```
0x0      build/bootloader/bootloader.bin
0x8000   build/partition_table/partition-table.bin
0x10000  build/ota_data_initial.bin      (blank otadata -> boots factory)
0x20000  build/layout_carousel.bin       (the app, now at the new offset)
```

None of these four ranges overlap `nvs` at `0x9000`-`0xf000`, so NVS content
is not erased by this write. esptool's write-flash hashes each written range
against what it just wrote (visible in its output) — that is the same
per-write verification already used for routine app flashes.

## Verify immediately after flashing

Run each of these and require a pass before doing anything else:

```bash
# 1. Confirm the new table actually landed (offsets/sizes, not stale cache)
python3 .tools/esp-idf/components/partition_table/gen_esp32part.py \
  build/partition_table/partition-table.bin

# 2. Serial boot log: no reset loop, no memory error
./scripts/idf.sh -p "$PORT" monitor
```

In the serial log, require:

1. Clean boot, no reset loop, no memory-corruption message.
2. `flash io: qio` (or the project's documented QIO gate) and 16 MB flash
   detected.
3. 8 MB PSRAM detected and the memory test passing.
4. The app actually running: carousel starts, at least one full page cycle.
5. `uxTaskGetStackHighWaterMark` headroom on the main task same as before
   (>= 2 KiB free) — the app didn't silently relink at a different address.

Then, separately:

6. **Wi-Fi credentials**: reboot without touching the Setup page. If the
   board reconnects on its own and reaches `Connected` with an IP, NVS
   survived. If it drops into Setup mode instead, credentials were lost —
   stop and report this loudly, do not just re-provision and move on.
7. **GPIO0 ROM-downloader recovery still works**: power off, hold BOOT,
   power on, hold BOOT ~1 s, release, confirm the port re-enumerates and
   `esptool --chip esp32s3 -p "$PORT" chip-id` responds. Then power-cycle
   normally (no BOOT held) and confirm the app boots again. Do this even
   though it "should" be unaffected — GPIO0 behavior is boot-mode-pin logic,
   not partition-table logic, but it is the only way out of a bricked board
   and must be checked, not assumed.

## If the board will not boot

Do not run `erase-flash`. Do not guess at partial fixes.

```bash
cd /Users/birdyo/Projects/esp32-s3-rlcd-4.2/.worktrees/layout-carousel
./scripts/verify-factory-backup.sh   # must print "factory backup verified" before continuing

# Enter ROM downloader: power off, hold BOOT, power on, hold BOOT ~1 s, release.
# Re-list ports, then:
esptool --chip esp32s3 -p "$PORT" write-flash 0x0 \
  firmware/backups/waveshare-factory-full-flash-2026-08-15.bin
esptool --chip esp32s3 -p "$PORT" verify-flash 0x0 \
  firmware/backups/waveshare-factory-full-flash-2026-08-15.bin
```

This restores the entire 16 MiB flash, including the bootloader, the old
single-factory partition table, the old app, and NVS exactly as it was on
2026-08-15. Any Wi-Fi credentials saved on the board *after* that date are
not in this backup and will be lost by a full restore — that is the known
cost of this fallback, not a bug in the restore.

After a restore, power-cycle normally and confirm the board boots the old
factory image before doing anything else. Only then attempt the partition
migration again.

## Not in scope here

No `esp_ota_ops` calls, no HTTPS OTA client, no rollback/anti-rollback
Kconfig, no app-side logic reading or writing `otadata`. The `otadata`
partition is present and correctly sized so the bootloader treats it as
valid-but-blank (boots `factory`); nothing yet writes to it. That is the
next slice.
