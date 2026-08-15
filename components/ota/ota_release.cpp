#include "ota_release.hpp"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <sdkconfig.h>

#include <memory>
#include <vector>

#include "ota_version.hpp"

namespace ota {
namespace {

constexpr char kTag[] = "ota";
constexpr int kTimeoutMs = 15'000;
// A release payload with a few assets runs a few KiB. 16 KiB is generous
// enough for a verbose release body while still bounding what an unexpected
// response can make this allocate.
constexpr std::size_t kMaxResponseBytes = 16 * 1024;

const char* running_version() {
  const esp_app_desc_t* desc = esp_app_get_description();
  return desc != nullptr ? desc->version : "";
}

// Pulls the firmware asset's download URL out of a GitHub release object.
std::string find_asset_url(const cJSON* release) {
  const cJSON* assets = cJSON_GetObjectItemCaseSensitive(release, "assets");
  if (!cJSON_IsArray(assets)) return {};
  const std::string suffix = CONFIG_OTA_RELEASE_ASSET_SUFFIX;
  const cJSON* asset = nullptr;
  cJSON_ArrayForEach(asset, assets) {
    const cJSON* name = cJSON_GetObjectItemCaseSensitive(asset, "name");
    const cJSON* url =
        cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url");
    if (!cJSON_IsString(name) || !cJSON_IsString(url)) continue;
    const std::string filename = name->valuestring;
    if (filename.size() >= suffix.size() &&
        filename.compare(filename.size() - suffix.size(), suffix.size(),
                         suffix) == 0) {
      return url->valuestring;
    }
  }
  return {};
}

}  // namespace

ReleaseInfo check_latest_release() {
  ReleaseInfo info;
  const std::string url = std::string("https://api.github.com/repos/") +
                          CONFIG_OTA_RELEASE_REPO + "/releases/latest";

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.timeout_ms = kTimeoutMs;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    info.message = "Could not start the update check";
    return info;
  }
  struct Closer {
    esp_http_client_handle_t client;
    ~Closer() {
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
    }
  } closer{client};

  // GitHub rejects API requests without a User-Agent, and the error it returns
  // for that looks like any other 403, so it is worth setting explicitly
  // rather than debugging later.
  esp_http_client_set_header(client, "User-Agent", "esp32-rlcd-standalone");
  esp_http_client_set_header(client, "Accept", "application/vnd.github+json");

  if (esp_http_client_open(client, 0) != ESP_OK) {
    info.message = "Could not reach GitHub";
    return info;
  }
  esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (status == 404) {
    // Distinguished from a generic failure because it is the answer for a
    // repository that simply has no releases yet, which is not an error the
    // user can act on by retrying.
    info.message = "No releases published yet";
    ESP_LOGI(kTag, "update check: repository has no releases");
    return info;
  }
  if (status == 403) {
    info.message = "GitHub rate limit reached; try later";
    return info;
  }
  if (status != 200) {
    ESP_LOGW(kTag, "update check: GitHub answered HTTP %d", status);
    info.message = "GitHub returned an error";
    return info;
  }

  std::string body;
  std::vector<char> chunk(1024);
  while (body.size() < kMaxResponseBytes) {
    const int read = esp_http_client_read(client, chunk.data(), chunk.size());
    if (read < 0) {
      info.message = "Update check interrupted";
      return info;
    }
    if (read == 0) break;
    body.append(chunk.data(), static_cast<std::size_t>(read));
  }

  cJSON* root = cJSON_Parse(body.c_str());
  if (root == nullptr) {
    info.message = "Could not read GitHub's answer";
    return info;
  }
  std::unique_ptr<cJSON, void (*)(cJSON*)> owned(root, [](cJSON* p) {
    cJSON_Delete(p);
  });

  const cJSON* tag = cJSON_GetObjectItemCaseSensitive(root, "tag_name");
  if (!cJSON_IsString(tag)) {
    info.message = "Release has no version tag";
    return info;
  }
  info.version = tag->valuestring;
  info.firmware_url = find_asset_url(root);
  info.ok = true;

  if (info.firmware_url.empty()) {
    // A real release that carries no firmware. Reported rather than treated
    // as "up to date", because the difference matters to whoever published it.
    info.message = "Release " + info.version + " has no firmware attached";
    ESP_LOGW(kTag, "update check: %s carries no %s asset",
             info.version.c_str(), CONFIG_OTA_RELEASE_ASSET_SUFFIX);
    return info;
  }

  const std::string running = running_version();
  info.update_available = is_newer(info.version, running);
  info.message = info.update_available
                     ? "Update available: " + info.version
                     : "Up to date (latest is " + info.version + ")";
  ESP_LOGI(kTag, "update check: running=%s latest=%s newer=%d",
           running.c_str(), info.version.c_str(), info.update_available);
  return info;
}

}  // namespace ota
