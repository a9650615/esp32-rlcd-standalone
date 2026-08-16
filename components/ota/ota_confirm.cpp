#include "ota_confirm.hpp"

#include <atomic>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace ota {
namespace {

constexpr char kTag[] = "ota";

SemaphoreHandle_t g_answered = nullptr;
std::atomic<bool> g_pending{false};
std::atomic<bool> g_accepted{false};
void (*g_prompt)(bool, const std::string&) = nullptr;

}  // namespace

void set_confirm_prompt_handler(void (*handler)(bool, const std::string&)) {
  g_prompt = handler;
}

bool confirm_pending() { return g_pending.load(std::memory_order_acquire); }

ConfirmResult request_confirm(const std::string& peer, uint32_t timeout_ms) {
  if (g_answered == nullptr) {
    // Binary rather than counting: an answer that arrives with nothing waiting
    // must not be banked and applied to the next request.
    g_answered = xSemaphoreCreateBinary();
    if (g_answered == nullptr) return ConfirmResult::Rejected;
  }
  bool expected = false;
  if (!g_pending.compare_exchange_strong(expected, true)) {
    ESP_LOGW(kTag, "a confirmation is already pending; rejecting %s",
             peer.c_str());
    return ConfirmResult::Rejected;
  }

  // Clear any stale signal before showing the prompt, so a previous timeout
  // that raced with its own answer cannot satisfy this one instantly.
  xSemaphoreTake(g_answered, 0);
  g_accepted.store(false, std::memory_order_release);
  if (g_prompt != nullptr) g_prompt(true, peer);
  ESP_LOGW(kTag, "update offered by %s; waiting for the board", peer.c_str());

  const bool signalled =
      xSemaphoreTake(g_answered, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
  const bool accepted = g_accepted.load(std::memory_order_acquire);
  g_pending.store(false, std::memory_order_release);
  if (g_prompt != nullptr) g_prompt(false, {});

  if (!signalled) {
    ESP_LOGW(kTag, "update offer from %s timed out unanswered", peer.c_str());
    return ConfirmResult::TimedOut;
  }
  ESP_LOGW(kTag, "update offer from %s %s", peer.c_str(),
           accepted ? "accepted" : "rejected");
  return accepted ? ConfirmResult::Accepted : ConfirmResult::Rejected;
}

void answer_confirm(bool accepted) {
  if (!g_pending.load(std::memory_order_acquire) || g_answered == nullptr) {
    return;
  }
  g_accepted.store(accepted, std::memory_order_release);
  xSemaphoreGive(g_answered);
}

}  // namespace ota
