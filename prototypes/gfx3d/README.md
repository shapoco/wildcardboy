# gfx3d プロトタイプ

WildCardBoy のシステムメニュー背景用 3D レンダリングライブラリのプロトタイプ。
仕様は [SPEC.md](SPEC.md) を参照。

## ディレクトリ構成

- `lib/include/shapoco/gfx3d/`: 公開ヘッダ (`gfx3d.hpp`, `math3d.hpp`)
- `lib/src/`: ライブラリ実装
- `example/common/`: サンプル共通のデモシーン (`scene.cpp`)
- `example/wasm/`: ブラウザ動作確認用サンプルの WASM ビルド環境
- `example/pretest/`: 実機 (pretest 試作機) での性能確認用ファームウェア。詳細は [example/pretest/README.md](example/pretest/README.md)
- `../../docs/prototypes/gfx3d/`: ビューアページ (index.html, main.js, gfx3d.wasm)

## ビルドと実行

Emscripten (emcc) が必要。

```sh
cd prototypes/gfx3d/example/wasm
make          # docs/prototypes/gfx3d/gfx3d.wasm を生成
make serve    # docs/ をルートに HTTP サーバを起動
```

ブラウザで http://localhost:8000/prototypes/gfx3d/ を開く。
(fetch を使用するため file:// では動作しない)

## コンパイルオプション

- `GFX3D_PERSPECTIVE_CORRECT_UV=1`: テクスチャ座標の透視補正を有効にする
  (デフォルトはアフィン補間。ピクセル単位の除算が追加される)

## メモリ使用量の目安

作業メモリはすべて `init()` に渡したアリーナから確保される。
サンプルは 128KB のアリーナを使用しており、内訳の目安は以下のとおり
(32bit 環境、`lib/src/gfx3d.cpp` の内部構造体のサイズに依存):

- ラインバケット: 画面の高さ x 4 バイト
- 線分プール: 残りの 1/4 (上限 512 個、1 個 64 バイト)
- トライアングルバッファ: 残り全部 (1 三角形約 124 バイト + インデックス 4 バイト。128KB のアリーナで約 770 個)

バッファがあふれた場合、超過分の三角形/線分はそのフレームでは破棄される。

## 制約

- テクスチャの幅・高さは 2 の冪であること (ラップをビットマスクで行うため。そうでない場合は繰り返しがずれる)
- ピクセル処理は固定小数点で、色は切り捨てで RGB565 に量子化される (float 実装との差は概ね 1 LSB 以内)

## 参考性能

480x320 のサンプルシーン (トーラス + キューブ 4 個、半透明・テクスチャ・環境マップ込み) を 1 フレーム:

- WASM (Node.js, シングルスレッド): 約 3 ms (固定小数化前は約 8 ms)
- RP2350 (312 MHz, シングルコア, `example/pretest`): 固定小数化前は render 約 55 ms (15〜18 fps)
