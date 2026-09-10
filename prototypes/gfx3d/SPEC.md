# 3D レンダリングライブラリのプロトタイプ

## 概要

WildCardBoy のシステムメニューの背景で用いる 3D レンダリングライブラリのプロトタイプ。

- 省メモリ: フレームバッファや Z バッファは持たず、ライン単位でレンダリングする
- 実機ではモデルデータ (頂点配列やテクスチャ) は Flash に配置する想定
- 作業メモリはユーザが与えるアリーナから確保する (ライブラリ内部で malloc しない)
- スキャンライン法によるラスタライズ
- 平行光源、拡散反射、環境光、テクスチャ、環境マッピング対応
- シェーディングはグーローシェーディング (頂点単位でライティングし、スパン内で色を線形補間)
- 名前空間: `shapoco::gfx3d`
- 特定のアーキテクチャに依存しない (ピュア C++)
- 将来的に組み込み向けの汎用的な 3D ライブラリとしてスピンアウトする可能性あり。

## 数学系

`math3d.hpp` にまとめて実装する

### 構造体

- `vec2f`: 2D ベクトル (x, y) を表す構造体
- `vec3f`: 3D ベクトル (x, y, z) を表す構造体
- `colorf`: 色 (r, g, b, a) を表す構造体
- `mat4f`: 4x4 行列を表す構造体

### 関数

各種ベクトル演算、行列演算、変換行列生成関数を提供する。

## データ構造

### 座標系

- OpenGL 互換の右手系座標系

### 頂点 (`Vertex`)

- 位置ベクトル (`vec3f`)
- 法線ベクトル (`vec3f`)
- テクスチャ座標 (`vec2f`, 環境マッピング時は未使用)

### 頂点バッファ (`VertexBuffer`)

- 頂点数 (`uint16_t`)
- 頂点の配列へのポインタ (`const Vertex*`)

### テクスチャ (`Texture`)

- 幅 (`int16_t`)
- 高さ (`int16_t`)
- ピクセルデータ (RGB565) へのポインタ (`const uint16_t*`)

### ブレンドモード (`BlendMode`)

```c++
enum class BlendMode : uint8_t {
    NONE,  // ブレンドなし (上書き)
    ALPHA, // アルファブレンディング
    ADD,   // 加算合成
};
```

`NONE` 以外のマテリアルを持つ三角形は半透明として扱われる
(線分リストで既存スパンを削除せず、リスト順にブレンドされる)。

### マテリアル (`Material`)

- 拡散反射色 (`colorf`)
- 環境反射色 (`colorf`)
- テクスチャへのポインタ (`const Texture*`)
- ブレンドモード (`BlendMode`)
- フラグ (`uint32_t`)
    - テクスチャを有効にするかどうか
    - 環境マッピングを有効にするかどうか (有効時はテクスチャを環境マップとして使用する)
    - 両面描画するかどうか (無効時は反時計回りを表としてバックフェイスカリングを行う)

### プリミティブの種類 (`PrimitiveType`)

- `TRIANGLES`: 3 つの頂点で構成される三角形の集合
- `TRIANGLE_STRIP`: 連続する頂点列で構成される三角形ストリップ
- `TRIANGLE_FAN`: 連続する頂点列で構成される三角形ファン

### プリミティブ (`Primitive`)

- プリミティブの種類 (`PrimitiveType`)
- 頂点バッファへのポインタ (`const VertexBuffer*`)
- 頂点インデックスの数 (`uint16_t`, 面の数はこれから算出)
- 頂点インデックスの配列へのポインタ (`const uint16_t*`)
- マテリアルへのポインタ (`const Material*`)

## API

```c++
namespace shapoco::gfx3d {

// ライブラリを初期化する (w, h: 画面サイズ、arena, arenaSize: 作業メモリ)
void init(int16_t w, int16_t h, void* arena, size_t arenaSize);
void deinit(); // ライブラリを終了する

void beginScene(); // シーンの構築を開始する
void endScene(); // シーンの構築を終了する

void loadIdentity(); // 現在の変換行列を単位行列にリセットする
void translate(const vec3f& v); // 現在の変換行列に平行移動を適用する
void translate(float x, float y, float z); // 同上
void rotate(float angle, const vec3f& axis); // 現在の変換行列に回転を適用する
void rotate(float angle, float x, float y, float z); // 同上
void scale(const vec3f& v); // 現在の変換行列にスケーリングを適用する
void scale(float x, float y, float z); // 同上

void pushState(); // 現在の変換行列とマテリアルをスタックに保存する
void popState(); // スタックから変換行列とマテリアルを復元する

void setMaterial(const Material& mat); // 現在のマテリアルを設定する
void putPrimitive(const Primitive& prim); // プリミティブをシーンに追加する
void putCube(const vec3f& center, const vec3f& size); // 中心とサイズを指定して立方体をシーンに追加する

void enableParallelLight(const vec3f& dir, const colorf& col); // 平行光源を設定する
void disableParallelLight(); // 平行光源を無効にする

void enableEnvironmentLight(const colorf& col); // 環境光を設定する
void disableEnvironmentLight(); // 環境光を無効にする

void setClearColor(const colorf& col); // 背景色を設定する (スパンで覆われないピクセルはこの色で塗られる)

void setPerspectiveProjection(float fovY, float aspect, float zNear, float zFar); // 透視投影行列を設定する
void setOrthographicProjection(float left, float right, float bottom, float top, float zNear, float zFar); // 正射影行列を設定する

void beginRender(); // レンダリングを開始する
void endRender(); // レンダリングを終了する
void render(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t* dst, uint32_t stride); // 指定された領域をレンダリングする

};
```

## シーンの構築

1. `beginScene()` を呼び出す
2. ノードやプリミティブを追加してシーンを構築する
3. `endScene()` を呼び出す

`putPrimitive()` はプリミティブを個々のポリゴンに分解し、頂点単位のライティング (グーローシェーディング) と投影変換までを実施する。
変換結果は内部のトライアングルバッファに保持され、`render()` でラスタライズされる。
`render()` は複数の領域に分けて呼び出される可能性がある。

この段階で以下の三角形は破棄される:

- ニア平面 (zNear) をまたぐ、またはニア平面より手前にある三角形
- バックフェイスカリングが有効なマテリアルで、裏を向いている三角形

## メモリ確保

- ライブラリ内部で動的メモリ確保 (malloc/new) は行わず、`init()` でユーザが与えたアリーナからすべての作業メモリ (トライアングルバッファ、線分リスト等) を確保する
- トライアングルバッファがあふれた場合、以降の三角形はそのフレームでは破棄される

## レンダリングパイプライン

### `beginRender()`

トライアングルバッファ内の三角形を遠い順にソートする

### `render()`

指定された領域の各ラインについて以下の処理を行う:

1. 線分リストをクリアする
2. そのラインと交差する三角形 (遠い順) に対して以下の処理を行う
    1. 当該三角形のライン上の2つの交点(線分)を求める
    2. 当該三角形が半透明 (`BlendMode` が `NONE` 以外) な場合はそのまま線分リストに追加する。不透明な場合は既存の線分リストのうち、当該三角形の線分と重なる部分を削除してから線分リストに追加する
3. ラインを背景色でクリアした後、線分リストに残った線分をリスト順にラスタライズしてピクセルに変換する。半透明の線分はマテリアルの `BlendMode` に従って合成する

テクスチャ座標は透視補正する (スクリーン空間で線形な (u/w, v/w, 1/w) を補間し、ピクセル単位で除算する)。
頂点色 (グーローシェーディング) はアフィン補間とする。

### `endRender()`

特にすることはない。

## ライブラリ実装

- `prototypes/gfx3d/lib/include/shapoco/gfx3d/`: ライブラリユーザへ公開するヘッダファイル
- `prototypes/gfx3d/lib/src/`: ライブラリの実装ファイル

ライブラリユーザは `#include "shapoco/gfx3d/gfx3d.hpp"` を使用してライブラリを利用する。

## サンプル実装

ブラウザ上で動作確認するためのサンプルコード。解像度は 480x320 で固定。

- `prototypes/gfx3d/example/wasm/cpp/main.cpp`: フレームバッファを確保し、gfx3d ライブラリを使用して適当なシンプルなシーンを構築し、レンダリングする
- `docs/prototypes/gfx3d/index.html`: canvas 上でレンダリング結果を確認するための HTML ファイル
- `prototypes/gfx3d/example/wasm/` の配下に WASM ビルド環境を構築する
    - 必要に応じて JavaScript または TypeScript 用のサブディレクトリを作成する
- マウスまたはキーボードでカメラ操作

## 参考資料

以前別のプロジェクトで作成した 3D ライブラリが以下にあるので参考にしてください。
ただしこちらは Z バッファ法によるラスタライズになっているので注意してください。
https://github.com/shapoco/xiamocon/tree/main/cpp/library
