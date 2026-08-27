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
    "useVirtIoExp": <false|true>, // 仮想 I/O エキスパンダを使用する場合は true。省略時は false。
    "ports": [
      {
        "i": <LCIO番号>,
        "f": <機能番号>,
        "m": <モード番号>
      },
      ...
    ]
  },

  // 仮想 I/O エキスパンダの設定 (lcio.useVirtIoExp が true の場合)
  "virtIoExp": {
    "chip": "<チップID>", // "mcp23017" または "pca9555"
    "addr": <I2Cスレーブアドレス> // 例: MCP23017 は通常 32 (0x20)、PCA9555 (Xiamocon) は 34 (0x22)
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
    "mcu": "<MCU ID>", // 最大 16 バイト (ヌル終端除く)
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
|64-71|仮想 I/O エキスパンダのポート A (MCP23017: GPA0-7、PCA9555: P0_0-P0_7)|
|72-79|仮想 I/O エキスパンダのポート B (MCP23017: GPB0-7、PCA9555: P1_0-P1_7)|

LCIO32-47 はロジックカード上の PCA9555 (devaddr=0x20, LCAUX 経由) のポートであり、モードは `OUTPUT` または `OPEN_DRAIN` (+ `負論理`) を使用できる。
オープンドレイン出力は PCA9555 の CONFIG レジスタによる入出力切替 (アサート = 出力 Low、解放 = 入力) で実現する。
プルアップ/プルダウンの指定は無視される (入力時は PCA9555 内蔵の弱いプルアップ)。

LCIO64-79 は `lcio.useVirtIoExp` が true の場合に有効な仮想ポートであり、主にキーパッド状態の受け渡しに使用する
([02_wildcardbus.md](02_wildcardbus.md)、[04_host_controller.md](04_host_controller.md))。
キー (機能番号 16-27) のモードは `OPEN_DRAIN` + `負論理` を使用する (実チップのプルアップ入力 + アクティブ Low に相当)。
プルアップ/プルダウンの指定は無視される。

仮想ポートに機能番号 1 (LCD I/F) を割り当てた場合、そのポートはディスプレイリセット (LCLCD_RST 相当) を意味する。
モードは `入力` + `負論理` (m=33) とし、カード側 MCU がそのビットをエキスパンダの出力に設定して Low を駆動している間、
ホストは LCD をリセット状態として扱う ([04_host_controller.md](04_host_controller.md))。
ホスト実装は当面このポートを無視してよい (段階的対応。その場合の LCD リセットは、カードが送る
初期化コマンド列への追従に依存する)。

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
|38|ISP (UART TX)|
|39|ISP (UART RX)|
|48|I2C スレーブ|

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
|2|UART (Espressif シリアルブートローダ)|
|16|USB (Mass Storage Class)|

## MCU ID

|ID|MCU|
|---|---|
|attiny85|ATtiny85|
|atmega32u4|ATmega32U4|
|esp8266|ESP8266|
|rp2040|Raspberry Pi Pico|
|rp2350|Raspberry Pi Pico 2|

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
`isp.method` が 2 (UART) の場合、`isp.ports` に ISP (UART TX) (38) と ISP (UART RX) (39) のポートが、
`isp.ports` (または `lcio.ports`) に RESET (32) と BOOTSEL (33) のポートが必要である。
UART には任意の LCIO を割り当てられるが、LCIO10 (TX) / LCIO11 (RX) を推奨する (ホスト側でハードウェア UART1 を使用できる)。
`lcio.useVirtIoExp` が true の場合、`virtIoExp` メンバが必要であり、LCIO6/7 に機能番号 48 (I2C スレーブ) のポートを記述する。
仮想 I/O エキスパンダは LCD I/F が SPI のカードでのみ使用できる ([02_wildcardbus.md](02_wildcardbus.md))。
`lcio.useTfCard` が true のロジックカードは、動作中に TF カードを占有する (LCTF_ENAX=Low)。

キーマップのボタン番号 `d` と LCIO の機能番号 `f` は `f = 16 + d` の関係にある。
キーマップに現れない本体側ボタンは、ロジックカードへは送られない。
複数の物理ボタンを同一の割当先 `d` に割り当ててもよい (いずれかが押されていればアサートされる。
例: Xiamocon のファンクションスイッチに L/R バンパーの両方を割り当てる)。

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

サンプル: [cards/TinyJoyPad/profile.json](../cards/TinyJoyPad/profile.json)。編集ツール: [docs/profile-editor](../docs/profile-editor/)。
