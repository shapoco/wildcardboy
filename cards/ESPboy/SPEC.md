# ESPboy カード

ESPboy カードは、ESP8266 を搭載した小型ゲーム機「ESPboy」(ESPboy2) の CPU 周辺回路を WildCardBoy のロジックカードとして実装したもの。
ESPboy 用のアプリバイナリ (Arduino ビルドの .bin) をそのまま実行することを目標とする。

## カードID

`ESPboy`

## 電源

LC3V3 (ホストシェルの 3.3V) を ESP8266 へ直接供給する。LDO は搭載しない。LCVSYS は使用しない。

## カード I/F

ESP8266 (ESP-12F / WeMos D1 mini 相当、Flash 4MB) と WildCardBus の接続:

|信号名|ESP8266 割り当て|説明|
|---|---|---|
|LCAUDIO_L|GPIO0|サウンド出力 (RC フィルタ + カップリングコンデンサ経由)|
|LCAUDIO_R|GPIO0|同上|
|LCIO0|未接続|LCLCD_RST は使用しない|
|LCIO1|GND|LCLCD_CS (常時アサート。ESPboy は LCD の CS を MCP23017 経由で制御するため、カード上に物理 CS 信号が無い)|
|LCIO2|GPIO14 (HSPI SCK)|LCLCD_SCK|
|LCIO3|GPIO13 (HSPI MOSI)|LCLCD_MOSI|
|LCIO4|GPIO16|LCLCD_DC|
|LCIO6|GPIO4|LCVIO_SDA|
|LCIO7|GPIO5|LCVIO_SCL|
|LCIO10|GPIO3 (RXD0)|ISP UART TX (ホスト → ESP8266)|
|LCIO11|GPIO1 (TXD0)|ISP UART RX (ESP8266 → ホスト)|
|LCIO12|GPIO0|BOOTSEL (open-drain、負論理)|
|LCIO13|RST|RESET (open-drain、負論理)|

GPIO0 はブートモード選択 (BOOTSEL) とサウンド出力を兼ねる (ESPboy の仕様)。
動作中のホスト側 LCIO12 は High-Z とし、サウンド出力の負荷にならないようにする。

## カード上の抵抗

|箇所|値|備考|
|---|---|---|
|LCVIO_SDA/SCL プルアップ|4.7kΩ x2|I2C バス|
|RST プルアップ|10kΩ||
|EN (CH_PD) プルアップ|10kΩ||
|GPIO0 プルアップ|10kΩ|ブートストラップ|
|GPIO2 プルアップ|10kΩ|ブートストラップ (NeoPixel は実装しない)|
|GPIO15 プルダウン|10kΩ|ブートストラップ|
|GPIO3 (RXD0) プルアップ|10kΩ|ホスト側 High-Z 時のフロート防止|

## 仮想 I/O エキスパンダ

ESPboy は MCP23017 (devaddr=0x20) をボタン入力・TFT CS・LED LOCK に使用する。
カードには実チップを搭載せず、ホストコントローラが模擬する ([spec/04_host_controller.md](../../spec/04_host_controller.md))。

|MCP23017ポート|LCIO|割り当て|
|---|---|---|
|GPA0|LCIO64|LEFT (左キー)|
|GPA1|LCIO65|UP (上キー)|
|GPA2|LCIO66|DOWN (下キー)|
|GPA3|LCIO67|RIGHT (右キー)|
|GPA4|LCIO68|ACT (Aボタン)|
|GPA5|LCIO69|ESC (Bボタン)|
|GPA6|LCIO70|LFT (Lバンパー)|
|GPA7|LCIO71|RGT (Rバンパー)|
|GPB0|(未使用)|TFT CS (アプリが出力設定する。ホストは使用しない)|
|GPB1|(未使用)|LED LOCK (同上)|

ESPboy 実機で同一バス上にある MCP4725 (バックライト調光 DAC、devaddr=0x60) は模擬しない
(ESPboy ライブラリは初期化時の probe が NACK になると DAC を無効化したまま動作する)。
NeoPixel (GPIO2) も実装しない。

## EEPROM

[profile.json](profile.json) を参照 (生成済みイメージ: [profile.hex](profile.hex))。

## プログラミング

LCIO10-13 を使用し、UART 経由 (Espressif シリアルブートローダプロトコル) で ESP8266 の SPI Flash に書き込む
([spec/04_host_controller.md](../../spec/04_host_controller.md))。
アプリバイナリは Flash オフセット 0x0000 に書き込む単一の .bin ファイルとする。
