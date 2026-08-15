# ESP32-S3-RLCD-4.2 官方開發研究

研究日期：2026-08-15（Asia/Taipei）  
範圍：只採用 Waveshare、Espressif 與 Waveshare 官方 GitHub 的第一方資料。官方 repo 內容以 commit [`eb1f634`](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/commit/eb1f63427d735a22b9c30e22fa63ebddae1834d3) 為研究快照。

## 結論先行

- 這塊板是 `ESP32-S3-WROOM-1-N16R8`，官方配置就是 **16 MB Quad-SPI Flash + 8 MB Octal-SPI PSRAM**；本輪實測板的 16 MB Flash / 8 MB PSRAM 與官方規格一致，沒有容量差異。[Waveshare 產品文件](https://docs.waveshare.com/ESP32-S3-RLCD-4.2)；[Espressif 模組 datasheet](https://www.espressif.com/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)
- 4.2 吋面板是 **ST7305、300 × 400、單色全反射式 LCD**，沒有背光。官方 LVGL 範例將它旋轉為 400 × 300 橫向畫布。[Waveshare 產品文件](https://docs.waveshare.com/ESP32-S3-RLCD-4.2)；[官方 ST7305 資料入口](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/Resources-And-Documents)；[官方 ESPHome 板級範例](https://docs.waveshare.com/ESP32-ESPHome-Tutorials/Example-RLCD-Voice)
- **此型號沒有觸控面板或 touch controller。** 這是由官方功能表、原理圖、pin map 與官方 demo 都沒有 touch IC／touch IRQ／touch bus 所得的結論；不可把名稱相近的 `ESP32-S3-Touch-LCD-*` 驅動套上來。[產品文件](https://docs.waveshare.com/ESP32-S3-RLCD-4.2)；[官方原理圖](https://files.waveshare.com/wiki/ESP32-S3-RLCD-4.2/ESP32-S3-RLCD-4.2-schematic.pdf)
- 官方支援兩條主要路徑：**ESP-IDF >= 5.5.0**，或 **arduino-esp32 >= 3.3.0**。顯示層可用 LVGL 8.3.11、LVGL 9.3.0 或 U8g2。[ESP-IDF 指南](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/Development-Environment-Setup-ESP-IDF)；[Arduino 指南](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/Development-Environment-Setup-Arduino)
- 專案開發建議以 ESP-IDF 5.5.x 為基準，先從官方 `10_FactoryProgram` 複製最小板級支援，再把 UI／應用邏輯與 BSP 分層。Arduino 適合快速驗證；U8g2 適合單色 UI 和較小的依賴面。

## 硬體規格與實測差異

| 項目 | 官方規格 | 本輪實測／判讀 | 結論 |
| --- | --- | --- | --- |
| MCU module | ESP32-S3-WROOM-1-N16R8，雙核 Xtensa LX7，最高 240 MHz | 已連接板回報 ESP32-S3 | 相符 |
| Flash | 16 MB Quad SPI | 16 MB | 相符；完整備份長度應為 `0x1000000` / 16,777,216 bytes |
| PSRAM | 8 MB Octal SPI | 8 MB | 相符；ESP-IDF 用 Octal 80 MHz，Arduino 選 OPI PSRAM |
| 顯示 | ST7305，300 × 400，全反射、無背光 | 板型與官方配置相符 | 橫屏 UI 通常用 400 × 300 |
| 觸控 | 無 | 無 touch controller 可枚舉 | 不建立 touch driver / LVGL input device |

來源：[Waveshare features / onboard resources](https://docs.waveshare.com/ESP32-S3-RLCD-4.2)、[官方 repo README](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/eb1f63427d735a22b9c30e22fa63ebddae1834d3/README.md)、[Espressif N16R8 對照表](https://www.espressif.com/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)、[官方 Factory `sdkconfig.defaults`](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/eb1f63427d735a22b9c30e22fa63ebddae1834d3/02_Example/ESP-IDF/10_FactoryProgram/sdkconfig.defaults)。

注意：`esptool flash-id` 能直接確認 Flash 容量；PSRAM 容量通常由啟動 log、ESP-IDF 的 `esp_psram_get_size()` 或應用程式確認，不應從 Flash ID 推測。[esptool basic commands](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html)

## 板載周邊與接腳

### 核心接腳表

| 周邊 | IC / 功能 | GPIO / bus | 備註 |
| --- | --- | --- | --- |
| RLCD | ST7305 | SCK 11、MOSI 12、DC 5、CS 40、RST 41、TE 6 | SPI mode 0、write-only，MISO 不用；官方驅動用 10 MHz |
| USB-C | USB Serial/JTAG | D- 19、D+ 20 | 燒錄、monitor、JTAG 共用原生 USB |
| I2C | 共用 bus | SDA 13、SCL 14 | RTC、SHTC3、audio codec 控制介面共用 |
| RTC | PCF85063A | I2C address `0x51` | 獨立 RTC 電池只能用可充電 ML1220 類型，不可用 CR1220 |
| 溫濕度 | SHTC3 | I2C address `0x70` | 官方 demo 每秒讀取一次 |
| audio DAC | ES8311 | I2C + I2S | speaker output |
| audio ADC | ES7210 | I2C + I2S | 雙麥克風輸入 |
| I2S | audio data | DOUT 8、BCLK 9、DIN 10、MCLK 16、LRCLK 45 | `DOUT` 是到 speaker codec 的資料；`DIN` 是 mic ADC 回來的資料 |
| speaker amp | NS4150B enable | GPIO46 | 必須拉高才有聲音；GPIO46 同時是 ESP32-S3 strapping pin，reset/download 時不要外部強拉高 |
| microSD | SDMMC 1-bit | CLK 38、CMD 21、D0 39 | 官方 class 預設就是這三腳；卡片用 FAT/FatFs |
| battery sense | 18650 分壓 ADC | GPIO4 / ADC1 channel 3 | 官方板級文件標示為 3 倍分壓，電壓需做軟體換算 |
| BOOT | active-low button | GPIO0 | reset/power-on 時拉低進 ROM download mode；應用中也被當一般按鍵 |
| KEY | active-low button | GPIO18 | 使用者按鍵 |
| PWR | 電源管理按鍵 | 非一般 app GPIO | 短按開機、長按關機 |
| UART0 | console/header | TX 43、RX 44 | 原理圖亦保留 UART；一般開發優先用 USB Serial/JTAG |

來源：[官方原理圖](https://files.waveshare.com/wiki/ESP32-S3-RLCD-4.2/ESP32-S3-RLCD-4.2-schematic.pdf)、[官方 pin-specific ESPHome 範例](https://docs.waveshare.com/ESP32-ESPHome-Tutorials/Example-RLCD-Voice)、[Factory `user_config.h`](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/eb1f63427d735a22b9c30e22fa63ebddae1834d3/02_Example/ESP-IDF/10_FactoryProgram/main/user_config.h)、[Factory SD BSP](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/eb1f63427d735a22b9c30e22fa63ebddae1834d3/02_Example/ESP-IDF/10_FactoryProgram/components/port_bsp/sdcard_bsp.h)、[Waveshare FAQ](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/FAQ)。

## USB、電源、BOOT 與連線流程

### 正常開發流程

1. 使用確定能傳資料的 USB-C cable；Type-C 口同時供電、燒錄與印 log。[Waveshare onboard resources](https://docs.waveshare.com/ESP32-S3-RLCD-4.2)
2. 短按 `PWR` 開機。正常情況下原生 USB Serial/JTAG 會枚舉成 serial device，ESP32-S3 可透過這個固定功能 USB controller 執行 flash、console 與 JTAG。[Espressif USB Serial/JTAG](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/usb-serial-jtag-console.html)
3. 用 `ls /dev/cu.*`（macOS）、Device Manager（Windows）或 `ls /dev/ttyACM* /dev/ttyUSB*`（Linux）找出 port；插拔前後比對比只看固定名稱可靠。

### 無法上傳／serial port 消失時

Waveshare 對這塊沒有 RESET 鍵的板子給的操作是：**按住 BOOT，再重新開機**，即可進 download mode。[Waveshare FAQ](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/FAQ)

實際操作：

1. 長按 `PWR` 關機。
2. 按住 `BOOT`。
3. 短按 `PWR` 開機，持續按住 `BOOT` 約 1 秒，再放開。
4. 重新列出 serial ports；原生 USB 重新枚舉時裝置名稱可能改變。
5. 再執行 `esptool --chip esp32s3 -p "$PORT" flash-id` 或 `idf.py -p "$PORT" flash`。

原理是 ESP32-S3 在 reset 時偵測 GPIO0：低電位進 ROM serial bootloader，高電位正常從 Flash 啟動。GPIO46 也必須保持浮接或低電位才能進 downloader。[Espressif boot mode selection](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html)

## 官方程式碼、工具鏈與 demo

官方 repo：[waveshareteam/ESP32-S3-RLCD-4.2](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2)

目錄重點：

```text
01_Arduino_Libraries/       官方包好的 SensorLib、U8g2、LVGL 8/9
02_Example/Arduino/         10 個 Arduino 範例
02_Example/ESP-IDF/         11 個 ESP-IDF 範例
03_Firmware/                Factory 與 XiaoZhi 預編譯韌體
Tools-Configuration.png     官方 Arduino Tools 選項
```

### ESP-IDF

- Waveshare 要求 ESP-IDF **5.5.0 以上**，文件示範 5.5.2。[Waveshare ESP-IDF guide](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/Development-Environment-Setup-ESP-IDF)
- 官方 `sdkconfig.defaults` 設定 target `esp32s3`、16 MB Flash、QIO Flash、Octal PSRAM 80 MHz、LVGL refresh period 1 ms。[Factory `sdkconfig.defaults`](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/eb1f63427d735a22b9c30e22fa63ebddae1834d3/02_Example/ESP-IDF/10_FactoryProgram/sdkconfig.defaults)
- 範例：Wi-Fi AP、Wi-Fi STA、battery ADC、PCF85063、SHTC3、SD card、audio loopback、LVGL 8、LVGL 9、FactoryProgram、U8g2。[Waveshare ESP-IDF example list](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/Development-Environment-Setup-ESP-IDF)
- `10_FactoryProgram` 是整合所有板載硬體的最佳 BSP 參考，但不是乾淨的 production architecture；新專案應抽出 `display/i2c/sd/audio/button/power` BSP，再把 UI 與 domain logic 放到上層。

建置與燒錄：

```bash
cd 02_Example/ESP-IDF/10_FactoryProgram
idf.py set-target esp32s3
idf.py build
idf.py -p "$PORT" flash monitor
```

`flash` 會自動 build；`monitor` 預設 115200，離開用 `Ctrl+]`。也可合併成一行 `idf.py -p "$PORT" flash monitor`。[Espressif build/flash/monitor](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/linux-macos-start-project.html)；[`idf.py` reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/tools/idf-py.html)

### Arduino

Waveshare 要求 `arduino-esp32 >= 3.3.0`，官方 `Tools-Configuration.png` 的設定是：[官方設定圖](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/eb1f63427d735a22b9c30e22fa63ebddae1834d3/Tools-Configuration.png)

| Arduino Tools 項目 | 值 |
| --- | --- |
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| CPU Frequency | 240MHz (WiFi) |
| USB DFU On Boot | Disabled |
| Flash Mode | QIO 80MHz |
| Flash Size | 16MB (128Mb) |
| Partition Scheme | 16M Flash (3MB APP / 9.9MB FATFS) |
| PSRAM | OPI PSRAM |
| Upload Mode | UART0 / Hardware CDC |
| Upload Speed | 921600；不穩時降至 460800 或 115200 |
| USB Mode | Hardware CDC and JTAG |

官方 Arduino 範例與 ESP-IDF 基本相同，但沒有 `10_FactoryProgram`；顯示範例包含 LVGL 8、LVGL 9 與 U8g2。[Waveshare Arduino guide](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/Development-Environment-Setup-Arduino)

### LVGL / U8g2 與顯示驅動

- 官方範例綁定 **LVGL 8.3.11** 或 **LVGL 9.3.0**；不可把 v8 driver/header 與 v9 library 混用。[Arduino library compatibility](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/Development-Environment-Setup-Arduino)
- 顯示不是一般 RGB565 TFT。ST7305 是單色 RLCD，官方 driver 把 LVGL color buffer 轉換成面板位元排列後，以 SPI 寫入；其 buffer/LUT 配置在 PSRAM。[官方 `display_bsp.cpp`](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/eb1f63427d735a22b9c30e22fa63ebddae1834d3/02_Example/ESP-IDF/10_FactoryProgram/components/port_bsp/display_bsp.cpp)
- 官方 driver 設定 SPI mode 0、10 MHz、無 MISO；畫面原生 300 × 400，LVGL sample 以 400 × 300 運作。[同上](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/eb1f63427d735a22b9c30e22fa63ebddae1834d3/02_Example/ESP-IDF/10_FactoryProgram/components/port_bsp/display_bsp.cpp)
- U8g2 適合文字、圖示與單色 dashboard。Waveshare 的一般 RLCD 教程指出原生 ST7305 300×400 支援從 U8g2 2.36.19 起可用；板級官方包也提供專用 U8g2 範例。[Waveshare RLCD/U8g2 tutorial](https://docs.waveshare.net/ESP32-Peripheral-Tutorials/Display/RLCD/)；[官方 U8g2 example](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/tree/eb1f63427d735a22b9c30e22fa63ebddae1834d3/02_Example/Arduino/10_U8G2_Test)
- 由於沒有觸控層，LVGL 只註冊 display driver/flush callback；若需要互動，以 BOOT/KEY 或外接 GPIO input 實作。

## 官方韌體備份與恢復

### 1. 開發前先做整顆 Flash 備份

先進入 download mode，確認 port 與容量：

```bash
esptool --chip esp32s3 -p "$PORT" flash-id
esptool --chip esp32s3 -p "$PORT" read-flash 0x0 0x1000000 factory-backup.bin
wc -c factory-backup.bin
shasum -a 256 factory-backup.bin
```

`read-flash` 的參數是起始位址、長度、輸出檔；也可用 `ALL` 讓 esptool 自動偵測容量。本板已知是 16 MB，固定讀 `0x1000000` 比較容易驗收檔案長度。[Espressif `read-flash`](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html)

備份檔可能包含 Wi-Fi credential、NVS、裝置識別與使用者資料，應視為敏感檔，不要 commit 到公開 repo。

### 2. 用實機完整備份還原

```bash
esptool --chip esp32s3 -p "$PORT" write-flash 0x0 factory-backup.bin
```

如果要確認寫入內容：

```bash
esptool --chip esp32s3 -p "$PORT" verify-flash 0x0 factory-backup.bin
```

`write-flash` 接受 offset/file pair，並會自動 erase 受影響 sectors；完整 16 MB dump 從 `0x0` 寫回即可。[Espressif `write-flash`](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html)；[`verify-flash`](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/advanced-commands.html)

### 3. 用 Waveshare 官方 Factory 韌體恢復

官方 repo 提供 [`03_Firmware/01_Factory_V1.bin`](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/eb1f63427d735a22b9c30e22fa63ebddae1834d3/03_Firmware/01_Factory_V1.bin)。研究快照的固定下載位址與雜湊：

```text
URL: https://raw.githubusercontent.com/waveshareteam/ESP32-S3-RLCD-4.2/eb1f63427d735a22b9c30e22fa63ebddae1834d3/03_Firmware/01_Factory_V1.bin
size: 4,548,144 bytes
sha256: d0591315a722d33f4a08931a0341ab840a6c15c56b289d621e8fc18bec8d55a8
```

這個檔案以 ESP image header 開始，大小與官方 merged factory image 相符，應從 `0x0` 寫入。Espressif 對 merged binary 的標準用法也是 `write-flash 0x0 merged.bin`。[Espressif merge-bin / write-flash](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html)

乾淨恢復流程（**erase 會刪除整顆 Flash，務必先有可驗證備份**）：

```bash
curl -fL -o 01_Factory_V1.bin \
  'https://raw.githubusercontent.com/waveshareteam/ESP32-S3-RLCD-4.2/eb1f63427d735a22b9c30e22fa63ebddae1834d3/03_Firmware/01_Factory_V1.bin'
printf '%s  %s\n' \
  d0591315a722d33f4a08931a0341ab840a6c15c56b289d621e8fc18bec8d55a8 \
  01_Factory_V1.bin | shasum -a 256 -c -
esptool --chip esp32s3 -p "$PORT" erase-flash
esptool --chip esp32s3 -p "$PORT" write-flash 0x0 01_Factory_V1.bin
```

如果只想寫回官方 image、保留其 EOF 之後的 Flash，可省略 `erase-flash`；但那不等同乾淨出廠狀態，舊的尾端 FATFS／未覆寫區域仍可能存在。`erase-flash` 在 Secure Boot 或 Flash Encryption 啟用時預設會被阻擋，除非明確強制；不要對未知 security state 使用 `--force`。[Espressif erase safety](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html)

## 常見陷阱

1. **選錯板或記憶體模式**：Arduino 必須是 16 MB Flash + OPI PSRAM；ESP-IDF 必須是 Octal PSRAM 80 MHz。選成 QSPI PSRAM、8 MB Flash 或錯誤 partition scheme，可能編譯成功但 runtime crash 或空間錯誤。[官方 Arduino 設定圖](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/eb1f63427d735a22b9c30e22fa63ebddae1834d3/Tools-Configuration.png)
2. **LVGL 主版本混用**：v8.3.11 與 v9.3.0 API、driver glue 不相容，務必讓 example、`lv_conf.h`、library 版本成套。[Waveshare Arduino guide](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/Development-Environment-Setup-Arduino)
3. **把 RLCD 當彩色 TFT**：面板是單色 ST7305，不能直接套 ST7789/ILI9341/RGB565 flush。LVGL v9 demo 裡的 asset 名稱出現 `RGB565A8` 不代表面板本身是彩色；仍由 BSP 做單色轉換。[官方 display BSP](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/eb1f63427d735a22b9c30e22fa63ebddae1834d3/02_Example/ESP-IDF/10_FactoryProgram/components/port_bsp/display_bsp.cpp)
4. **期待背光亮起**：它完全沒有背光；環境越亮越清楚，暗處看起來黯淡是正常特性。[Waveshare FAQ](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/FAQ)
5. **誤找 touch driver**：這款無觸控；官方 `Touch-LCD` 系列的 controller/pin 不適用。[官方產品文件](https://docs.waveshare.com/ESP32-S3-RLCD-4.2)
6. **USB port 在燒錄後消失**：程式可能改掉 USB console 設定、crash 太早，或裝置重枚舉；用「按住 BOOT + 重新開機」回 ROM downloader。[Waveshare FAQ](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/FAQ)
7. **GPIO46 影響下載模式**：它既是 speaker amp enable 又是 strapping pin；不要外加強 pull-up，應在 app 啟動後才設 high。[Espressif boot mode selection](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html)
8. **SD 卡 bus 認錯**：官方範例用 SDMMC 1-bit（38/21/39），不是與 RLCD 共用的 SPI bus。[官方 SD BSP](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/eb1f63427d735a22b9c30e22fa63ebddae1834d3/02_Example/ESP-IDF/10_FactoryProgram/components/port_bsp/sdcard_bsp.h)
9. **RTC 電池用錯**：RTC holder 會充電，只能用 ML1220 或相容可充電電池，不能裝 CR1220。[Waveshare FAQ](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/FAQ)
10. **電池首次無法開機**：裝入 18650 後需先接 Type-C 啟動電源路徑，再拔線測試電池供電。[Waveshare FAQ](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/FAQ)
11. **高 baud 不穩**：921600 是官方 Arduino 設定，不是所有 cable/hub/host 都可靠；連線錯誤先降到 460800 或 115200。[Espressif esptool baud guidance](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-options.html)
12. **Flash 前沒有完整備份**：官方 Factory image 能恢復 demo，但不能恢復原機 NVS、校正資料或個人設定；第一次改寫前先保存 16 MB full dump。

## 建議納入專案開發 skill 的固定守則

1. 開始任何 flash 動作前：列出 port、執行 `flash-id`、確認 16 MB、確認已存在 16 MB 備份與 SHA-256。
2. 預設技術棧：ESP-IDF 5.5.x；`target=esp32s3`、Flash QIO 16 MB、PSRAM Octal 80 MHz。
3. 顯示 BSP 固定記錄 ST7305 與 11/12/5/40/41/6 pin map，不允許以通用 TFT 設定覆寫。
4. UI 座標預設 400 × 300 landscape；driver 層保留 native 300 × 400 的轉換責任。
5. 不建立 touch abstraction，除非未來真的外接 touch controller；板上互動使用 BOOT(GPIO0) / KEY(GPIO18)。
6. 每次 flash 後至少驗證：serial boot log、Flash/PSRAM 容量、顯示完整 refresh、KEY/BOOT、I2C probe、battery ADC；改 audio/SD 時再加對應 smoke test。
7. 遇到無法連線先走 BOOT + PWR recovery，不先 erase；`erase-flash` 只能在已驗證備份後執行。
8. 官方 source pin 到 commit 或 release，避免 `main` 更新後 LVGL/BSP/firmware hash 無聲漂移。

## 第一方來源索引

- [Waveshare ESP32-S3-RLCD-4.2 產品文件](https://docs.waveshare.com/ESP32-S3-RLCD-4.2)
- [Waveshare Resources：原理圖、datasheet、官方 repo](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/Resources-And-Documents)
- [Waveshare Arduino 開發指南](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/Development-Environment-Setup-Arduino)
- [Waveshare ESP-IDF 開發指南](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/Development-Environment-Setup-ESP-IDF)
- [Waveshare FAQ](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/FAQ)
- [Waveshare 官方 GitHub repo](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2)
- [Waveshare 原理圖 PDF](https://files.waveshare.com/wiki/ESP32-S3-RLCD-4.2/ESP32-S3-RLCD-4.2-schematic.pdf)
- [Espressif ESP32-S3-WROOM-1 datasheet](https://www.espressif.com/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)
- [Espressif ESP-IDF `idf.py`](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/tools/idf-py.html)
- [Espressif USB Serial/JTAG console](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/usb-serial-jtag-console.html)
- [Espressif boot mode selection](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html)
- [Espressif esptool basic commands](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html)
