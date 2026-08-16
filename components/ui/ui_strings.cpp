#include "ui_strings.hpp"

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
    {"Join the open Wi-Fi, then scan QR or open URL and enter PAGE PW",
     "連上開放熱點, 掃描 QR 或開啟網址, 輸入頁面密碼"},
    {"Not yet connected", "尚未連線"},
    {"QR unavailable - use SSID at left", "QR 無法顯示 - 請用左側熱點名稱"},

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
    {"Checking...", "檢查中..."},

    {"Next", "下一項"},
    {"Select", "選擇"},
    {"Back", "返回"},
    {"Prev", "上一頁"},
    {"Next", "下一頁"},

    {"BATTERY", "電池"},
    {"INDOOR", "室內"},
    {"MARKET", "市場"},
    {"WEATHER", "天氣"},
    {"HISTORY", "歷史"},
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
    {"HUMID", "偏濕"},
    {"MID", "盤中"},
    {"NOW", "現在"},
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

}  // namespace

Language language() { return g_language; }

void set_language(Language value) {
  if (value < Language::Count) g_language = value;
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

}  // namespace ui
