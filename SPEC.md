# WildCardBoy

## コンセプト

MCU を差し替えることができるハンドヘルドゲームコンソール。

- 本体には ILI9488 互換 LCD とキーパッド (方向キー、ABXYボタンなど) を搭載。
- 本体にはゲーム自体の実行機能は無く、背面に「ロジックカード」を差し込むスロットがある。
- ロジックカードには頭脳となる MCU が搭載されており、ゲームプログラムはそこで実行される。
- ロジックカードには例えば TinyJoyPad や Arduboy のようなゲーム機の CPU 周辺回路が搭載される。
- 新しいゲーム機に対応するときは、ロジックカードだけを作ればよい。
- 本体には TF カードスロットが搭載され、そこに格納されたゲームプログラムをロジックカード上の MCU に書き込むことができる。

## 本体機能

本体側は RP2350B をコアとして、システム全体を制御する「ホストコントローラ」を構成する。

- ロジックカードの検出
- PCA9555 を経由したキーパッド入力の取得
- ロジックカードへのキーパッド入力状態の送信
- ロジックカードからの LCD 描画命令の受信
- LCD 制御
- TF カード上のファイルシステムのブラウズ
- TF カードからバイナリファイルを読み込んでロジックカード上の MCU へのプログラム書き込み (SPI or USB)

## インタフェース

### キーパッド

ボタン類は全て PCA9555 (devaddr=0x21) 経由で読み取られる。
Home ボタンは本体側の機能の呼び出しに使用する。

|GPIO|割り当て|説明|
|---|---|---|
|GPIO28|AUX_SDA|I2C データライン|
|GPIO29|AUX_SCL|I2C クロックライン|

|PCA9555ポート|割り当て|説明|
|---|---|---|
|P0_0|HKEY_L|左キー|
|P0_1|HKEY_R|右キー|
|P0_2|HKEY_U|上キー|
|P0_3|HKEY_D|下キー|
|P0_4|HKEY_A|Aボタン|
|P0_5|HKEY_B|Bボタン|
|P0_6|HKEY_X|Xボタン|
|P0_7|HKEY_Y|Yボタン|
|P1_0|HKEY_STA|STARTボタン|
|P1_1|HKEY_SEL|SELECTボタン|
|P1_2|HKEY_BL|Lバンパー|
|P1_3|HKEY_BR|Rバンパー|
|P1_4|HKEY_HOME|Homeボタン|

### LCD I/F

ILI9488 互換の 320x480 ドットの液晶を横に倒して使用する。8bit パラレル接続。

|GPIO|割り当て|
|---|---|
|GPIO15|HLCD_RST|
|GPIO16|HLCD_D0|
|GPIO17|HLCD_D1|
|GPIO18|HLCD_D2|
|GPIO19|HLCD_D3|
|GPIO20|HLCD_D4|
|GPIO21|HLCD_D5|
|GPIO22|HLCD_D6|
|GPIO23|HLCD_D7|
|GPIO24|HLCD_DC|
|GPIO25|HLCD_WR|
|GPIO26|HLCD_RD|
|GPIO27|HLCD_CS|

### TF カード I/F

SPI 接続。割り当ては T.B.D.

|GPIO|割り当て|
|---|---|
|GPIO32|HTF_MISO|
|GPIO33|HTF_CS|
|GPIO34|HTF_SCK|
|GPIO35|HTF_MOSI|

### オーディオ I/F

T.B.D.

### ロジックカード I/F (WildCardBus)

2x12 ピンのピンソケット。

|ヘッダピン番号|GPIO|信号名|説明|
|---|---|---|---|
|1|-|LCAUDIO_L|オーディオ左チャンネル|
|2|-|LCAUDIO_R|オーディオ右チャンネル|
|3|-|GND|グランド|
|4|-|NC|未接続|
|5|GPIO0|LCIO0|ロジックカード側の GPIO0|
|6|GPIO1|LCIO1|ロジックカード側の GPIO1|
|7|GPIO2|LCIO2|ロジックカード側の GPIO2|
|8|GPIO3|LCIO3|ロジックカード側の GPIO3|
|9|GPIO4|LCIO4|ロジックカード側の GPIO4|
|10|GPIO5|LCIO5|ロジックカード側の GPIO5|
|11|GPIO6|LCIO6|ロジックカード側の GPIO6|
|12|GPIO7|LCIO7|ロジックカード側の GPIO7|
|13|GPIO8|LCIO8|ロジックカード側の GPIO8|
|14|GPIO9|LCIO9|ロジックカード側の GPIO9|
|15|GPIO10|LCIO10|ロジックカード側の GPIO10|
|16|GPIO11|LCIO11|ロジックカード側の GPIO11|
|17|GPIO12|LCIO12|ロジックカード側の GPIO12|
|18|GPIO13|LCIO13|ロジックカード側の GPIO13|
|19|GPIO28|AUX_SDA|I2C データライン|
|20|GPIO29|AUX_SCL|I2C クロックライン|
|21|GPIO30|LCUSB_DP|USB D+|
|22|GPIO31|LCUSB_DM|USB D-|
|23|-|LC3V3|3.3V 電源|
|24|-|LC5V|5V 電源|

#### カードIDチップ

AUX_SDA/SCL は、ロジックカード上の EEPROM (24LC256, devaddr=0x50) に接続されており、本体側から読み出すことで、ロジックカードの種類を判別することができる。

内容は T.B.D.。

#### LCD I/F が I2C の場合

|信号名|割り当て|
|---|---|
|LCIO2|LCLCD_SDA|
|LCIO3|LCLCD_SCL|

#### LCD I/F が SPI の場合

|信号名|割り当て|
|---|---|
|LCIO0|LCLCD_RST|
|LCIO1|LCLCD_CS|
|LCIO2|LCLCD_SCK|
|LCIO3|LCLCD_MOSI|
|LCIO4|LCLCD_DC|

#### LCD I/F が 8bit パラレルの場合

|信号名|割り当て|
|---|---|
|LCIO0|LCLCD_RST|
|LCIO1|LCLCD_CS|
|LCIO2|LCLCD_WR|
|LCIO3|LCLCD_D0|
|LCIO4|LCLCD_D1|
|LCIO5|LCLCD_D2|
|LCIO6|LCLCD_D3|
|LCIO7|LCLCD_D4|
|LCIO8|LCLCD_D5|
|LCIO9|LCLCD_D6|
|LCIO10|LCLCD_D7|
|LCIO11|LCLCD_DC|

#### キーパッド I/F がパラレルの場合

ロジックカード側の LCD I/F がパラレル以外の場合で、TF カード I/F を持たず、使用するキーが方向キーと A、B、START、SELECT のみの場合は、キーパッド I/F はパラレル接続となる。

|信号名|割り当て|説明|
|---|---|---|
|LCIO5|LCKEY_L|左キー|
|LCIO6|LCKEY_R|右キー|
|LCIO7|LCKEY_U|上キー|
|LCIO8|LCKEY_D|下キー|
|LCIO9|LCKEY_A|Aボタン|
|LCIO10|LCKEY_B|Bボタン|
|LCIO11|LCKEY_STA|STARTボタン|
|LCIO12|LCKEY_SEL|SELECTボタン|

#### キーパッド I/F が I2C の場合

キーパッド I/F がパラレルで使用できない場合は I2C 接続となる。I2C は AUX_SDA/AUX_SCL を通じてロジックカード上の PCA9555 (devaddr=0x20) に接続される。

|PCA9555ポート|割り当て|説明|
|---|---|---|
|P0_0|LCKEY_L|左キー|
|P0_1|LCKEY_R|右キー|
|P0_2|LCKEY_U|上キー|
|P0_3|LCKEY_D|下キー|
|P0_4|LCKEY_A|Aボタン|
|P0_5|LCKEY_B|Bボタン|
|P0_6|LCKEY_X|Xボタン|
|P0_7|LCKEY_Y|Yボタン|
|P1_0|LCKEY_STA|STARTボタン|
|P1_1|LCKEY_SEL|SELECTボタン|
|P1_2|LCKEY_BL|Lバンパー|
|P1_3|LCKEY_BR|Rバンパー|

#### TF カード I/F

ロジックカード側が TF カード I/F を要求する場合は、本体側の TF カードスロットがロジックカードへ接続される。

|信号名|割り当て|
|---|---|
|LCIO5|LCTF_MISO|
|LCIO6|LCTF_CS|
|LCIO7|LCTF_SCK|
|LCIO8|LCTF_MOSI|

ロジックカードが TF カードを占有する間、ホストコントローラは TF カードへはアクセスできなくなる。
TF カードスロットをホストコントローラに接続するかロジックカードに接続するかは、ホストコントローラの GPIO で切り替える。

|GPIO|信号名|説明|
|---|---|---|
|T.B.D.|T.B.D.|T.B.D.|

#### USB I/F

USB I/F は、将来的にホストコントローラが USB ホストとなってロジックカード上の USB デバイスと通信するために使用される。
ロジックカード上の MCU が RP2040 や RP2350 で、ホストから UF2 を書き込むようなシーンが想定される。

### 電源I/F

電源 I/F は、ロジックカードへの電源供給を制御する。

|GPIO|信号名|説明|
|---|---|---|
|T.B.D.|T.B.D.|LC3V3 のイネーブル|
|T.B.D.|T.B.D.|LC5V のイネーブル|

## ホストコントローラ

### 起動

起動時、ロジックカードの EEPROM からカードIDを読み出し、カードの種類を判別する。
カードIDに応じて、ロジックカードの LCD I/F、キーパッド I/F、TF カード I/F の接続方法を切り替える。
ロジックカードを検出できなかった場合は「システムメニュー」を起動する。

### システムメニュー

起動時は「システムメニュー」が動作する。
将来的には TF カード上のファイルをブラウズして、ロジックカード上の MCU に書き込む機能を持たせる予定。
現状は「No Logic Card」と表示しておく。

### ゲームモード

本体側のキーパッド入力をロジックカードに送信する。
ロジックカードからの LCD 描画命令は LcdTap (submodule/lcdtap/) で受けてフレームバッファに描画し、本体側の LCD に表示する。
本体側 LCD への転送は、フレームバッファの更新された部分のみに対して行う。

## TJP カード

TJP カードは、ATtiny85 を搭載した小型ゲーム機「TinyJoyPad」の CPU 周辺回路を WildCardBoy のロジックカードとして実装したもの。

### カード I/F

|信号名|ATtiny85 割り当て|説明|
|---|---|---|
|LCAUDIO_L|PB4|オーディオ左チャンネル|
|LCAUDIO_R|PB4|オーディオ右チャンネル|
|LCIO2|PB0|SSD1309 制御信号 (SDA)|
|LCIO3|PB2|SSD1309 制御信号 (SCL)|
|LCIO5|抵抗を介して PB5|左キー (open-drain)|
|LCIO6|抵抗を介して PB5|右キー (open-drain)|
|LCIO7|抵抗を介して PB3|上キー (open-drain)|
|LCIO8|抵抗を介して PB3|下キー (open-drain)|
|LCIO9|PB1|Aボタン (open-drain)|
|LCIO13|PB5|RESET (open-drain)|

LCIO2、3、9、13 は ATtiny85 へのプログラムの書き込み時にも使用される。

|信号名|ATtiny85 割り当て|説明|
|---|---|---|
|LCIO2|PB0|MOSI|
|LCIO3|PB2|SCK|
|LCIO9|PB1|MISO|
|LCIO13|PB5|RESET (open-drain)|

方向キーはロジックカード上で次のように配線され、各キーの押下状態は ATtiny85 の ADC で読み取られる (TinyJoyPad の仕様)。

```
                            LC3V3
                              |
                            22kOhm
                              |
LCIO5 (LCKEY_L) --- 88kOhm ---+--- PB5 (ATtiny85)
                              |
LCIO6 (LCKEY_R) --- 33kOhm ---+


                            LC3V3
                              |
                            22kOhm
                              |
LCIO7 (LCKEY_U) --- 33kOhm ---+--- PB3 (ATtiny85)
                              |
LCIO8 (LCKEY_D) --- 88kOhm ---+
```
