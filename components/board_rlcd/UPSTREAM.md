# Board port provenance

This component is derived from the Waveshare ESP32-S3-RLCD-4.2 repository:

- Repository: <https://github.com/waveshareteam/ESP32-S3-RLCD-4.2.git>
- Pinned commit: `eb1f63427d735a22b9c30e22fa63ebddae1834d3`
- Display source: `02_Example/ESP-IDF/09_LVGL_V9_Test/components/port_bsp/display_bsp.cpp`
- Display header: `02_Example/ESP-IDF/09_LVGL_V9_Test/components/port_bsp/display_bsp.h`
- LVGL source: `02_Example/ESP-IDF/09_LVGL_V9_Test/components/app_bsp/lvgl_bsp.cpp`
- LVGL header: `02_Example/ESP-IDF/09_LVGL_V9_Test/components/app_bsp/lvgl_bsp.h`

The pinned checkout used for this port is `/tmp/waveshare-rlcd-upstream-20260815`.
That working copy is sparse and contains only the factory binary on disk; the
paths above were read from the pinned Git tree and are retained as the exact
source provenance.

## Intentional audit changes

- Put the board API in the `board` namespace and define fixed pins in one
  header rather than passing mutable pin values through the application.
- Replace constructor-time `ESP_ERROR_CHECK` and allocation assertions with
  explicit `esp_err_t` returns and startup logging.
- Check PSRAM presence and every display/LVGL allocation before starting tasks.
- Add diagnostics for chip target, Flash, PSRAM, display buffers, and LVGL task
  creation.
- Keep the ST7305 command bytes, 10 MHz SPI transport, landscape LUT, 15,000
  byte 1-bit framebuffer, and full-frame `RLCD_Display()` transfer unchanged.
- Remove the generated UI and unrelated application/peripheral code; this
  component only owns the display transport and LVGL service.
