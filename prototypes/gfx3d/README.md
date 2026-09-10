# gfx3d プロトタイプ

WildCardBoy のシステムメニュー背景用 3D レンダリングライブラリのプロトタイプ。
仕様は [SPEC.md](SPEC.md) を参照。

## ディレクトリ構成

- `lib/include/shapoco/gfx3d/`: 公開ヘッダ (`gfx3d.hpp`, `math3d.hpp`)
- `lib/src/`: ライブラリ実装
- `example/wasm/`: ブラウザ動作確認用サンプルの WASM ビルド環境
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

## メモリ使用量の目安

作業メモリはすべて `init()` に渡したアリーナから確保される。
サンプルは 128KB のアリーナを使用しており、内訳の目安は以下のとおり
(32bit 環境、`lib/src/gfx3d.cpp` の内部構造体のサイズに依存):

- 線分プール: アリーナの 1/4 (上限 512 個、1 個約 70 バイト)
- トライアングルバッファ: 残り全部 (1 三角形約 120 バイト)

バッファがあふれた場合、超過分の三角形/線分はそのフレームでは破棄される。

## 参考性能

WASM (Node.js, シングルスレッド) で 480x320 のサンプルシーン
(トーラス + キューブ 4 個、半透明・テクスチャ・環境マップ込み) を 1 フレーム約 1.6ms。
