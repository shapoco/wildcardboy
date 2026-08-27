# ロジックカード I/F (WildCardBus)

2x12 ピンのピンソケット。

|ヘッダピン番号|GPIO|信号名|説明|
|---|---|---|---|
|1|-|LCAUDIO_L|オーディオ左チャンネル|
|2|-|LCAUDIO_R|オーディオ右チャンネル|
|3|-|GND|グランド|
|4|-|NC|未接続|
|5|GPIO0|LCIO0|ロジックカード側の GPIO0|
|6|GPIO1|LCIO1|ロジックカード側の GPIO1|
|7|GPIO2|LCIO2|ロジックカード側の GPIO2|
|8|GPIO3|LCIO3|ロジックカード側の GPIO3|
|9|GPIO4|LCIO4|ロジックカード側の GPIO4|
|10|GPIO5|LCIO5|ロジックカード側の GPIO5|
|11|GPIO6|LCIO6|ロジックカード側の GPIO6|
|12|GPIO7|LCIO7|ロジックカード側の GPIO7|
|13|GPIO8|LCIO8|ロジックカード側の GPIO8|
|14|GPIO9|LCIO9|ロジックカード側の GPIO9|
|15|GPIO10|LCIO10|ロジックカード側の GPIO10|
|16|GPIO11|LCIO11|ロジックカード側の GPIO11|
|17|GPIO12|LCIO12|ロジックカード側の GPIO12|
|18|GPIO13|LCIO13|ロジックカード側の GPIO13|
|19|GPIO28|LCAUX_SDA|I2C データライン|
|20|GPIO29|LCAUX_SCL|I2C クロックライン|
|21|GPIO30|LCUSB_DP|USB D+|
|22|GPIO31|LCUSB_DM|USB D-|
|23|-|LC3V3|3.3V 電源|
|24|-|LC5V|5V 電源|

## LCD I/F が I2C の場合

|信号名|割り当て|
|---|---|
|LCIO2|LCLCD_SDA|
|LCIO3|LCLCD_SCL|

## LCD I/F が SPI の場合

|信号名|割り当て|
|---|---|
|LCIO0|LCLCD_RST|
|LCIO1|LCLCD_CS|
|LCIO2|LCLCD_SCK|
|LCIO3|LCLCD_MOSI|
|LCIO4|LCLCD_DC|

## LCD I/F が 8bit パラレルの場合

|信号名|割り当て|
|---|---|
|LCIO0|LCLCD_RST|
|LCIO1|LCLCD_CS|
|LCIO2|LCLCD_WR|
|LCIO3|LCLCD_D0|
|LCIO4|LCLCD_D1|
|LCIO5|LCLCD_D2|
|LCIO6|LCLCD_D3|
|LCIO7|LCLCD_D4|
|LCIO8|LCLCD_D5|
|LCIO9|LCLCD_D6|
|LCIO10|LCLCD_D7|
|LCIO11|LCLCD_DC|

## キーパッド I/F がパラレルの場合

ロジックカード側の LCD I/F がパラレル以外の場合で、TF カード I/F を持たず、使用するキーが方向キーと A、B、START、SELECT のみの場合は、キーパッド I/F はパラレル接続となる。

|信号名|割り当て|説明|
|---|---|---|
|LCIO5|LCKEY_L|左キー|
|LCIO6|LCKEY_R|右キー|
|LCIO7|LCKEY_U|上キー|
|LCIO8|LCKEY_D|下キー|
|LCIO9|LCKEY_A|Aボタン|
|LCIO10|LCKEY_B|Bボタン|
|LCIO11|LCKEY_STA|STARTボタン|
|LCIO12|LCKEY_SEL|SELECTボタン|

## キーパッド I/F が I2C の場合

キーパッド I/F がパラレルで使用できない場合は I2C 接続となる。I2C は LCAUX_SDA/LCAUX_SCL を通じてロジックカード上の PCA9555 (devaddr=0x20) に接続される。

|PCA9555ポート|割り当て|説明|
|---|---|---|
|P0_0|LCKEY_L|左キー|
|P0_1|LCKEY_R|右キー|
|P0_2|LCKEY_U|上キー|
|P0_3|LCKEY_D|下キー|
|P0_4|LCKEY_A|Aボタン|
|P0_5|LCKEY_B|Bボタン|
|P0_6|LCKEY_X|Xボタン|
|P0_7|LCKEY_Y|Yボタン|
|P1_0|LCKEY_STA|STARTボタン|
|P1_1|LCKEY_SEL|SELECTボタン|
|P1_2|LCKEY_BL|Lバンパー|
|P1_3|LCKEY_BR|Rバンパー|

## キーパッド I/F が仮想 I/O エキスパンダの場合

カードプロファイルの `lcio.useVirtIoExp` が true の場合、ホストコントローラ自身が I2C スレーブとなり、
ロジックカード上の I/O エキスパンダ IC の動作を模擬する (詳細は [03_card_profile.md](03_card_profile.md) および [04_host_controller.md](04_host_controller.md))。
ロジックカード上の MCU は、実チップに対するのと同様に I2C マスタとしてホストへアクセスする。
キーパッド状態のほか、カードによってはディスプレイリセット等の信号もこの経路で受け渡す ([03_card_profile.md](03_card_profile.md))。

|信号名|割り当て|
|---|---|
|LCIO6|LCVIO_SDA|
|LCIO7|LCVIO_SCL|

- ホスト側では GPIO6/7 を I2C1 (スレーブモード) として使用する。
- バスのプルアップ抵抗はロジックカード側に実装する (4.7kΩ 推奨)。
- I2C1 ペリフェラルは LCD I/F が I2C の場合の受信にも使用するため、仮想 I/O エキスパンダと I2C LCD は併用できない。
  また LCIO6/7 は 8bit パラレル LCD (D3/D4) およびパラレルキーパッド (LCKEY_R/U) と衝突する。
  実質的に、仮想 I/O エキスパンダは LCD I/F が SPI のカード専用である。

## TF カード I/F

ロジックカード側が TF カード I/F を要求する場合は、本体側の TF カードスロットがロジックカードへ接続される。

|信号名|割り当て|
|---|---|
|LCIO8|LCTF_MOSI|
|LCIO9|LCTF_CS|
|LCIO10|LCTF_SCK|
|LCIO11|LCTF_MISO|

ロジックカードが TF カードを占有する間、ホストコントローラは TF カードへはアクセスできなくなる。
TF カードスロットをホストコントローラに接続するかロジックカードに接続するかは、ホストコントローラの GPIO で切り替える。

|GPIO|信号名|説明|
|---|---|---|
|GPIO38|LCTF_ENAX|Low=TFカードはロジックカードに接続, High=TFカードはホストコントローラに接続|

LCTF_ENAX は基板上でプルアップされている。
LCTF_ENAX が Low の間は、ホストコントローラ側の TF カード I/F は High-Z でなければならない。

## USB I/F

USB I/F は、将来的にホストコントローラが USB ホストとなってロジックカード上の USB デバイスと通信するために使用される。
ロジックカード上の MCU が RP2040 や RP2350 で、ホストから UF2 を書き込むようなシーンが想定される。
