#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ota {

// Why the upload is being refused. Ordered roughly by how early it is caught.
enum class ImageVerdict : uint8_t {
  Ok,
  // Fewer bytes than the header needs. Not necessarily a bad image - the
  // caller may simply not have enough of it yet.
  TooShort,
  // First byte is not 0xE9. Anything that is not an ESP binary at all - a
  // JPEG, an HTML error page a proxy substituted for the firmware, a
  // truncated download - dies here, on byte one.
  NotAnEspImage,
  // An ESP image built for a different chip.
  WrongChip,
  // An ESP image with no IDF application descriptor: a bootloader, a
  // partition table, or a raw flash dump rather than an app.
  NotAnApplication,
  // A valid ESP32-S3 IDF application, but from some other project.
  WrongProject,
};

struct ImageInfo {
  ImageVerdict verdict = ImageVerdict::TooShort;
  // Populated only when the descriptor was readable; both are shown on the
  // panel and logged, so they are worth extracting even for a rejected image.
  std::string version;
  std::string project_name;
};

// Bytes of the image prefix needed before a verdict can be reached: the
// 24-byte esp_image_header_t, an 8-byte esp_image_segment_header_t, and
// enough of the esp_app_desc_t that follows to reach the end of project_name.
//
// Offsets confirmed against this project's own build/layout_carousel.bin
// rather than derived from struct arithmetic: magic 0xE9 at 0, chip id at 12,
// descriptor magic 0xABCD5432 at 32, version at 48, project name at 80.
inline constexpr std::size_t kImagePrefixBytes = 112;

// Pure: no flash, no ESP APIs, so the whole decision is host-testable.
//
// Meant to be run on the very first chunk of an upload, before esp_ota_begin
// erases anything. Erasing a 3 MiB slot to discover on the last byte that the
// user picked the wrong file destroys the working spare copy for nothing.
//
// Deliberately does NOT compare versions. Re-flashing the version already
// installed is a legitimate recovery move, and a device that refuses it is a
// device you cannot rescue with the image you have on hand.
ImageInfo inspect_image_prefix(const uint8_t* data, std::size_t length);

// ASCII, for the panel and the HTTP response.
const char* image_verdict_message(ImageVerdict verdict);

}  // namespace ota
