#pragma once

#include <cstdint>
#include <cstddef>

#include <esp_err.h>

namespace board {

enum class Color : uint8_t {
  Black = 0x00,
  White = 0xff,
};

class Display {
 public:
  Display() = default;
  ~Display();

  Display(const Display&) = delete;
  Display& operator=(const Display&) = delete;

  esp_err_t init();
  esp_err_t refresh();
  void clear(Color color);
  void set_pixel(int x, int y, Color color);

 private:
  esp_err_t send_command(uint8_t command);
  esp_err_t send_data(uint8_t data);
  esp_err_t send_data(const uint8_t* data, size_t length);
  esp_err_t send_command_data(uint8_t command, const uint8_t* data,
                              size_t length);
  esp_err_t reset();
  esp_err_t controller_init();
  void release_resources();
  void init_portrait_lut();
  void init_landscape_lut();

  void* io_handle_ = nullptr;
  uint8_t* display_buffer_ = nullptr;
  uint16_t (*pixel_index_lut_)[300] = nullptr;
  uint8_t (*pixel_bit_lut_)[300] = nullptr;
  bool spi_bus_initialized_ = false;
  bool initialized_ = false;
};

Display& display();
esp_err_t display_init();

}  // namespace board
