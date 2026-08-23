# pretest — TJP カード動作テスト用ファームウェア

本体ハードウェアの事前テスト用の仮ファームウェア。TJP カード (ATtiny85 / Tinyjoypad 互換) を対象とする。

## 動作

1. 起動時に本体 LCD (ILI9488, 8080 8bit) を初期化し、カラーバーとバナーを表示。
2. LCAUX I2C (GPIO28/29) でカード EEPROM (0x50) を 200 ms 周期でプローブ。
   **3 回連続で ACK** が返ったら、EEPROM からカードプロファイル (長さ + CBOR + CRC32、[spec/03_card_profile.md](../spec/03_card_profile.md))
   を読み出し、長さ / CRC / CBOR / 内容 (ポートモード、LcdTap プリセット、LCD バスが I2C であること) を検証して検出成功とする。
   未検出時は "No Logic Card"、プロファイルが無効なときは "Card profile: <理由>" を表示 (この状態でも HOME → `Profile` で書き込み可。
   500 ms 毎に EEPROM を確認し、NAK で未検出に戻る)。
   ※ HAUX (GPIO36/37, PCA9555) と LCAUX は同じ I2C0 に属するため、アクセスの度に
   ピン機能を切り替え、使わない側は Hi-Z にする (挿抜の擾乱を本体側バスに波及させない)。
3. 検出後、**WildCardBus の設定はプロファイルに従う**:
   - `lcio.ports`: 機能 16-27 (ボタン) と 32 (RESET) のポートをモード (`OUTPUT` / `OPEN_DRAIN` / 負論理 / プル) に従って初期化。
     LCD I/F (機能 1) は I2C1 スレーブが使う。未使用 LCIO は Hi-Z
   - `lcdtap.preset` + `cfg` で LcdTap を設定 (I2C スレーブアドレスも実効 cfg から)。**LCD バスは I2C のみ対応**
   - `keymap.map` で本体ボタン → カードボタン (機能 `16+d`) を対応付け
   - `isp` から SPI ISP の MOSI/SCK/MISO を取得 (Apps の書き込みに使用)
   - RESET ポートでカード MCU をリセット
4. 以降ループ:
   - PCA9555 (0x21, HAUX) の本体キー → キーマップ経由でカードのキー線へ
   - I2C1 で受けた LCD コマンド → LcdTap → 更新行のみ本体 LCD へ転送

5. HOME ボタンでシステムメニュー (`Launch` / `Apps` / `Profile`) を開閉。メニュー表示中もカードは動作を続け、
   LCD 転送だけ止まる。
   - `Apps`: TF カード (`/WCB/Cards/<id>/Apps/`) のファイルブラウザを開き、`.bin` (生バイナリ) / `.hex` (Intel HEX) を選んで
     A → 確認 → ATtiny85 へ ISP 書き込み (署名確認 → 消去 → 書き込み → ベリファイ、8 KB 上限、ヒューズは読むだけ)。
   - `Profile`: `/WCB/Cards/` からプロファイルの `.hex` ([docs/profile-editor](../docs/profile-editor/) で生成、標準は
     `/WCB/Cards/<id>/profile.hex`) を選ぶと SRAM に展開して長さ / CRC / CBOR を検証し、id / name を表示して
     「Overwrite card profile?」→ A で EEPROM (24LC256) に書き込み → 読み戻し検証 → 稼働中のカードを停止して検出をやり直す。
     CBOR の上限は 4 KB。

## TF カード

- SPI0 (GPIO32-35)、LCTF_ENAX (GPIO38) は起動時に High 固定 (ホストが TF を占有)。
- FatFs R0.16 (`firmware/lib/fatfs/`、読み取り専用・LFN・exFAT) + 自前 SD SPI ドライバ (`src/sd_spi.cpp`)。
  SD v1 / SDSC / SDHC / SDXC 対応、MMC 非対応。ブラウザを開くたびにカードを再検出する。

## ISP 書き込み

プロファイルの `isp.ports` にある MOSI/SCK/MISO と RESET ポートをビットバング (~100 kHz)。書き込み中は
キー線を Hi-Z にし、I2C1 スレーブを停止する。終了時に RESET を解放すると新しいアプリが起動する。
プロトコルは ATtiny85 固定 (`isp.method` = 1 のみ。USB MSC は未対応)。

## カードプロファイル

CBOR のデコードには [QCBOR](https://github.com/laurencelundblade/QCBOR) (`submodule/QCBOR`) を使用。
pretest で未対応の項目: LCD バスが I2C 以外 (プロファイル無効として扱う)、TF カード I/F (機能 2)、BOOTSEL、
I2C キーパッド、ISP over USB (警告ログのみ)。
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
