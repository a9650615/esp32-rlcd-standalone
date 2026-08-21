# System Tray Icon Replacement Design

**Date:** 2026-08-21  
**Status:** Approved for implementation planning

## Goal

Replace the hand-drawn Audio, AirPlay, and Wi-Fi tray icons with existing icons from Phosphor Icons Bold. Give these indicators one shared `20×20 px` system tray specification.

The battery remains the current `30×14 px` status component. It is not part of the fixed icon specification because it also displays a continuous level and charging state.

Weather icons are a separate later stage.

## Icon source

Use Phosphor Icons Core at commit:

`2b75f3ad12b420c9504ef05df8d2564a28f8500e`

Phosphor Icons uses the MIT License. Vendor these source files:

- `assets/bold/speaker-high-bold.svg`
- `assets/bold/airplay-bold.svg`
- `assets/bold/wifi-high-bold.svg`
- `assets/bold/wifi-slash-bold.svg`

Store the SVG files in `components/ui/assets/`. Record each upstream path, the pinned commit, and the MIT license in `components/ui/UPSTREAM.md`.

The four SVG files use a common `256×256` viewBox and one filled path. This keeps their visual weight consistent and fits the existing offline rasterizer.

## System tray specification

Add one shared application-core constant for transient and network icons:

- Width: `20 px`
- Height: `20 px`
- Format: tight-packed I1
- Bit order: row-major, MSB-first
- Tight stride: `3 bytes` per row
- Bitmap storage: `60 bytes`
- Registration payload: `TrayIndicatorBitmap::byte_count` must be exactly `60`

Audio, AirPlay, and both Wi-Fi states use this specification. Their full Phosphor viewBox scales into the same `20×20 px` bitmap without hand-edited pixels. Every registry caller supplies the array's `sizeof(...)` as `byte_count`; the `uint8_t` field is sufficient for this fixed 60-byte payload.

Keep these existing properties:

- System tray height remains `28 px`.
- Existing tray gaps remain unchanged.
- Network and battery remain anchored at the right edge.
- Transient indicators remain to the left of network.
- Battery remains `30×14 px` and vertically centred independently.

Split the current shared height constant so battery height does not change when transient indicators become `20 px` high.

## Asset generation

Use `scripts/svg-to-bitmap.py` offline. Add an `--emit-bytes` output mode for a tight-packed C byte array.

Use these fixed raster parameters for all four icons:

- `--width 20 --height 20`
- `--fit viewbox`
- `--threshold 0.35`
- `--min-stroke 1`
- `--emit-bytes <array-name>`

For every icon:

1. Read the vendored SVG.
2. Fit the full SVG viewBox into `20×20 px`.
3. Supersample and threshold with the fixed parameters above.
4. Emit a checked-in `60-byte` tight-packed I1 array.
5. Save the exact regeneration command beside the generated array.
6. Do not hand-edit generated rows or bytes.

Update the rasterizer documentation that currently says only the charging bolt has been converted. It must list these four tray icons and their fixed parameters.

Normal firmware builds consume the checked-in arrays. They do not require Python, NumPy, SVG parsing, or a runtime vector renderer.

## Component boundaries

### Audio

Replace the procedural speaker generator in `modules/audio/audio.cpp` with the generated `speaker-high-bold` bitmap.

Keep the existing registration and active-state flow. Each `TrayIndicatorBitmap` now declares its generated array length through `byte_count`, using `sizeof(array)`; icon source, storage, and dimensions otherwise remain local to the module.

### AirPlay

Replace the procedural AirPlay generator in `modules/airplay/airplay.cpp` with the generated `airplay-bold` bitmap.

Keep the existing registry handle and session-state calls. Only the icon source, storage, and dimensions change. Registration and state behaviour remain unchanged.

### Wi-Fi

Replace the LVGL circles and bars in `components/ui/ui_theme.cpp` with two I1 canvases:

- `wifi-high-bold` when connected.
- `wifi-slash-bold` when disconnected.

Keep the public `wifi_icon()` and `set_wifi_icon_state()` flow. `wifi_icon()` creates both canvas objects. The state update toggles those canvases and does not rebuild the page.

### Registry and layout

Define the `20×20 px` contract in `components/app_core/include/tray_registry.hpp` so modules and UI use one value.

`register_tray_indicator()` rejects null pixels, a bitmap whose width or height differs from the shared tray icon size, or a `byte_count` other than `kTrayIconBitmapBytes`. Registration failure continues to use an invalid `TrayIndicatorHandle`.

Update `components/ui/include/ui_data.hpp` so:

- Network uses the shared `20×20 px` size.
- Transient indicator cells use the shared `20×20 px` height.
- Battery uses its own unchanged `30×14 px` constants.

Do not change registry capacity, slot priority, tray order, or cheap-update behaviour.

## Data flow

```text
Pinned Phosphor SVG
        ↓ offline rasterizer
Checked-in 20×20 tight I1 bitmap
        ↓
Audio/AirPlay registry or Wi-Fi UI state
        ↓ existing repack_i1_bits()
LVGL I1 canvas storage
        ↓ current display threshold path
1-bit reflective panel
```

There is no runtime SVG decoding and no new icon framework.

## Failure handling

- Unsupported SVG path syntax fails during asset generation.
- Wrong generated byte count fails a compile-time assertion.
- Null, non-`20×20`, non-`60-byte`, or full-capacity registry input returns an invalid handle.
- Existing module logs continue to expose failed registration.
- If an icon is unclear on hardware, replace only its generated bitmap parameters. Do not change the registry or tray architecture.

## Verification

### Automated checks

- Run the rasterizer self-test.
- Add a host test that accepts one valid `20×20`, 60-byte registry bitmap.
- Add host tests that reject null, non-`20×20`, and too-short `byte_count` bitmaps.
- Keep the registry capacity and active-state test meanings and assertions.
- Replace their `1×1` and `3×2` fixtures with valid `20×20`, 60-byte fixtures.
- Update tray layout tests to expect `20×20` network and transient cells.
- Verify battery remains `30×14`.
- Run the complete host test suite.
- Run the complete firmware build.

### Hardware checks

Hardware appearance cannot be approved during the remote stage. When the panel is available, check:

- Audio icon is centred and recognisable.
- AirPlay icon is distinct from Audio.
- Connected Wi-Fi uses `wifi-high-bold`.
- Disconnected Wi-Fi uses `wifi-slash-bold`.
- All three icon families have equal visual scale.
- No icon clips at the `20×20` boundary.
- Battery size and position are unchanged.
- Cheap state updates do not shift adjacent tray cells.

## Expected files

- `components/ui/assets/speaker-high-bold.svg`
- `components/ui/assets/airplay-bold.svg`
- `components/ui/assets/wifi-high-bold.svg`
- `components/ui/assets/wifi-slash-bold.svg`
- `components/ui/UPSTREAM.md`
- `scripts/svg-to-bitmap.py`
- `components/app_core/include/tray_registry.hpp`
- `components/app_core/tray_registry.cpp`
- `components/ui/include/ui_data.hpp`
- `components/ui/include/ui_theme.hpp`
- `components/ui/ui_theme.cpp`
- `modules/audio/audio.cpp`
- `modules/audio/README.md`
- `modules/airplay/airplay.cpp`
- `tests/host/test_tray_layout.cpp`

Update `modules/audio/README.md`, `components/ui/UPSTREAM.md`, and the rasterizer header so none still describe Audio, AirPlay, or Wi-Fi as `16×12` procedural or unconverted icons.

No render page, registry capacity, battery drawing, or weather icon file changes are required.

## Acceptance criteria

- Audio, AirPlay, connected Wi-Fi, and disconnected Wi-Fi derive from pinned Phosphor Icons Bold SVG files.
- Audio, AirPlay, and Wi-Fi use one `20×20 px` tray specification.
- Battery remains `30×14 px`.
- Existing tray ordering and cheap updates remain unchanged.
- Normal firmware builds require no runtime SVG support.
- Host tests and the firmware build pass.
- Hardware appearance remains explicitly pending until someone can inspect the panel.
