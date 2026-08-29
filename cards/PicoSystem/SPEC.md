# PicoSystem カード

PicoSystem カードは、RP2040 を搭載した Pimoroni の小型ゲーム機「[PicoSystem](https://shop.pimoroni.com/products/picosystem)」の
CPU 周辺回路を WildCardBoy のロジックカードとして実装したもの。
PicoSystem 用のアプリバイナリ (UF2) をそのまま実行することを目標とする。

## カードID

`PicoSystem`

## LCD I/F 方式: デシリアライザによる 8bit パラレル化

PicoSystem のファームウェアは画面転送時に SPI クロックを最大約 62.5 MHz
(システムクロック 250 MHz へのオーバークロック時) で駆動する。
ホストシェルの SPI 直接キャプチャでは 312 MHz 化しても化けが解消しなかったため
(サンプリングマージン不足)、カード上にデシリアライザを搭載して 8bit パラレルに変換し、
WildCardBus の 8bit パラレル LCD 固定割り当てで接続する
(LcdTap-Pico2 Universal の PicoSystem 対応と同一構成。
[lcdtap issue 0001](../../submodule/lcdtap/issues/0001_spi_input_change_to_parallel.md) /
[0003](../../submodule/lcdtap/issues/0003_shift_register_change_to_hc595.md))。

```
RP2040 GPIO6 (SCLK) ──┬─→ 74HC595 SRCLK
                      └─→ 74HC4040 CP
RP2040 GPIO7 (MOSI) ────→ 74HC595 SER
RP2040 GPIO5 (CS)   ──┬─→ 74HC4040 CLR (active-high)
                      └─→ LCIO1 (LCLCD_CS)
74HC4040 Q3 (SCLK/8) ──→ 74AHC1G04 ──→ BCLK ──┬─→ LCIO2 (LCLCD_WR)
                                              └─→ 74HC595 RCLK
74HC595 Q1-Q8 (D0-D7) ─────────────────────────→ LCIO3-10 (LCLCD_D0-7)
RP2040 GPIO9 (DC) ─────────────────────────────→ LCIO11 (LCLCD_DC)
RP2040 GPIO4 (LCD_RST) ────────────────────────→ LCIO0 (LCLCD_RST)
```

CS=High の間はカウンタがクリアされ BCLK が出ないため、ホスト側は BCLK の
立ち上がり毎に D0-7 と DC をサンプリングするだけでよい (ピクセルレートは最大 7.8125 MHz)。

## カード I/F

|信号名|接続|説明|
|---|---|---|
|LCAUDIO_L|GPIO11|オーディオ (ローパスフィルタ + カップリングコンデンサ経由)|
|LCAUDIO_R|GPIO11|同上|
|LCIO0|GPIO4|LCLCD_RST|
|LCIO1|GPIO5|LCLCD_CS|
|LCIO2|74AHC1G04 出力|LCLCD_WR (BCLK = SCLK/8)|
|LCIO3-10|74HC595 Q1-Q8|LCLCD_D0-7|
|LCIO11|GPIO9|LCLCD_DC|
|LCIO12|QSPI_SS|BOOTSEL (open-drain、負論理)|
|LCIO13|RUN|RESET (open-drain、負論理)|
|LCIO32-39|GPIO16-23|ボタン (カード上の PCA9555 経由。後述)|
|LCUSB_DP|USB D+|UF2 書き込み用|
|LCUSB_DM|USB D-|UF2 書き込み用|

## LCD VSYNC (T.B.D.)

PicoSystem のファームウェアは ST7789 の TE 信号 (GPIO8) の立ち上がりエッジをポーリングして
フレーム同期を行う。8bit パラレル構成では LCIO0-11 が LCD I/F で占有され、ホストシェルの
VSYNC 出力 (機能番号 3) に割り当てられる空き LCIO が無いため、**現状はカード上に小型 MCU を
追加して GPIO8 へ自走パルス (60 Hz 程度) を生成する想定 (詳細 T.B.D.)**。
カードプロファイルでは VSYNC 出力ポートおよび `vsync` メンバを使用しない。

## ボタン

キーパッド I/F は I2C (カード上の PCA9555、devaddr=0x20、LCAUX 経由)。ボタンはアクティブ Low。

**注意: PicoSystem SDK の `init_inputs()` はピン番号をマスクとして OR するバグがあり、
ボタン用 GPIO16-23 の内部プルアップは実際には有効化されない** (実機 PicoSystem は基板上の
外付けプルアップで動作している)。RP2040 のパッドはリセット既定でプルダウンのため、
オープンドレイン駆動ではボタンが全押し状態に見える。
そのため PCA9555 は **プッシュプル出力 + 負論理 (m=34)** とし、非押下時に High を能動的に駆動する。

|PCA9555ポート|RP2040 割り当て|ボタン|
|---|---|---|
|P0_0|GPIO22|左|
|P0_1|GPIO21|右|
|P0_2|GPIO23|上|
|P0_3|GPIO20|下|
|P0_4|GPIO18|A|
|P0_5|GPIO19|B|
|P0_6|GPIO17|X|
|P0_7|GPIO16|Y|

## 不使用ピン

|ピン|処置|
|---|---|
|GPIO2|未接続 (公式 SDK では CHARGE_LED として出力 Low 固定)|
|GPIO12|未接続 (バックライト PWM)|
|GPIO13-15|未接続 (RGB LED)|
|GPIO24|カード上でプルアップ (充電ステータス。非充電相当に固定)|
|GPIO26|カード上の分圧抵抗で満充電相当の電圧を入力 (バッテリーレベル ADC。
分圧比は PicoSystem 実機の回路に合わせて調整する)|

## カード上の部品

|箇所|部品/値|備考|
|---|---|---|
|シフトレジスタ|74HC595|シリアル→パラレル変換|
|分周カウンタ|74HC4040|SCLK/8 生成。CLR ← CS|
|インバータ|74AHC1G04|BCLK 生成 (74HC4040 Q3 の反転)|
|RUN プルアップ|10kΩ||
|QSPI_SS プルアップ|10kΩ|ブートストラップ (BOOTSEL)|
|GPIO24 プルアップ|10kΩ|充電ステータス非アサート固定|
|GPIO26 分圧|適宜|バッテリーレベル入力|
|VSYNC 生成|小型 MCU (T.B.D.)|GPIO8 へ自走パルス|

## MCU について

本カードの MCU が初期ロットの RP2040 (B0、ROM v1) の場合、PicoSystem 公式ビルド
(`PICO_RP2040_B0_SUPPORTED=0`) は double 演算関数の欠落により起動直後に
panic("missing double function") で停止する。アプリは `PICO_BOARD=pico` または
`PICO_RP2040_B0_SUPPORTED=1` でビルドしたものを使用すること。

## EEPROM

[profile.json](profile.json) を参照 (生成済みイメージ: [profile.hex](profile.hex))。

## プログラミング

BOOTSEL と RESET を使って RP2040 を MSC モードにし、USB 経由で UF2 を書き込む
([spec/04_host_controller.md](../../spec/04_host_controller.md))。
UF2 転送完了後も RP2040 側では Flash への書き込みが継続中である可能性に注意すること。
