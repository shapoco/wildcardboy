# Xiamocon RP2350 カード

Xiamocon RP2350 カードは、RP2350 を搭載した小型ゲーム機「[Xiamocon](https://github.com/shapoco/xiamocon)」(RP2350 版) の
CPU 周辺回路を WildCardBoy のロジックカードとして実装したもの。
Xiamocon 用のアプリバイナリ (UF2) をそのまま実行することを目標とする。
ESP32-S3 版の Xiamocon は対象外。

## カードID

`XiamoconRP`

## 電源

RP2350 とその周辺回路は LC3V3 で動作する (カード上のレギュレータは不要)。

## カード I/F

RP2350 (+ QSPI Flash) と WildCardBus の接続:

|信号名|RP2350 割り当て|説明|
|---|---|---|
|LCAUDIO_L|GPIO1|サウンド出力 (RC フィルタ + カップリングコンデンサ経由)|
|LCAUDIO_R|GPIO1|同上|
|LCIO0|未接続|LCLCD_RST は使用しない (ディスプレイリセットは仮想 I/O エキスパンダの PORTA-5。後述)|
|LCIO1|GPIO5|LCLCD_CS|
|LCIO2|GPIO2|LCLCD_SCK (TF と共有バス。後述)|
|LCIO3|GPIO3|LCLCD_MOSI (TF と共有バス。後述)|
|LCIO4|GPIO28|LCLCD_DC|
|LCIO6|GPIO6|LCVIO_SDA|
|LCIO7|GPIO7|LCVIO_SCL|
|LCIO8|GPIO3|LCTF_MOSI|
|LCIO9|GPIO0|LCTF_CS|
|LCIO10|GPIO2|LCTF_SCK|
|LCIO11|GPIO4|LCTF_MISO|
|LCIO12|QSPI_SS|BOOTSEL (open-drain、負論理)|
|LCIO13|RUN|RESET (open-drain、負論理)|
|LCUSB_DP|USB D+|UF2 書き込み用|
|LCUSB_DM|USB D-|UF2 書き込み用|

### LCD / TF の共有バス

Xiamocon は LCD と TF カードが SPI バス (SCK = GPIO2、MOSI = GPIO3) を共有し、CS でスレーブを選択する。
カード上で GPIO2 は LCIO2 (LCLCD_SCK) と LCIO10 (LCTF_SCK) の両方へ、
GPIO3 は LCIO3 (LCLCD_MOSI) と LCIO8 (LCTF_MOSI) の両方へ分岐して接続する。
TF カードへのアクセス中も LCIO2/3 がトグルするが、ホスト側の SPI キャプチャは LCLCD_CS でゲートされるため問題ない
(実機の共有バスと同じトポロジ)。

### 不使用ピン

|ピン|処置|
|---|---|
|GPIO26|未接続 (Xiamocon 実機では拡張ポート用)|
|GPIO27|電源ボタン (ハイアクティブ)。使用しないためカード上でプルダウン|

## カード上の抵抗

|箇所|値|備考|
|---|---|---|
|LCVIO_SDA/SCL プルアップ|4.7kΩ x2|I2C バス ([spec/02_wildcardbus.md](../../spec/02_wildcardbus.md))|
|RUN プルアップ|10kΩ||
|QSPI_SS プルアップ|10kΩ|ブートストラップ (BOOTSEL)|
|GPIO27 プルダウン|10kΩ|電源ボタン非アサート固定|

## 仮想 I/O エキスパンダ

Xiamocon は PCA9555 (devaddr=0x22) をボタン入力・ディスプレイリセット・オーディオミュートなどに使用する。
カードには実チップを搭載せず、ホストコントローラが模擬する ([spec/04_host_controller.md](../../spec/04_host_controller.md))。

|PCA9555ポート|LCIO|割り当て|
|---|---|---|
|PORTA-0..3|(未使用)|拡張ポート用 (ユーザ定義)|
|PORTA-4|(未使用)|ペリフェラルイネーブル (アプリが出力設定する。ホストは使用しない)|
|PORTA-5|LCIO69|ディスプレイリセット (LCLCD_RST 相当として扱う。ホスト側は段階的対応)|
|PORTA-6|(未使用)|オーディオミュート (今回は不使用。将来的にホストシェル側にミュート制御機能を設ける可能性あり)|
|PORTA-7|LCIO71|ファンクションスイッチ (ホストシェル側の L/R バンパー両方を割り当て)|
|PORTB-0|LCIO72|A ボタン (ホストシェル側は B ボタン)|
|PORTB-1|LCIO73|B ボタン (ホストシェル側は A ボタン)|
|PORTB-2|LCIO74|Y ボタン (ホストシェル側は X ボタン)|
|PORTB-3|LCIO75|X ボタン (ホストシェル側は Y ボタン)|
|PORTB-4|LCIO76|上キー|
|PORTB-5|LCIO77|下キー|
|PORTB-6|LCIO78|左キー|
|PORTB-7|LCIO79|右キー|

すべてローアクティブ。

Xiamocon 実機で同一バス上にあるバッテリー監視 ADC (ADC101C027、devaddr=0x52) は搭載も模擬もしない
(probe が NACK になったときにファームウェアがハングする場合は Xiamocon 側を修正する)。

## EEPROM

[profile.json](profile.json) を参照 (生成済みイメージ: [profile.hex](profile.hex))。

## プログラミング

BOOTSEL と RESET を使って RP2350 を MSC モードにし、USB 経由で UF2 を書き込む
([spec/04_host_controller.md](../../spec/04_host_controller.md))。
UF2 転送完了後も RP2350 側では Flash への書き込みが継続中である可能性に注意すること。
