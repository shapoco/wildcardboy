# PicoPad1/PicoPad2 カード

PicoPad1/PicoPad2 カードは、Raspberry Pi Pico / Pico 2 を搭載した小型ゲーム機「PicoPad」および「PicoPad2」の CPU 周辺回路を WildCardBoy のロジックカードとして実装したもの。

## カードID

`PicoPad1` または `PicoPad2`

## カード I/F

|信号名|Pico 割り当て|説明|
|---|---|---|
|LCAUDIO_L|GPIO15|オーディオ左チャンネル|
|LCAUDIO_R|GPIO15|オーディオ右チャンネル|
|LCIO0|GPIO20|ST7789 制御信号 (RST)|
|LCIO1|GPIO21|ST7789 制御信号 (CS)|
|LCIO2|GPIO22|ST7789 制御信号 (SCLK)|
|LCIO3|GPIO23|ST7789 制御信号 (MOSI)|
|LCIO4|GPIO24|ST7789 制御信号 (DC)|
|LCIO8|GPIO11|TF カード制御信号 (MOSI)|
|LCIO9|GPIO13|TF カード制御信号 (CS)|
|LCIO10|GPI10|TF カード制御信号 (SCK)|
|LCIO11|GPI12|TF カード制御信号 (MISO)|
|LCIO12|GPIO12|BOOTSEL|
|LCIO13|GPIO13|RESET|
|LCIO32|GPIO3|左ボタン|
|LCIO33|GPIO2|右ボタン|
|LCIO34|GPIO4|上ボタン|
|LCIO35|GPIO5|下ボタン|
|LCIO36|GPIO7|Aボタン|
|LCIO37|GPIO6|Bボタン|
|LCIO38|GPIO9|Xボタン|
|LCIO39|GPIO8|Yボタン|
|LCUSB_DP|USB D+|USB D+|
|LCUSB_DM|USB D-|USB D-|

## EEPROM

T.B.D.

## プログラミング

BOOTSEL と RESET を使って Pico を MSC モードにし、USB 経由で Pico にプログラムを書き込む。
UF2 転送完了後も Pico 側では Flash への書き込みが継続中である可能性に注意すること。
