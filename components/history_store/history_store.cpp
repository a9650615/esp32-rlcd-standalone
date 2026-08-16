#include "history_store.hpp"

#include <esp_log.h>
#include <esp_partition.h>

#include <cstring>

namespace history_store {
namespace {

constexpr char kTag[] = "history";
constexpr char kPartitionLabel[] = "storage";
constexpr size_t kSectorBytes = 4096;

const esp_partition_t* g_partition = nullptr;
size_t g_sectors = 0;
// Sector the last successful save landed in; the next one goes after it.
size_t g_last_sector = 0;
uint32_t g_last_seq = 0;
bool g_ready = false;

// Held in static storage rather than on a caller's stack: it is 3.5 KiB, and
// the tasks that record into it are sized for sampling, not for carrying the
// whole window.
app_core::HistoryBlob g_current;

}  // namespace

esp_err_t init() {
  if (g_ready) return ESP_OK;

  g_partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, kPartitionLabel);
  if (g_partition == nullptr) {
    ESP_LOGE(kTag, "no '%s' partition; history will not persist",
             kPartitionLabel);
    return ESP_ERR_NOT_FOUND;
  }
  g_sectors = g_partition->size / kSectorBytes;
  if (g_sectors == 0) return ESP_ERR_INVALID_SIZE;

  // Read only the header of each sector to find candidates. A full read of
  // every sector would be a megabyte off the flash at every boot to answer a
  // question the first sixteen bytes settle.
  struct Header {
    uint32_t magic;
    uint32_t seq;
  };
  bool found = false;
  size_t best_sector = 0;
  uint32_t best_seq = 0;
  for (size_t sector = 0; sector < g_sectors; ++sector) {
    Header header{};
    if (esp_partition_read(g_partition, sector * kSectorBytes, &header,
                           sizeof(header)) != ESP_OK) {
      continue;
    }
    if (header.magic != app_core::kHistoryMagic) continue;
    if (!found || header.seq > best_seq) {
      found = true;
      best_seq = header.seq;
      best_sector = sector;
    }
  }

  g_current = app_core::HistoryBlob{};
  if (found) {
    app_core::HistoryBlob candidate;
    const bool read_ok =
        esp_partition_read(g_partition, best_sector * kSectorBytes, &candidate,
                           sizeof(candidate)) == ESP_OK;
    // A header can survive a power cut that truncated the body, so the
    // checksum is what decides, not the magic that got us here. Falling back
    // to an empty history rather than hunting for the next-best sector: the
    // one case this happens is the save that was interrupted, and the sector
    // before it holds a window five minutes older that nobody will miss.
    if (read_ok && app_core::history_blob_valid(candidate)) {
      g_current = candidate;
      g_last_sector = best_sector;
      g_last_seq = candidate.seq;
      ESP_LOGI(kTag, "restored %u slots from sector %u (seq %u)",
               static_cast<unsigned>(g_current.count),
               static_cast<unsigned>(best_sector),
               static_cast<unsigned>(g_last_seq));
    } else {
      // Still adopt the sequence number: writing a lower one would make the
      // damaged sector look newer than the good save that follows it.
      g_last_sector = best_sector;
      g_last_seq = best_seq;
      ESP_LOGW(kTag, "sector %u failed its checksum; starting empty",
               static_cast<unsigned>(best_sector));
    }
  } else {
    ESP_LOGI(kTag, "no stored history yet (%u sectors available)",
             static_cast<unsigned>(g_sectors));
  }

  g_ready = true;
  return ESP_OK;
}

const app_core::HistoryBlob& current() { return g_current; }

esp_err_t record(const app_core::HistorySample& sample) {
  if (!g_ready || g_partition == nullptr) return ESP_ERR_INVALID_STATE;

  app_core::history_append(g_current, sample);

  // Rotation is the whole wear strategy: every save goes to the next sector,
  // so 256 sectors share the erase load instead of one sector absorbing it.
  const size_t sector = (g_last_sector + 1) % g_sectors;
  const size_t offset = sector * kSectorBytes;
  app_core::history_blob_seal(g_current, g_last_seq + 1);

  esp_err_t result = esp_partition_erase_range(g_partition, offset,
                                               kSectorBytes);
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "erase of sector %u failed: %s",
             static_cast<unsigned>(sector), esp_err_to_name(result));
    return result;
  }
  result = esp_partition_write(g_partition, offset, &g_current,
                               sizeof(g_current));
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "write to sector %u failed: %s",
             static_cast<unsigned>(sector), esp_err_to_name(result));
    return result;
  }

  // Only advance once the write succeeded. A failed save leaves the previous
  // sector as the newest valid one, which is exactly what a reboot should
  // find.
  g_last_sector = sector;
  g_last_seq = g_current.seq;
  return ESP_OK;
}

esp_err_t clear() {
  if (!g_ready || g_partition == nullptr) return ESP_ERR_INVALID_STATE;
  const esp_err_t result =
      esp_partition_erase_range(g_partition, 0, g_sectors * kSectorBytes);
  if (result != ESP_OK) return result;
  g_current = app_core::HistoryBlob{};
  g_last_sector = 0;
  g_last_seq = 0;
  ESP_LOGW(kTag, "history erased");
  return ESP_OK;
}

}  // namespace history_store
