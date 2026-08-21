# pretest — TJP カード動作テスト用ファームウェア

本体ハードウェアの事前テスト用の仮ファームウェア。TJP カード (ATtiny85 / TinyJoypad 互換) を対象とする。

## 動作

1. 起動時に本体 LCD (ILI9488, 8080 8bit) を初期化し、カラーバーとバナーを表示。
2. LCAUX I2C (GPIO28/29) でカード EEPROM (0x50) を 200 ms 周期でプローブ。
   **3 回連続で ACK** が返ればカード検出 (挿抜時のチャタリング対策)。NAK で連続カウントはリセット。
   未検出時は "No Logic Card" を表示。
   ※ HAUX (GPIO36/37, PCA9555) と LCAUX は同じ I2C0 に属するため、アクセスの度に
   ピン機能を切り替え、使わない側は Hi-Z にする (挿抜の擾乱を本体側バスに波及させない)。
3. 検出後:
   - LCIO5-9 (キー, open-drain active-low) / LCIO13 (RESET) を初期化
   - LcdTap を TinyJoypad preset (SSD1306, I2C slave 0x3C on LCIO2/3) で起動
   - LCIO13 で ATtiny85 をリセット
4. 以降ループ:
   - PCA9555 (0x21, HAUX) の本体キー → LCIO5-9 へ転送 (L/R/U/D/A のみ)
   - I2C1 で受けた SSD1306 コマンド → LcdTap → 更新行のみ本体 LCD へ転送 (128x64 を 3 倍で中央表示)

5. HOME ボタンでシステムメニュー (`Launch` / `Apps`) を開閉。メニュー表示中もカードは動作を続け、
   LCD 転送だけ止まる。`Apps` で TF カード (`/WCB/Cards/TJP/Apps/`) のファイルブラウザを開き、
   `.bin` (生バイナリ) / `.hex` (Intel HEX) を選んで A → 確認 → ATtiny85 へ ISP 書き込み
   (署名確認 → 消去 → 書き込み → ベリファイ、8 KB 上限、ヒューズは読むだけ)。

## TF カード

- SPI0 (GPIO32-35)、LCTF_ENAX (GPIO38) は起動時に High 固定 (ホストが TF を占有)。
- FatFs R0.16 (`firmware/lib/fatfs/`、読み取り専用・LFN・exFAT) + 自前 SD SPI ドライバ (`src/sd_spi.cpp`)。
  SD v1 / SDSC / SDHC / SDXC 対応、MMC 非対応。ブラウザを開くたびにカードを再検出する。

## ISP 書き込み

LCIO2=MOSI / LCIO3=SCK / LCIO9=MISO / LCIO13=RESET をビットバング (~100 kHz)。書き込み中は
キー線を Hi-Z にし、I2C1 スレーブを停止する。終了時に RESET を解放すると新しいアプリが起動する。

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
