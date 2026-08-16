#include "ui_strings.hpp"

#include <cstdio>

#include <cstddef>

namespace ui {
namespace {

// One row per Text, in enum order. The static_assert below is what keeps that
// promise: add a Text without a row and the build stops, rather than the
// screen silently showing the wrong string.
struct Row {
  const char* english;
  const char* traditional_chinese;
};

constexpr Row kRows[] = {
    {"NO DATA", "無資料"},
    {"NO INTRADAY DATA", "無盤中資料"},
    {" OLD", " 過期"},
    {"COMFORT BAND  40-60 RH", "舒適範圍  40-60 RH"},

    {"Setup", "設定連線"},
    {"AP SSID unavailable", "無法取得熱點名稱"},
    {"PAGE PW: unavailable", "頁面密碼: 無法取得"},
    {"Join the Wi-Fi, open the URL, enter the password.",
     "連上熱點, 開啟網址, 輸入密碼。"},
    {"Not yet connected", "尚未連線"},
    {"QR unavailable - join the Wi-Fi named at left",
     "QR 無法顯示 - 請連左側熱點"},

    {"UPDATE OFFERED", "收到更新"},
    {"KEY cancel      accept BOOT", "KEY 取消      接受 BOOT"},
    {"UPDATING", "更新中"},
    {"FINISHING UPDATE", "寫入中"},
    {"VERIFYING UPDATE", "驗證中"},
    {"UPDATE ROLLED BACK", "更新已還原"},
    {"UPDATE FAILED", "更新失敗"},
    {"DO NOT POWER OFF", "請勿斷電"},
    {"WORKING", "處理中"},

    {"Settings", "設定"},
    {"Firmware", "韌體版本"},
    {"Language", "語言"},
    {"Check for updates", "檢查更新"},
    {"Wi-Fi setup", "Wi-Fi 設定"},
    {"Battery", "電池"},
    {"Runtime", "預估續航"},
    {"Checking...", "檢查中..."},
    {"Charging", "充電中"},
    {"Collecting", "收集中"},

    {"Next", "下一項"},
    {"Select", "選擇"},
    {"Back", "返回"},
    {"Prev", "上一頁"},
    {"Next", "下一頁"},

    {"BATTERY", "電池"},
    {"SENSOR", "感測器"},
    {"MARKET", "市場"},
    {"WEATHER", "天氣"},
    {"HISTORY", "歷史"},
    {"COLLECTING...", "記錄中..."},
    {"RANGE", "區間"},
    {"CLOSE", "收盤"},
    {"STATUS", "狀態"},
    {"TAIWAN MARKET", "台股"},
    {"US MARKET", "美股"},
    {"WIFI", "已連線"},
    {"NO WIFI", "無網路"},
    {"SETUP", "設定中"},
    {"ALERT  ", "警示  "},
    {"LOW BATTERY", "電量偏低"},
    {"OVERVOLTAGE", "電壓過高"},
    {"DRY", "偏乾"},
    {"WET", "偏濕"},
    {"OPEN", "開盤"},
    {"MID", "盤中"},
    {"CLOSE", "收盤"},
    {"WIFI: ", "熱點: "},
    {"PAGE PW: ", "頁面密碼: "},
    {"Connecting...", "連線中..."},

    {"CLOCK FROM RTC", "時鐘來自 RTC"},
    {"CLOCK NOT SYNCED", "時鐘未同步"},

    {"English", "English"},
    {"Traditional Chinese", "繁體中文"},
};

static_assert(sizeof(kRows) / sizeof(kRows[0]) ==
                  static_cast<std::size_t>(Text::Count),
              "every Text needs exactly one row, in enum order");

Language g_language = Language::English;
void (*g_language_store)(Language) = nullptr;

}  // namespace

Language language() { return g_language; }

void set_language(Language value) {
  if (value >= Language::Count || value == g_language) return;
  g_language = value;
  if (g_language_store != nullptr) g_language_store(value);
}

void set_language_store_handler(void (*handler)(Language value)) {
  g_language_store = handler;
}

const char* text_in(Language language, Text id) {
  const auto index = static_cast<std::size_t>(id);
  if (index >= static_cast<std::size_t>(Text::Count)) return "";
  const Row& row = kRows[index];
  if (language == Language::TraditionalChinese &&
      row.traditional_chinese != nullptr &&
      row.traditional_chinese[0] != '\0') {
    return row.traditional_chinese;
  }
  return row.english;
}

const char* text(Text id) { return text_in(g_language, id); }

const char* language_name(Language value) {
  // Each language names itself in its own script: someone who cannot read the
  // current interface language still has to be able to find their own in the
  // list, which is the one moment the current language is useless to them.
  switch (value) {
    case Language::English:
      return text_in(Language::English, Text::LanguageEnglish);
    case Language::TraditionalChinese:
      return text_in(Language::TraditionalChinese,
                     Text::LanguageTraditionalChinese);
    case Language::Count:
      break;
  }
  return "";
}

std::string temperature_text(float celsius, int decimals) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%.*f°C", decimals,
                static_cast<double>(celsius));
  return buffer;
}

}  // namespace ui
