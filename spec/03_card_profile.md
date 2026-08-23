# カードプロファイルチップ

LCAUX_SDA/SCL は、ロジックカード上の EEPROM (24LC256, devaddr=0x50) に接続されており、本体側から読み出すことで、ロジックカードの種類を判別することができる。

## EEPROM レイアウト

内容は先頭から次の通り。

1. CBOR オブジェクトの長さ (4 バイト, Big Endian)
2. CBOR オブジェクト
3. 長さ + CBOR オブジェクトに対する 32bit CRC32 チェックサム (Big Endian)

## CBOR オブジェクト

CBOR オブジェクトの内容は次の通り。

```json
{
  "format": "WCBCARD", // 固定値
  "id": "<カードID>", // 最大 16 バイト (ヌル終端除く)
  "name": "<カード名>", // 最大 64 バイト (ヌル終端除く)

  // ロジックカードの汎用 I/O の設定
  "lcio": {
    "useTfCard": <false|true>, // TF カード I/F を使用する場合は true。省略時は false。
    "ports": [
      {
        "i": <LCIO番号>,
        "f": <機能番号>,
        "m": <モード番号>
      },
      ...
    ]
  },

  // ロジックカードの LCD I/F の設定
  "lcdtap": {
    "preset": "<LcdTap のプリセット名>",
    "cfg": { // プリセットから変更する場合
      (LcdTap の設定を CBOR 化したもの)
    }
  },

  // ロジックカード上の MCU へのプログラム書き込みに使用する ISP プロトコル
  "isp": {
    "method": <ISPプロトコル番号>,
    "ports": [ // LCIO を使って書き込みを行う場合
      {
        "i": <LCIO番号>,
        "f": <機能番号>,
        "m": <モード番号>
      },
      ...
    ]
  },

  // 本体側ボタンとロジックカード側ボタンの対応関係
  "keymap": {
    "map": [
      {
        "s": <物理ボタン番号>,
        "d": <割当先ボタン番号>
      },
      ...
    ]
  }
}
```

## LCIO 番号

LCIO 番号は次の通り。

|LCIO番号|信号名|
|---|---|
|0-13|LCIO0-13|
|32-39|PCA9555 の P0_0-P0_7 (I2C 経由)|
|40-47|PCA9555 の P1_0-P1_7 (I2C 経由)|

LCIO32-47 はロジックカード上の PCA9555 (devaddr=0x20, LCAUX 経由) のポートであり、モードは `OUTPUT` または `OPEN_DRAIN` (+ `負論理`) を使用できる。
オープンドレイン出力は PCA9555 の CONFIG レジスタによる入出力切替 (アサート = 出力 Low、解放 = 入力) で実現する。
プルアップ/プルダウンの指定は無視される (入力時は PCA9555 内蔵の弱いプルアップ)。

## ポートの機能番号

ポートの機能番号は次の通り。

|機能番号|機能|
|---|---|
|0|未使用|
|1|LCD I/F|
|2|TF カード I/F|
|16|左ボタン|
|17|右ボタン|
|18|上ボタン|
|19|下ボタン|
|20|Aボタン|
|21|Bボタン|
|22|Xボタン|
|23|Yボタン|
|24|STARTボタン|
|25|SELECTボタン|
|26|Lバンパー|
|27|Rバンパー|
|32|RESET|
|33|BOOTSEL|
|34|ISP (CS)|
|35|ISP (SCK)|
|36|ISP (MOSI)|
|37|ISP (MISO)|

## ポートのモード番号

ポートのモード番号は次の値を足したもの。

|モード番号|モード|
|---|---|
|0|未使用|
|1|入力|
|2|出力|
|4|オープンドレイン出力|
|8|プルアップ|
|16|プルダウン|
|32|負論理|

## ISP プロトコル番号

ISP プロトコル番号は次の通り。

|ISPプロトコル番号|プロトコル|
|---|---|
|0|未使用|
|1|SPI|
|16|USB (Mass Storage Class)|

## キーマップのボタン番号

キーマップのボタン番号は次の通り。

|ボタン番号|ボタン|
|---|---|
|0|左ボタン|
|1|右ボタン|
|2|上ボタン|
|3|下ボタン|
|4|Aボタン|
|5|Bボタン|
|6|Xボタン|
|7|Yボタン|
|8|STARTボタン|
|9|SELECTボタン|
|10|Lバンパー|
|11|Rバンパー|

## 解釈の規則

LCIO のポート設定と LcdTap/ISP の設定が衝突する場合は後者を優先する。

`isp.method` が 16 (USB) の場合、`isp.ports` (または `lcio.ports`) に RESET (32) と BOOTSEL (33) のポートが必要である。
`lcio.useTfCard` が true のロジックカードは、動作中に TF カードを占有する (LCTF_ENAX=Low)。

キーマップのボタン番号 `d` と LCIO の機能番号 `f` は `f = 16 + d` の関係にある。
キーマップに現れない本体側ボタンは、ロジックカードへは送られない。

機能番号 1 (LCD I/F) および 2 (TF カード I/F) を持つポートは、当該 I/F が占有することを示すだけであり、
各信号 (SDA/SCL、MOSI/MISO/SCK/CS など) の LCIO への割り当ては [WildCardBus](02_wildcardbus.md) の固定表に従う (PIO や物理配線の都合で自由には選べない)。
キーパッド I/F が I2C の場合の割り当ても同様に [WildCardBus](02_wildcardbus.md) の固定表に従う。
`lcio.ports` に現れない LCIO は未使用 (High-Z) とする。

`lcdtap.cfg` は、LcdTap の `CONFIG_IDS` (`ctrlFamily`, `busInterface`, `i2cAddr`, `buffWidth`, ... `scaleMode`) をキー、
`setConfigValueById()` に渡す int16 値を値とするマップで、プリセットを適用した後に上書きされる
(LcdTap の JSON I/F `setparams` と同じ語彙)。`dviWidth`/`dviHeight` はホスト側で決定するため対象外。

## 符号化の制約

CBOR の符号化は次の制約に従う (ホスト側パーサの簡略化のため)。

- definite length のみ (indefinite length は不使用)
- 整数は CBOR の非負整数 (major type 0) と負整数 (major type 1) のみ (`lcdtap.cfg` の `intfFmtOvr` は -1 = Off)。真偽値は major type 7 の `true` / `false` (`useTfCard`)。浮動小数点・タグ・バイト列は不使用
- マップのキーは text string
- `id` / `name` の長さ制限は UTF-8 バイト数で数える

CRC32 は zlib 互換の CRC-32 (多項式 0x04C11DB7 反転形 0xEDB88320、初期値 0xFFFFFFFF、最終 XOR 0xFFFFFFFF) とする。
CBOR オブジェクトは高々数 kB を想定しており、ホストは全体を SRAM に読み込んで CRC を検証した後にパースする。

サンプル: [cards/TJP/profile.json](../cards/TJP/profile.json)。編集ツール: [docs/profile-editor](../docs/profile-editor/)。
