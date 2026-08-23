#include "display_port.hpp"

#include <cstring>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_lcd_panel_io.h>
#include <esp_memory_utils.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board_pins.hpp"

namespace board {
namespace {

constexpr char kTag[] = "board_display";
// One frame goes out as ceil(kFramebufferBytes / this) SPI transfers. See
// the bus config in init() for why it is not the whole frame.
constexpr size_t kSpiChunkBytes = 2048;
constexpr size_t kFramebufferBytes =
    static_cast<size_t>(kWidth) * static_cast<size_t>(kHeight) / 8U;
constexpr size_t kPixelCount =
    static_cast<size_t>(kWidth) * static_cast<size_t>(kHeight);

}  // namespace

Display::~Display() { release_resources(); }

Display& display() {
  static Display instance;
  return instance;
}

esp_err_t display_init() {
  const size_t psram_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  esp_chip_info_t chip_info{};
  esp_chip_info(&chip_info);
  uint32_t flash_bytes = 0;
  const esp_err_t flash_result = esp_flash_get_size(nullptr, &flash_bytes);
  ESP_LOGI(kTag, "chip target: ESP32-S3, cores=%d, revision=%d",
           chip_info.cores, chip_info.revision);
  if (flash_result == ESP_OK) {
    ESP_LOGI(kTag, "Flash size: %u bytes", static_cast<unsigned>(flash_bytes));
  } else {
    ESP_LOGE(kTag, "Flash size diagnostic failed: %s",
             esp_err_to_name(flash_result));
  }
  ESP_LOGI(kTag, "PSRAM size: %u bytes, free: %u bytes",
           static_cast<unsigned>(psram_bytes),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  if (psram_bytes == 0) {
    ESP_LOGE(kTag, "fatal: required PSRAM is not available");
    return ESP_ERR_NOT_FOUND;
  }

  const esp_err_t result = display().init();
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "fatal: display initialization failed: %s",
             esp_err_to_name(result));
  }
  return result;
}

esp_err_t Display::init() {
  if (initialized_) {
    return ESP_OK;
  }

  const size_t psram_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  if (psram_bytes == 0) {
    return ESP_ERR_NOT_FOUND;
  }

  spi_bus_config_t bus_config{};
  bus_config.miso_io_num = -1;
  bus_config.mosi_io_num = kDisplayMosi;
  bus_config.sclk_io_num = kDisplaySck;
  bus_config.quadwp_io_num = -1;
  bus_config.quadhd_io_num = -1;
  // Not kFramebufferBytes. This is what esp_lcd chunks colour transfers by
  // (spi_bus_get_max_transaction_len), and therefore the size of the bounce
  // buffer spi_master.c allocates per chunk for a PSRAM source. 15 KB in one
  // piece could not be found on a fragmented heap; 4 KB always can, and the
  // frame simply goes out as four transfers instead of one.
  bus_config.max_transfer_sz = kSpiChunkBytes;

  esp_err_t result = spi_bus_initialize(SPI3_HOST, &bus_config, SPI_DMA_CH_AUTO);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "spi_bus_initialize failed: %s", esp_err_to_name(result));
    return result;
  }
  spi_bus_initialized_ = true;

  esp_lcd_panel_io_spi_config_t io_config{};
  io_config.dc_gpio_num = kDisplayDc;
  io_config.cs_gpio_num = kDisplayCs;
  io_config.pclk_hz = 10 * 1000 * 1000;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.spi_mode = 0;
  // One, not two, and not upstream's ten. Each queued chunk holds its own
  // bounce buffer until it is recycled, so the transient internal RAM this
  // costs is chunk size times queue depth - and that product, not the chunk
  // size alone, is what has to fit.
  //
  // Measured in the field at depth 2 with 4 KB chunks, during the market and
  // weather TLS handshakes at t=9.9 s: "display refresh failed:
  // ESP_ERR_NO_MEM ... internal free=29807 largest=7680, dma largest=5120".
  // Two 4 KB buffers is 8 KB against 5 KB available, so it still failed -
  // less often than the 15 KB single transfer it replaced, which is why it
  // looked fixed. Depth 1 with 2 KB chunks needs 2 KB at a time.
  //
  // Serialising costs nothing measurable: the whole frame transfers in
  // 0.7 ms, so the per-chunk overhead of eight transfers instead of four is
  // far below the noise on a 265 ms refresh.
  io_config.trans_queue_depth = 1;

  auto* io = reinterpret_cast<esp_lcd_panel_io_handle_t*>(&io_handle_);
  result = esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI3_HOST),
                                    &io_config, io);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "esp_lcd_new_panel_io_spi failed: %s",
             esp_err_to_name(result));
    release_resources();
    return result;
  }

  gpio_config_t reset_config{};
  reset_config.intr_type = GPIO_INTR_DISABLE;
  reset_config.mode = GPIO_MODE_OUTPUT;
  reset_config.pin_bit_mask = 1ULL << kDisplayReset;
  reset_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  reset_config.pull_up_en = GPIO_PULLUP_ENABLE;
  result = gpio_config(&reset_config);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "reset GPIO configuration failed: %s", esp_err_to_name(result));
    release_resources();
    return result;
  }

  // PSRAM, and the transfer below is chunked so that this does not cost a
  // 15 KB contiguous internal allocation per refresh.
  //
  // The history matters because both obvious answers are wrong. Left in PSRAM
  // with the whole frame as one transfer, spi_master.c's setup_priv_desc()
  // saw a tx_buffer failing esp_ptr_dma_capable() and allocated a fresh
  // kFramebufferBytes of contiguous MALLOC_CAP_DMA memory for every refresh -
  // against a largest free block measured between 7.9 and 14.3 KB, so most
  // refreshes returned ESP_ERR_NO_MEM.
  //
  // Moving the framebuffer itself into MALLOC_CAP_DMA fixed that and broke
  // something else: 15 KB taken permanently, before wifi initialises, left
  // the wifi RX path short, and AirPlay went from 0-1 inserted silence frames
  // per capture to 1,266. The display worked and the audio turned to
  // crackle. Internal RAM here is not a resource one subsystem can quietly
  // claim.
  //
  // kSpiChunkBytes below is the actual fix: the driver still bounces through
  // an internal buffer, but one small enough that a fragmented heap can
  // always satisfy it, and nothing is held between refreshes.
  display_buffer_ = static_cast<uint8_t*>(
      heap_caps_malloc(kFramebufferBytes, MALLOC_CAP_SPIRAM));
  if (display_buffer_ == nullptr) {
    ESP_LOGE(kTag, "display framebuffer allocation failed (%u bytes)",
             static_cast<unsigned>(kFramebufferBytes));
    release_resources();
    return ESP_ERR_NO_MEM;
  }

  pixel_index_lut_ = reinterpret_cast<uint16_t (*)[kHeight]>(heap_caps_malloc(
      kPixelCount * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
  if (pixel_index_lut_ == nullptr) {
    ESP_LOGE(kTag, "landscape pixel-index LUT allocation failed");
    release_resources();
    return ESP_ERR_NO_MEM;
  }

  pixel_bit_lut_ = reinterpret_cast<uint8_t (*)[kHeight]>(heap_caps_malloc(
      kPixelCount * sizeof(uint8_t), MALLOC_CAP_SPIRAM));
  if (pixel_bit_lut_ == nullptr) {
    ESP_LOGE(kTag, "landscape pixel-bit LUT allocation failed");
    release_resources();
    return ESP_ERR_NO_MEM;
  }

  init_landscape_lut();
  ESP_LOGI(kTag, "display buffer allocation: framebuffer=%u, pixel LUT=%u/%u bytes",
           static_cast<unsigned>(kFramebufferBytes),
           static_cast<unsigned>(kPixelCount * sizeof(uint16_t)),
           static_cast<unsigned>(kPixelCount * sizeof(uint8_t)));

  result = controller_init();
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "ST7305 initialization failed: %s", esp_err_to_name(result));
    release_resources();
    return result;
  }

  initialized_ = true;
  return ESP_OK;
}

void Display::release_resources() {
  if (io_handle_ != nullptr) {
    (void)esp_lcd_panel_io_del(static_cast<esp_lcd_panel_io_handle_t>(io_handle_));
    io_handle_ = nullptr;
  }
  if (spi_bus_initialized_) {
    (void)spi_bus_free(SPI3_HOST);
    spi_bus_initialized_ = false;
  }
  if (pixel_bit_lut_ != nullptr) {
    heap_caps_free(pixel_bit_lut_);
    pixel_bit_lut_ = nullptr;
  }
  if (pixel_index_lut_ != nullptr) {
    heap_caps_free(pixel_index_lut_);
    pixel_index_lut_ = nullptr;
  }
  if (display_buffer_ != nullptr) {
    heap_caps_free(display_buffer_);
    display_buffer_ = nullptr;
  }
  initialized_ = false;
}

esp_err_t Display::controller_init() {
  esp_err_t result = reset();
  if (result != ESP_OK) {
    return result;
  }

  const auto write = [this](uint8_t command, const uint8_t* data,
                            size_t length) {
    return send_command_data(command, data, length);
  };
  const uint8_t d6[] = {0x17, 0x02};
  const uint8_t d1[] = {0x01};
  const uint8_t c0[] = {0x11, 0x04};
  const uint8_t c1[] = {0x69, 0x69, 0x69, 0x69};
  const uint8_t c2[] = {0x19, 0x19, 0x19, 0x19};
  const uint8_t c4[] = {0x4B, 0x4B, 0x4B, 0x4B};
  const uint8_t c5[] = {0x19, 0x19, 0x19, 0x19};
  const uint8_t d8[] = {0x80, 0xE9};
  const uint8_t b2[] = {0x02};
  const uint8_t b3[] = {0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77,
                        0x77, 0x77, 0x76, 0x45};
  const uint8_t b4[] = {0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45};
  const uint8_t b9[] = {0x20};
  const uint8_t b8[] = {0x29};
  const uint8_t column[] = {0x12, 0x2A};
  const uint8_t page[] = {0x00, 0xC7};
  const uint8_t c62[] = {0x32, 0x03, 0x1F};
  const uint8_t b7[] = {0x13};
  const uint8_t b0[] = {0x64};
  const uint8_t c9[] = {0x00};
  const uint8_t c36[] = {0x48};
  const uint8_t c3a[] = {0x11};
  const uint8_t c35[] = {0x00};
  const uint8_t d0[] = {0xFF};

  if ((result = write(0xD6, d6, sizeof(d6))) != ESP_OK ||
      (result = write(0xD1, d1, sizeof(d1))) != ESP_OK ||
      (result = write(0xC0, c0, sizeof(c0))) != ESP_OK ||
      (result = write(0xC1, c1, sizeof(c1))) != ESP_OK ||
      (result = write(0xC2, c2, sizeof(c2))) != ESP_OK ||
      (result = write(0xC4, c4, sizeof(c4))) != ESP_OK ||
      (result = write(0xC5, c5, sizeof(c5))) != ESP_OK ||
      (result = write(0xD8, d8, sizeof(d8))) != ESP_OK ||
      (result = write(0xB2, b2, sizeof(b2))) != ESP_OK ||
      (result = write(0xB3, b3, sizeof(b3))) != ESP_OK ||
      (result = write(0xB4, b4, sizeof(b4))) != ESP_OK ||
      (result = write(0x62, c62, sizeof(c62))) != ESP_OK ||
      (result = write(0xB7, b7, sizeof(b7))) != ESP_OK ||
      (result = write(0xB0, b0, sizeof(b0))) != ESP_OK) {
    return result;
  }

  if ((result = send_command(0x11)) != ESP_OK) {
    return result;
  }
  vTaskDelay(pdMS_TO_TICKS(200));
  if ((result = write(0xC9, c9, sizeof(c9))) != ESP_OK ||
      (result = write(0x36, c36, sizeof(c36))) != ESP_OK ||
      (result = write(0x3A, c3a, sizeof(c3a))) != ESP_OK ||
      (result = write(0xB9, b9, sizeof(b9))) != ESP_OK ||
      (result = write(0xB8, b8, sizeof(b8))) != ESP_OK ||
      (result = send_command(0x21)) != ESP_OK ||
      (result = write(0x2A, column, sizeof(column))) != ESP_OK ||
      (result = write(0x2B, page, sizeof(page))) != ESP_OK ||
      (result = write(0x35, c35, sizeof(c35))) != ESP_OK ||
      (result = write(0xD0, d0, sizeof(d0))) != ESP_OK ||
      (result = send_command(0x38)) != ESP_OK ||
      (result = send_command(0x29)) != ESP_OK) {
    return result;
  }

  clear(Color::White);
  return refresh();
}

esp_err_t Display::reset() {
  esp_err_t result = gpio_set_level(kDisplayReset, 1);
  if (result != ESP_OK) {
    return result;
  }
  vTaskDelay(pdMS_TO_TICKS(50));
  if ((result = gpio_set_level(kDisplayReset, 0)) != ESP_OK) {
    return result;
  }
  vTaskDelay(pdMS_TO_TICKS(20));
  if ((result = gpio_set_level(kDisplayReset, 1)) != ESP_OK) {
    return result;
  }
  vTaskDelay(pdMS_TO_TICKS(50));
  return ESP_OK;
}

esp_err_t Display::send_command(uint8_t command) {
  return esp_lcd_panel_io_tx_param(
      static_cast<esp_lcd_panel_io_handle_t>(io_handle_), command, nullptr, 0);
}

esp_err_t Display::send_data(uint8_t data) {
  return esp_lcd_panel_io_tx_param(
      static_cast<esp_lcd_panel_io_handle_t>(io_handle_), -1, &data, 1);
}

esp_err_t Display::send_data(const uint8_t* data, size_t length) {
  return esp_lcd_panel_io_tx_param(
      static_cast<esp_lcd_panel_io_handle_t>(io_handle_), -1, data, length);
}

esp_err_t Display::send_command_data(uint8_t command, const uint8_t* data,
                                      size_t length) {
  esp_err_t result = send_command(command);
  if (result != ESP_OK) {
    return result;
  }
  // Keep each parameter transfer separate, matching the vendor's ST7305 init.
  for (size_t index = 0; index < length; ++index) {
    result = send_data(data[index]);
    if (result != ESP_OK) {
      return result;
    }
  }
  return ESP_OK;
}

void Display::clear(Color color) {
  if (display_buffer_ != nullptr) {
    std::memset(display_buffer_, static_cast<uint8_t>(color), kFramebufferBytes);
  }
}

esp_err_t Display::refresh() {
  if (io_handle_ == nullptr || display_buffer_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t result = send_command(0x2A);  // Column Address Set
  if (result != ESP_OK) {
    return result;
  }
  const uint8_t column[] = {0x12, 0x2A};
  const uint8_t page[] = {0x00, 0xC7};
  if ((result = send_data(column, sizeof(column))) != ESP_OK ||
      (result = send_command(0x2B)) != ESP_OK ||
      (result = send_data(page, sizeof(page))) != ESP_OK ||
      (result = send_command(0x2C)) != ESP_OK) {
    return result;
  }
  // Full RLCD_Display() transfer: ST7305 consumes the complete 1-bit frame.
  return esp_lcd_panel_io_tx_color(
      static_cast<esp_lcd_panel_io_handle_t>(io_handle_), -1, display_buffer_,
      kFramebufferBytes);
}

void Display::init_portrait_lut() {
  const uint16_t width_bytes = static_cast<uint16_t>(kWidth >> 2);
  for (uint16_t y = 0; y < kHeight; ++y) {
    const uint16_t byte_y = y >> 1;
    const uint8_t local_y = y & 1U;
    for (uint16_t x = 0; x < kWidth; ++x) {
      const uint16_t byte_x = x >> 2;
      const uint8_t local_x = x & 3U;
      const uint32_t index = byte_y * width_bytes + byte_x;
      const uint8_t bit = 7U - static_cast<uint8_t>((local_x << 1U) | local_y);
      pixel_index_lut_[x][y] = static_cast<uint16_t>(index);
      pixel_bit_lut_[x][y] = static_cast<uint8_t>(1U << bit);
    }
  }
}

void Display::init_landscape_lut() {
  const uint16_t height_blocks = static_cast<uint16_t>(kHeight >> 2);
  for (uint16_t y = 0; y < kHeight; ++y) {
    const uint16_t inverted_y = static_cast<uint16_t>(kHeight - 1U - y);
    const uint16_t block_y = inverted_y >> 2;
    const uint8_t local_y = inverted_y & 3U;
    for (uint16_t x = 0; x < kWidth; ++x) {
      const uint16_t byte_x = x >> 1;
      const uint8_t local_x = x & 1U;
      const uint32_t index = byte_x * height_blocks + block_y;
      const uint8_t bit = 7U - static_cast<uint8_t>((local_y << 1U) | local_x);
      pixel_index_lut_[x][y] = static_cast<uint16_t>(index);
      pixel_bit_lut_[x][y] = static_cast<uint8_t>(1U << bit);
    }
  }
}

// Measured at 157 ms per 400x300 frame before this existed, against 0.7 ms
// for the SPI transfer that follows it - 230x the cost of the thing everyone
// assumes is expensive, repeating every 265 ms, and the reason AirPlay
// dropped packets intermittently.
//
// Three costs, all removed here:
//
//  - pixel_index_lut_[x][y] and pixel_bit_lut_[x][y] live in PSRAM and were
//    indexed with x innermost, so consecutive lookups jumped kHeight*2 = 600
//    bytes. Every one of the 240,000 lookups per frame was a cache miss on
//    the slowest memory on the board. The values they held are four shifts
//    and a multiply; computing them in registers is not a trade-off.
//  - set_pixel() is in this translation unit and the caller was in another,
//    so none of the 120,000 calls per frame could be inlined, and each one
//    re-checked the same six bounds conditions.
//  - The per-row terms were recomputed per pixel.
//
// The LUTs had exactly one caller, this path, and are gone with it - 360 KB
// of PSRAM returned as well.
void Display::write_rgb565_area(const uint16_t* rgb565, int x1, int y1,
                                int x2, int y2) {
  if (!initialized_ || display_buffer_ == nullptr || rgb565 == nullptr) return;
  if (x1 < 0) x1 = 0;
  if (y1 < 0) y1 = 0;
  if (x2 >= kWidth) x2 = kWidth - 1;
  if (y2 >= kHeight) y2 = kHeight - 1;

  const int height_blocks = kHeight >> 2;
  for (int y = y1; y <= y2; ++y) {
    // Per-row, not per-pixel: none of this depends on x.
    const int inverted_y = kHeight - 1 - y;
    const int block_y = inverted_y >> 2;
    const int local_y = inverted_y & 3;
    const int bit_hi = 7 - (local_y << 1);        // x even
    const int bit_lo = 7 - ((local_y << 1) | 1);  // x odd
    const uint8_t mask_hi = static_cast<uint8_t>(1U << bit_hi);
    const uint8_t mask_lo = static_cast<uint8_t>(1U << bit_lo);

    const uint16_t* src = rgb565 + static_cast<size_t>(y - y1) * (x2 - x1 + 1);
    for (int x = x1; x <= x2; ++x) {
      // LVGL renders white as 0xffff and black as 0x0000; the original
      // threshold is kept exactly rather than reinterpreted.
      const bool black = *src++ < 0x7fff;
      const size_t index =
          static_cast<size_t>(x >> 1) * height_blocks + block_y;
      const uint8_t mask = (x & 1) ? mask_lo : mask_hi;
      if (black) {
        display_buffer_[index] &= static_cast<uint8_t>(~mask);
      } else {
        display_buffer_[index] |= mask;
      }
    }
  }
}

// The inverse of write_rgb565_area()'s layout, run on demand.
//
// A shadow copy of this used to be maintained pixel by pixel on every flush,
// which is 120,000 iterations and a second PSRAM write pattern per frame, to
// serve a screenshot route that fires when a person asks for one. Deriving it
// here costs the same work once per request instead of four times a second
// forever.
bool Display::read_row_major(uint8_t* out, size_t length) const {
  const size_t stride = kWidth / 8;
  if (out == nullptr || display_buffer_ == nullptr ||
      length < stride * kHeight) {
    return false;
  }
  std::memset(out, 0, stride * kHeight);
  const int height_blocks = kHeight >> 2;
  for (int y = 0; y < kHeight; ++y) {
    const int inverted_y = kHeight - 1 - y;
    const int block_y = inverted_y >> 2;
    const int local_y = inverted_y & 3;
    const uint8_t mask_hi = static_cast<uint8_t>(1U << (7 - (local_y << 1)));
    const uint8_t mask_lo =
        static_cast<uint8_t>(1U << (7 - ((local_y << 1) | 1)));
    uint8_t* row = out + static_cast<size_t>(y) * stride;
    for (int x = 0; x < kWidth; ++x) {
      const size_t index =
          static_cast<size_t>(x >> 1) * height_blocks + block_y;
      const uint8_t mask = (x & 1) ? mask_lo : mask_hi;
      // write_rgb565_area() clears the bit for black and sets it for white.
      if ((display_buffer_[index] & mask) == 0) {
        row[x >> 3] |= static_cast<uint8_t>(0x80U >> (x & 7));
      }
    }
  }
  return true;
}

void Display::set_pixel(int x, int y, Color color) {
  if (!initialized_ || display_buffer_ == nullptr || pixel_index_lut_ == nullptr ||
      pixel_bit_lut_ == nullptr || x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
    return;
  }
  const uint16_t index = pixel_index_lut_[x][y];
  const uint8_t mask = pixel_bit_lut_[x][y];
  if (color == Color::White) {
    display_buffer_[index] |= mask;
  } else {
    display_buffer_[index] &= static_cast<uint8_t>(~mask);
  }
}

}  // namespace board
