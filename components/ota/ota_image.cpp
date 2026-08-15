#include "ota_image.hpp"

#include <cstring>

namespace ota {
namespace {

// Layout constants, duplicated here as literals rather than by including
// esp_app_format.h, so this file compiles for host tests without an ESP-IDF
// target. Every one was read back out of this project's own built binary (see
// kImagePrefixBytes) instead of computed from struct arithmetic.
constexpr uint8_t kEspImageMagic = 0xE9;          // esp_image_header_t.magic
constexpr std::size_t kChipIdOffset = 12;         // esp_image_header_t.chip_id
constexpr uint16_t kChipIdEsp32S3 = 0x0009;       // ESP_CHIP_ID_ESP32S3
constexpr std::size_t kDescMagicOffset = 32;      // esp_app_desc_t.magic_word
constexpr uint32_t kDescMagic = 0xABCD5432;       // ESP_APP_DESC_MAGIC_WORD
constexpr std::size_t kVersionOffset = 48;        // esp_app_desc_t.version
constexpr std::size_t kVersionBytes = 32;
constexpr std::size_t kProjectOffset = 80;        // esp_app_desc_t.project_name
constexpr std::size_t kProjectBytes = 32;

// Must match the CMake project() name; it is what ends up in the descriptor.
constexpr char kExpectedProject[] = "layout_carousel";

uint16_t read_u16(const uint8_t* data, std::size_t offset) {
  return static_cast<uint16_t>(data[offset]) |
         static_cast<uint16_t>(data[offset + 1] << 8);
}

uint32_t read_u32(const uint8_t* data, std::size_t offset) {
  return static_cast<uint32_t>(data[offset]) |
         (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) |
         (static_cast<uint32_t>(data[offset + 3]) << 24);
}

// The descriptor's char arrays are fixed-width and NUL-padded, but a corrupt
// or hostile image need not terminate them - bound the scan by the field.
std::string read_fixed_string(const uint8_t* data, std::size_t offset,
                              std::size_t size) {
  const void* end = std::memchr(data + offset, '\0', size);
  const std::size_t length =
      end != nullptr ? static_cast<std::size_t>(
                           static_cast<const uint8_t*>(end) - (data + offset))
                     : size;
  return std::string(reinterpret_cast<const char*>(data + offset), length);
}

}  // namespace

ImageInfo inspect_image_prefix(const uint8_t* data, std::size_t length) {
  ImageInfo info;
  if (data == nullptr || length < kImagePrefixBytes) {
    info.verdict = ImageVerdict::TooShort;
    return info;
  }

  if (data[0] != kEspImageMagic) {
    info.verdict = ImageVerdict::NotAnEspImage;
    return info;
  }
  if (read_u16(data, kChipIdOffset) != kChipIdEsp32S3) {
    info.verdict = ImageVerdict::WrongChip;
    return info;
  }
  if (read_u32(data, kDescMagicOffset) != kDescMagic) {
    info.verdict = ImageVerdict::NotAnApplication;
    return info;
  }

  // Read before the project check so a rejected image can still say what it
  // actually was, which is the difference between "wrong file" and a mystery.
  info.version = read_fixed_string(data, kVersionOffset, kVersionBytes);
  info.project_name = read_fixed_string(data, kProjectOffset, kProjectBytes);

  if (info.project_name != kExpectedProject) {
    info.verdict = ImageVerdict::WrongProject;
    return info;
  }

  info.verdict = ImageVerdict::Ok;
  return info;
}

const char* image_verdict_message(ImageVerdict verdict) {
  switch (verdict) {
    case ImageVerdict::Ok:
      return "OK";
    case ImageVerdict::TooShort:
      return "File too small to be firmware";
    case ImageVerdict::NotAnEspImage:
      return "Not an ESP firmware image";
    case ImageVerdict::WrongChip:
      return "Firmware is for a different chip";
    case ImageVerdict::NotAnApplication:
      return "Not an application image";
    case ImageVerdict::WrongProject:
      return "Firmware is for a different project";
  }
  return "Rejected";
}

}  // namespace ota
