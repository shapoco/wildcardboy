# 本体インタフェース

## キーパッド

ボタン類は全て PCA9555 (devaddr=0x21) 経由で読み取られる。
Home ボタンは本体側の機能の呼び出しに使用する。

|GPIO|割り当て|説明|
|---|---|---|
|GPIO36|HAUX_SDA|I2C データライン|
|GPIO37|HAUX_SCL|I2C クロックライン|

|PCA9555ポート|割り当て|説明|
|---|---|---|
|P0_0|HKEY_L|左キー|
|P0_1|HKEY_R|右キー|
|P0_2|HKEY_U|上キー|
|P0_3|HKEY_D|下キー|
|P0_4|HKEY_A|Aボタン|
|P0_5|HKEY_B|Bボタン|
|P0_6|HKEY_X|Xボタン|
|P0_7|HKEY_Y|Yボタン|
|P1_0|HKEY_STA|STARTボタン|
|P1_1|HKEY_SEL|SELECTボタン|
|P1_2|HKEY_BL|Lバンパー|
|P1_3|HKEY_BR|Rバンパー|
|P1_4|HKEY_HOME|Homeボタン|

## LCD I/F

ILI9488 互換の 320x480 ドットの液晶を横に倒して使用する。8bit パラレル接続。

|GPIO|割り当て|
|---|---|
|GPIO15|HLCD_RST|
|GPIO16|HLCD_D0|
|GPIO17|HLCD_D1|
|GPIO18|HLCD_D2|
|GPIO19|HLCD_D3|
|GPIO20|HLCD_D4|
|GPIO21|HLCD_D5|
|GPIO22|HLCD_D6|
|GPIO23|HLCD_D7|
|GPIO24|HLCD_DC|
|GPIO25|HLCD_WR|
|GPIO26|HLCD_RD|
|GPIO27|HLCD_CS|

## TF カード I/F

SPI 接続。

|GPIO|割り当て|
|---|---|
|GPIO32|HTF_MISO|
|GPIO33|HTF_CS|
|GPIO34|HTF_SCK|
|GPIO35|HTF_MOSI|

## オーディオ I/F

T.B.D.

## 電源I/F

電源 I/F は、ロジックカードへの電源供給を制御する。

|GPIO|信号名|説明|
|---|---|---|
|T.B.D.|T.B.D.|LC3V3 のイネーブル|
|T.B.D.|T.B.D.|LC5V のイネーブル|
