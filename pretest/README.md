# pretest — ロジックカード動作テスト用ファームウェア

本体ハードウェアの事前テスト用の仮ファームウェア。TJP カード (ATtiny85 / Tinyjoypad 互換) と
PP1/PP2 カード (Raspberry Pi Pico / Pico 2、PicoPad 互換) を対象とする。

システムクロックは 288 MHz (`src/sys_clock.cpp`、Pico-PIO-USB の要求する 12 MHz の倍数で、LcdTap の SPI 受信が 62.5 MHz に追従できる)。
core0 = UI / 本体 LCD 出力 / カード制御、core1 = LcdTap 入力 (I2C または 4 線 SPI) と dirty 走査、または USB ホスト (`src/core1.cpp`)。

## 動作

1. 起動時に本体 LCD (ILI9488, 8080 8bit) を初期化し、カラーバーとバナーを表示。
2. LCAUX I2C (GPIO28/29) でカード EEPROM (0x50) を 200 ms 周期でプローブ。
   **3 回連続で ACK** が返ったら、EEPROM からカードプロファイル (長さ + CBOR + CRC32、[spec/03_card_profile.md](../spec/03_card_profile.md))
   を読み出し、長さ / CRC / CBOR / 内容 (ポートモード、LcdTap プリセット、LCD バスが I2C であること) を検証して検出成功とする。
   未検出時は "No Logic Card"、プロファイルが無効なときは "Card profile: <理由>" を表示 (この状態でも HOME → `Profile` で書き込み可。
   500 ms 毎に EEPROM を確認し、NAK で未検出に戻る)。
   ※ HAUX (GPIO36/37, PCA9555) と LCAUX は同じ I2C0 に属するため、アクセスの度に
   ピン機能を切り替え、使わない側は Hi-Z にする (挿抜の擾乱を本体側バスに波及させない)。
3. 検出後、**WildCardBus の設定はプロファイルに従う** (`cardPrepare`)。カードは **停止** 状態 (RESET アサート、LCTF_ENAX=High) になり、
   本体起動から 3 秒以内に検出できた場合は自動で **動作** 状態へ、それ以外は「Start card <id>?」を表示して A で起動する:
   - `lcio.ports`: 機能 16-27 (ボタン)、32 (RESET)、33 (BOOTSEL) のポートをモード (`OUTPUT` / `OPEN_DRAIN` / 負論理 / プル) に従って初期化。
     LCIO32-47 はカード側 PCA9555 (0x20, LCAUX) の出力。LCD I/F (機能 1) は LcdTap の受信が使う。未使用 LCIO は Hi-Z
   - `lcdtap.preset` + `cfg` で LcdTap を設定 (I2C スレーブアドレスも実効 cfg から)。**LCD バスは I2C (LCIO2/3) と 4 線 SPI (LCIO0-4) に対応**
   - `keymap.map` で本体ボタン → カードボタン (機能 `16+d`) を対応付け
   - `isp.method` = 1 (SPI, ATtiny85) / 16 (USB MSC, RP2040/RP2350)
   - `lcio.useTfCard` が true なら動作中は LCTF_ENAX=Low (TF カードをカードに渡し、本体の HTF ピンは Hi-Z)
4. 動作中のループ:
   - PCA9555 (0x21, HAUX) の本体キー → キーマップ経由でカードのキー線 (GPIO または カード側 PCA9555) へ
   - core1: I2C1 / PIO SPI で受けた LCD コマンド → LcdTap → dirty 行をライン群として core0 へ通知 → core0 が本体 LCD へ転送

5. HOME ボタンでシステムメニュー (`Start card`/`Stop card` / `Apps` / `Profile`) を開閉。メニュー表示中もカードは動作を続け、
   LCD 転送だけ止まる。動作中かつ `useTfCard` のときは `Apps` / `Profile` は使えない (先に `Stop card`)。
   - `Apps`: TF カード (`/WCB/Cards/<id>/Apps/`) のファイルブラウザを開き、ファイルを選んで A → 確認 → 書き込み (動作中なら先に停止する)。
     完了後に「Start card?」で起動を確認。
     - `.bin` / `.hex`: ATtiny85 へ SPI ISP (署名確認 → 消去 → 書き込み → ベリファイ、8 KB 上限、ヒューズは読むだけ)
     - `.uf2`: BOOTSEL + RESET で MCU を BOOTSEL モードにし、core1 の USB ホスト (Pico-PIO-USB + TinyUSB MSC) で
       UF2 ブロックを直接セクタ書き込み → USB 切断 (= Flash 書き込み完了) を待つ
   - `Profile`: `/WCB/Cards/` からプロファイルの `.hex` ([docs/profile-editor](../docs/profile-editor/) で生成、標準は
     `/WCB/Cards/<id>/profile.hex`) を選ぶと SRAM に展開して長さ / CRC / CBOR を検証し、id / name を表示して
     「Overwrite card profile?」→ A で EEPROM に書き込み → 読み戻し検証 → 稼働中のカードを停止して検出をやり直す。
     CBOR の上限は 4 KB。EEPROM のページサイズ (8-256 B) は書き込み前に自動判定する (ページ折り返しを利用。
     2 バイトアドレスの 24C32 以上が対象で、判定失敗時は "Unsupported EEPROM")。

## TF カード

- SPI0 (GPIO32-35)、LCTF_ENAX (GPIO38) は起動時に High 固定 (ホストが TF を占有)。
- FatFs R0.16 (`firmware/lib/fatfs/`、読み取り専用・LFN・exFAT) + 自前 SD SPI ドライバ (`src/sd_spi.cpp`)。
  SD v1 / SDSC / SDHC / SDXC 対応、MMC 非対応。ブラウザを開くたびにカードを再検出する。

## ISP 書き込み

- SPI (`isp.method` = 1): プロファイルの `isp.ports` にある MOSI/SCK/MISO と RESET ポートをビットバング (~100 kHz)。プロトコルは ATtiny85 固定。
- USB (`isp.method` = 16): RESET + BOOTSEL で BOOTSEL モードへ → MSC マウント (5 s) → UF2 ブロックを LBA 0x100 から連続書き込み →
  切断待ち (10 s) → RESET アサート。デバッグ CDC (native USB) と PIO-USB ホストは TinyUSB の dual-role 構成
  (`include/tusb_config.h`、`tinyusb_host_base` を直接リンクして `pico_stdio_usb` を生かしている)。

## カードプロファイル

CBOR のデコードには [QCBOR](https://github.com/laurencelundblade/QCBOR) (`submodule/QCBOR`) を使用。
pretest で未対応の項目: LCD バスが 3 線 SPI / パラレル (プロファイル無効として扱う)、動作中の抜去検出。
ホスト側テスト: `test_host/build.sh` (`cards/TJP/profile.hex` のパースとエラー経路)。

## ビルド

```
./build.sh          # build/pretest.uf2
```

## デバッグ出力

USB CDC (本体基板の native USB)。起動後 3 秒まで接続を待つ。
1 秒毎に `[stat] ...` 行で LcdTap 受信バイト数、リングバッファの drop/overflow、LCD 送信ライン数などを出力する。

UART stdio は無効。ボードのデフォルト UART ピン (GPIO12/13) が LCIO12/13 (ATtiny85 RESET) と衝突するため。

## 操作 (システムメニュー)

```
HOME            : メニュー開閉
U/D             : カーソル移動
L / B           : 親ディレクトリ (ルートではホームへ)
R               : 子ディレクトリへ
A               : ディレクトリ=移動 / ファイル=書き込み確認
```

## 注意

- 基板上の USER ボタン (GPIO23) は HLCD_D7 と共用。LCD 転送中に押すと表示が乱れる。
- `PICO_DEFAULT_LED_PIN` (GPIO25) は HLCD_WR。LED API は使わないこと。
- 向き/色が違う場合は `src/ili9488.cpp` の `WCB_LCD_MADCTL` (0x28 / 0xE8, BGR bit) と `WCB_LCD_INVERT` を調整。
- 部分更新に問題がある場合は `-DWCB_PUMP_ALWAYS_FULL=1` で常時全画面再送と比較できる。
- 転送速度は `src/main.cpp` の `LCD_PIO_CLKDIV` (1.0 = 40 ns/byte) で調整可能。
