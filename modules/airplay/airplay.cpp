#include "airplay.hpp"

#ifdef CONFIG_AIRPLAY_ENABLE

#include "esp_raop_receiver.h"

#include <esp_log.h>

namespace airplay {

namespace {

const char *kTag = "airplay";
raop_handle_t *g_handle = nullptr;

// Decoded PCM has nowhere to go yet - see airplay.hpp's namespace comment.
// Nothing calls airplay_init() from main/app_main.cpp in this pass either,
// so this callback firing at all is itself unverified (see README.md,
// "What is unverified"). A later, deliberately separate pass replaces this
// with a real sink - modules/audio is the obvious candidate given the
// module contract's one-way airplay -> audio dependency (modules/README.md
// rule 4), but that wiring decision belongs to that pass, not this one.
void discard_audio(const uint8_t * /*data*/, size_t /*len*/, void * /*user_ctx*/) {}

}  // namespace

esp_err_t airplay_init() {
  if (g_handle != nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  raop_config_t config = {};
  config.audio_output_cb = discard_audio;
  config.mdns_mode = RAOP_MDNS_MANAGED;
  config.volume_mode = RAOP_VOLUME_SOFTWARE;

  esp_err_t err = raop_init(&config, &g_handle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "raop_init failed: %s", esp_err_to_name(err));
    g_handle = nullptr;
  }
  return err;
}

esp_err_t airplay_deinit() {
  if (g_handle == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t err = raop_deinit(g_handle);
  g_handle = nullptr;
  return err;
}

}  // namespace airplay

#endif  // CONFIG_AIRPLAY_ENABLE
