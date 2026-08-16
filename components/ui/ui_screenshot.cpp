#ifndef NDEBUG
#include "ui_screenshot.hpp"

#include <array>
#include <cstdio>

#include <esp_log.h>
#include <mbedtls/base64.h>

#include "lvgl_port.hpp"

namespace ui {
namespace {

constexpr char kTag[] = "ui_shot";

// One per PageId. Each page dumps once per boot: enough to look at every
// screen after a flash, and quiet afterwards. A reflash - which is what
// follows any layout change worth looking at - resets them.
std::array<bool, 8> g_sent{};

// Serial carries this a line at a time so a capture stays readable and a
// dropped line is visible as a short frame rather than a corrupt one.
constexpr size_t kLineBytes = 96;

}  // namespace

void dump_frame_once(app_core::PageId page, const char* name) {
  const auto index = static_cast<size_t>(page);
  if (index >= g_sent.size() || g_sent[index]) return;

  static std::array<uint8_t, board::kFramebufferSnapshotBytes> raw;
  if (!board::framebuffer_snapshot(raw.data(), raw.size())) return;
  g_sent[index] = true;

  ESP_LOGW(kTag, "SHOT BEGIN %s 400x300", name);
  size_t offset = 0;
  while (offset < raw.size()) {
    const size_t chunk = std::min(kLineBytes, raw.size() - offset);
    unsigned char encoded[kLineBytes * 4 / 3 + 8];
    size_t written = 0;
    if (mbedtls_base64_encode(encoded, sizeof(encoded), &written,
                              raw.data() + offset, chunk) != 0) {
      ESP_LOGE(kTag, "SHOT ABORT encode failed at %u", (unsigned)offset);
      return;
    }
    encoded[written] = '\0';
    // printf rather than ESP_LOG: no timestamp or tag to strip back off, so
    // the decoder can take the payload lines verbatim.
    std::printf("SHOT %s\n", reinterpret_cast<char*>(encoded));
    offset += chunk;
  }
  ESP_LOGW(kTag, "SHOT END %s", name);
}

}  // namespace ui
#endif
