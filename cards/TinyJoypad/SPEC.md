# TinyJoypad カード

TinyJoypad カードは、ATtiny85 を搭載した小型ゲーム機「Tinyjoypad」の CPU 周辺回路を WildCardBoy のロジックカードとして実装したもの。

## カードID

`TinyJoypad`

## カード I/F

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

方向キーはロジックカード上で次のように配線され、各キーの押下状態は ATtiny85 の ADC で読み取られる (Tinyjoypad の仕様)。

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

## EEPROM

[profile.json](profile.json) を参照 (生成済みイメージ: [profile.hex](profile.hex))。

## プログラミング

LCIO2、3、9、13 を使用して ATtiny85 にプログラムを書き込む。
他の LCIO は High-Z でなければならない。
