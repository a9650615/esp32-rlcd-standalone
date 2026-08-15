# RLCD Layout Carousel — First Hardware Slice

Date: 2026-08-15  
Target: Waveshare ESP32-S3-RLCD-4.2  
Status: Awaiting user review

## Goal

Build and flash the smallest useful UI slice that validates the real 400 × 300 reflective display before adding provisioning, sensors, weather, or market networking.

The firmware shall render five mock-data pages, switch them automatically, support manual previous/next navigation, and preserve the ESP32-S3 ROM download path. The first hardware session is for judging typography, information density, refresh behavior, and button ergonomics.

## Scope

### Included

- ESP-IDF 5.5.2 and LVGL 9.3.0.
- Waveshare ST7305 display BSP derived from official commit `eb1f63427d735a22b9c30e22fa63ebddae1834d3`.
- 400 × 300 landscape, monochrome rendering.
- Five pages with deterministic mock data:
  1. Clock Hero home.
  2. Taiwan market intraday trend.
  3. US market intraday trend.
  4. Current and seven-day weather.
  5. Indoor temperature and humidity.
- Automatic carousel and manual previous/next navigation.
- Real clock display from PCF85063 when valid, with compile-time fallback when invalid.
- Serial diagnostics for display, PSRAM, RTC, page changes, and button events.

### Excluded from this slice

- Wi-Fi station or AP provisioning.
- Captive portal and QR code.
- Real SHTC3 readings.
- Real weather or financial network requests.
- NVS configuration schema.
- OTA updates, audio, SD card, and battery operation.

These remain later slices so UI/BSP failures are not confused with networking or provider failures.

## Hardware safety

- Verify the existing 16 MiB factory backup and SHA-256 before flashing.
- Flash a normal project image; do not erase the complete Flash.
- Keep GPIO0 as the BOOT strapping input. Do not add a pull-down, repurpose it during early boot, or attach a long-press action.
- Process BOOT button events only after application startup.
- Leave the physical PWR button under the hardware power-management circuit.
- Use KEY/GPIO18 as the primary application button.
- Confirm that holding BOOT during power-on still reaches the ROM downloader after the test firmware is installed.

## Visual system

### Shared rules

- Use 6–8 px safe margins and one-pixel separators.
- Do not render a persistent footer.
- Show page position as small bottom-right dots.
- Show navigation help as a temporary inverse overlay for two seconds after a button action.
- Vertically center content inside secondary tiles and scale its primary value to use the available height.
- Avoid animation. Replace a page atomically, then let the display settle.

### Clock Hero home

- Make the current time the largest element, using the largest font that fits the measured LVGL bounds rather than a fixed decorative size.
- Do not display seconds.
- Keep date and clock update metadata close to the clock bounds to avoid dead space.
- Use the right column for mock current weather, mock indoor climate, and the currently relevant market summary.

### Data pages

- Keep a permanent Clock Mast at the upper left with clearly readable time and date.
- Give the primary subject about 70–75% of the content width.
- For market pages, devote most remaining height to a real polyline chart; place quote and change in a compact block above it.
- Place three secondary tiles in the right column with vertically centered content.
- For weather, promote rain or an alert to the primary region; for indoor climate, promote temperature and humidity.

## Page model and layout engine

Represent each page with a lightweight registry entry:

```text
PageDescriptor
  id
  kind
  priority
  dwell_seconds
  is_available(snapshot)
  render(snapshot)
```

Use a single immutable `AppSnapshot` for rendering. In this slice it contains mock market, weather, and indoor values plus current time. The UI must not own future network or sensor clients.

The registry shall omit unavailable pages instead of showing empty cards. This behavior is exercised with a build-time demo flag even though all five pages are available by default.

## Carousel behavior

- Start on Clock Hero.
- Dwell on Clock Hero for 30 seconds.
- Dwell on each data page for 12 seconds.
- Advance pages using an LVGL timer so all widget creation and deletion stays on the LVGL thread.
- KEY short press: next page.
- BOOT short press: previous page.
- A manual button action pauses automatic advance for 60 seconds, then returns to Clock Hero and resumes automatic mode.
- Debounce both buttons and accept one event per deliberate press.
- Do not assign a BOOT long-press action.

Dynamic priority is represented in this slice but uses mock conditions. The registry orders a weather-alert page first in a morning scenario, Taiwan market first during a weekday Taiwan session, and US market first during a night scenario. It only recalculates order at the start of a carousel cycle, never in the middle of a displayed page.

## Components

```text
main/
  app lifecycle and demo scenario selection

components/board/
  pinned ST7305 display BSP
  PSRAM checks
  PCF85063 clock access
  KEY and BOOT input events

components/app_state/
  immutable AppSnapshot and deterministic mock data

components/ui/
  theme and fonts
  Clock Hero
  market, weather, and indoor renderers
  page registry, carousel, and temporary navigation overlay
```

The board component must remain independent of UI page content. Page renderers consume only `AppSnapshot` and layout bounds.

## Data flow

```text
RTC or compile-time fallback ─┐
deterministic mock providers ─┼─> AppSnapshot ─> page registry ─> LVGL renderer ─> ST7305
KEY / BOOT events ────────────┘                       │
                                                     └─> carousel state
```

Only the LVGL task mutates UI objects. Button handlers enqueue navigation events rather than directly changing screens.

## Failure behavior

- Missing 8 MiB PSRAM: print a fatal diagnostic and do not start the carousel.
- Display initialization failure: preserve the error on serial and stop UI startup.
- Invalid RTC value: show build time, mark the source as fallback in serial output, and continue.
- Button queue overflow: log a rate-limited warning; do not block the input task.
- Page renderer failure: log the page ID, skip it for the current cycle, and return to Clock Hero if no data page remains.

## Verification

### Build checks

- Build with ESP-IDF 5.5.2 for target `esp32s3`.
- Confirm 16 MiB Flash and 8 MiB Octal PSRAM configuration.
- Keep LVGL at 9.3.0; do not mix LVGL 8 headers or driver glue.

### Hardware acceptance

Run the board for at least ten minutes and capture evidence that:

1. boot completes without reset loops or memory errors;
2. serial reports 16 MiB Flash and 8 MiB PSRAM;
3. all five pages appear without clipping;
4. Clock Hero is readable at desk distance and does not leave excessive unused space;
5. market charts use the available height and secondary tiles are vertically centered;
6. automatic dwell timing follows 30/12 seconds;
7. KEY moves forward and BOOT moves backward exactly once per press;
8. manual navigation pauses auto mode for 60 seconds and returns to Clock Hero;
9. the display remains stable for ten complete carousel cycles;
10. holding BOOT during power-on still exposes the ROM downloader.

No completion claim is based on compilation alone. Visual findings from the physical panel become explicit adjustments for the next slice.

## First implementation boundary

The first implementation ends after the layout/carousel firmware is built, flashed, and observed on the connected board. It does not proceed into Wi-Fi provisioning or live data without a separate reviewed slice.
