# ESP32-S3-RLCD-4.2 Dashboard

Waveshare ESP32-S3-RLCD-4.2 的 400 × 300 單色資訊儀表板。第一版韌體以大時鐘為預設頁，並透過自動輪播與三顆實體按鍵切換市場、天氣及室內溫濕度頁面。

## 介面設計 sample

![RLCD 大時鐘首頁與台股走勢頁版型](docs/assets/ui-layout-samples.svg)

這份 sample 記錄目前收斂的版型方向：大時鐘固定為預設首頁、資料頁保留隨時可見的時間、走勢圖盡量使用主區域高度、右側資訊格垂直置中並填滿空間。實機以 400 × 300 的 ST7305 反射式單色面板呈現，字型與線條已針對 1-bit 像素限制加粗。

目前韌體使用 deterministic mock data；Wi-Fi AP 設定頁、QR code、感測器與即時資料來源留待下一階段整合。接手狀態與驗收缺口請見 [HANDOFF.md](HANDOFF.md)。
