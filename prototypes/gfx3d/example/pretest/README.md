# gfx3d pretest 実機サンプル

gfx3d プロトタイプの性能を実機で確認するためのファームウェア。
WASM サンプルと同じシーン ([../common/scene.cpp](../common/scene.cpp)) をホストシェルの LCD に描画する。

対象は **pretest の試作機** (ピン配置は [pretest/include/board_pins.hpp](../../../../pretest/include/board_pins.hpp) に従い、
[spec/](../../../../spec/) のピンアサインとは異なる)。ロジックカードは使用しない (LCIO / LCAUX / TF カードは触らない)。

## 構成

- pretest の足回りを無改造で流用: `board_pins.hpp` (ピン定義)、`sys_clock.cpp` (312 MHz)、
  `aux_i2c.cpp` (HAUX の PCA9555 キーパッド)、`lcd8080.pio` (8080 8bit 出力)
- `src/lcd_ili9488.cpp`: pretest の `ili9488.cpp` を元に、帯単位の非同期 DMA 転送 (`writeBytesAsync()`) を追加した LCD ドライバ
- `src/overlay.cpp`: 帯バッファへの文字描画 (LcdTap の 8x16 フォントをヘッダのみで使用)
- `src/main.cpp`: 描画ループ・計測・カメラ操作
- 全体を SRAM で実行する (`copy_to_ram`)。XIP キャッシュミスで計測値がぶれないようにするため

## 描画方式

480x320 を 32 ラインの帯 10 本に分けて `render()` する。帯バッファは 2 面 (各 30 KB) で、
帯 i を描画している間に帯 i-1 を DMA で LCD へ転送する (40 ns/byte、全画面で約 12.3 ms)。
LCD の TE 信号は試作機では MCU に接続されていないため、ティアリングは発生する。

## ビルド

Pico SDK 2.x と arm-none-eabi-gcc が必要。LcdTap サブモジュール (`submodule/lcdtap`) をチェックアウトしておくこと。
USB CDC 出力には SDK 側の TinyUSB サブモジュール (`$PICO_SDK_PATH/lib/tinyusb`) が必要
(無い場合は CMake が警告を出し、計測値は画面表示のみになる)。

```sh
./build.sh                                    # build/gfx3d_pretest.uf2
./build.sh -DGFX3D_PERSPECTIVE_CORRECT_UV=ON  # テクスチャ座標の透視補正を有効にする
./build.sh -DGFX3D_PERSPECTIVE_CORRECT_UV=OFF # 元に戻す (CMake キャッシュに残るため明示が必要)
```

## 操作

| キー | 動作 |
|---|---|
| 十字キー | カメラ回転 (左右 = yaw、上下 = pitch) |
| A / B | ズームイン / ズームアウト |
| X | カメラを初期位置に戻す |
| START | アニメーションの一時停止 / 再開 |
| SELECT | 画面左上の計測値表示の ON / OFF |

## 計測値

1 秒ごとに USB CDC (本体基板の native USB) へ以下を出力し、画面左上にも表示する。値は 1 フレームあたりの平均 (ms)。

```
[gfx3d] fps=... frame=...ms (max ...) key=... scene=... sort=... render=... (band max ...) swap=... lcdwait=... | cam ...
```

| 項目 | 内容 |
|---|---|
| `key` | キーパッド読み取り (I2C) |
| `scene` | `sceneBuild()` (座標変換・ライティング・投影、`beginScene()` 〜 `endScene()`) |
| `sort` | `beginRender()` (三角形の深度ソート) |
| `render` | 全帯の `render()` 合計。`band max` は 1 帯あたりの最大 |
| `swap` | 計測値の文字描画 + バイトスワップ |
| `lcdwait` | 前の帯の DMA 完了待ち + ウィンドウ設定 (描画が転送より速いと増える) |
