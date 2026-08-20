# pretest — TJP カード動作テスト用ファームウェア

本体ハードウェアの事前テスト用の仮ファームウェア。TJP カード (ATtiny85 / TinyJoypad 互換) を対象とする。

## 動作

1. 起動時に本体 LCD (ILI9488, 8080 8bit) を初期化し、カラーバーとバナーを表示。
2. AUX I2C (I2C0, GPIO28/29) でカード EEPROM (0x50) をプローブ。ACK が返ればカード検出。
   未検出時は "No Logic Card" を表示し 500 ms 周期で再試行。
3. 検出後:
   - LCIO5-9 (キー, open-drain active-low) / LCIO13 (RESET) を初期化
   - LcdTap を TinyJoypad preset (SSD1306, I2C slave 0x3C on LCIO2/3) で起動
   - LCIO13 で ATtiny85 をリセット
4. 以降ループ:
   - PCA9555 (0x21) の本体キー → LCIO5-9 へ転送 (L/R/U/D/A のみ)
   - I2C1 で受けた SSD1306 コマンド → LcdTap → 更新行のみ本体 LCD へ転送 (128x64 を 3 倍で中央表示)

## ビルド

```
./build.sh          # build/pretest.uf2
```

## デバッグ出力

USB CDC (本体基板の native USB)。起動後 3 秒まで接続を待つ。
1 秒毎に `[stat] ...` 行で LcdTap 受信バイト数、リングバッファの drop/overflow、LCD 送信ライン数などを出力する。

UART stdio は無効。ボードのデフォルト UART ピン (GPIO12/13) が LCIO12/13 (ATtiny85 RESET) と衝突するため。

## 注意

- 基板上の USER ボタン (GPIO23) は HLCD_D7 と共用。LCD 転送中に押すと表示が乱れる。
- `PICO_DEFAULT_LED_PIN` (GPIO25) は HLCD_WR。LED API は使わないこと。
- 向き/色が違う場合は `src/ili9488.cpp` の `WCB_LCD_MADCTL` (0x28 / 0xE8, BGR bit) と `WCB_LCD_INVERT` を調整。
- 部分更新に問題がある場合は `-DWCB_PUMP_ALWAYS_FULL=1` で常時全画面再送と比較できる。
- 転送速度は `src/main.cpp` の `LCD_PIO_CLKDIV` (1.0 = 40 ns/byte) で調整可能。
