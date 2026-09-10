#pragma once

#include <cstddef>
#include <cstdint>

#include "shapoco/gfx3d/math3d.hpp"

namespace shapoco::gfx3d {

// ---------------------------------------------------------------------------
// データ構造

struct Vertex {
    vec3f position;
    vec3f normal;
    vec2f uv; // 環境マッピング時は未使用
};

struct VertexBuffer {
    uint16_t vertexCount;
    const Vertex *vertices;
};

struct Texture {
    int16_t width;
    int16_t height;
    const uint16_t *pixels; // RGB565
};

enum class BlendMode : uint8_t {
    NONE,  // ブレンドなし (上書き)
    ALPHA, // アルファブレンディング
    ADD,   // 加算合成
};

namespace MaterialFlags {
constexpr uint32_t TEXTURE = 1u << 0;      // テクスチャを有効にする
constexpr uint32_t ENV_MAP = 1u << 1;      // テクスチャを環境マップとして使用する
constexpr uint32_t DOUBLE_SIDED = 1u << 2; // 両面描画 (バックフェイスカリング無効)
} // namespace MaterialFlags

struct Material {
    colorf diffuse;          // 拡散反射色 (a は不透明度として使用)
    colorf ambient;          // 環境反射色
    const Texture *texture;  // テクスチャ (未使用時は nullptr 可)
    BlendMode blendMode;
    uint32_t flags;          // MaterialFlags の組み合わせ
};

enum class PrimitiveType : uint8_t {
    TRIANGLES,
    TRIANGLE_STRIP,
    TRIANGLE_FAN,
};

struct Primitive {
    PrimitiveType type;
    const VertexBuffer *vertexBuffer;
    uint16_t indexCount;
    const uint16_t *indices;
    const Material *material; // nullptr の場合は setMaterial() で設定されたマテリアルを使用する
};

// ---------------------------------------------------------------------------
// API
// 角度は全てラジアン。

// ライブラリを初期化する (w, h: 画面サイズ、arena, arenaSize: 作業メモリ)
void init(int16_t w, int16_t h, void *arena, size_t arenaSize);
void deinit(); // ライブラリを終了する

void beginScene(); // シーンの構築を開始する
void endScene();   // シーンの構築を終了する

void loadIdentity();                            // 現在の変換行列を単位行列にリセットする
void translate(const vec3f &v);                 // 現在の変換行列に平行移動を適用する
void translate(float x, float y, float z);      // 同上
void rotate(float angle, const vec3f &axis);    // 現在の変換行列に回転を適用する
void rotate(float angle, float x, float y, float z); // 同上
void scale(const vec3f &v);                     // 現在の変換行列にスケーリングを適用する
void scale(float x, float y, float z);          // 同上

void pushState(); // 現在の変換行列とマテリアルをスタックに保存する
void popState();  // スタックから変換行列とマテリアルを復元する

void setMaterial(const Material &mat);   // 現在のマテリアルを設定する
void putPrimitive(const Primitive &prim); // プリミティブをシーンに追加する
void putCube(const vec3f &center, const vec3f &size); // 中心とサイズを指定して直方体をシーンに追加する

// 平行光源を設定する (dir は呼び出し時点の変換行列で変換される)
void enableParallelLight(const vec3f &dir, const colorf &col);
void disableParallelLight();

void enableEnvironmentLight(const colorf &col); // 環境光を設定する
void disableEnvironmentLight();

void setClearColor(const colorf &col); // 背景色を設定する

void setPerspectiveProjection(float fovY, float aspect, float zNear, float zFar);
void setOrthographicProjection(float left, float right, float bottom, float top, float zNear, float zFar);

void beginRender(); // レンダリングを開始する
void endRender();   // レンダリングを終了する

// 指定された領域をレンダリングする。dst は領域の左上を指し、stride は dst の行ピッチ (ピクセル単位)
void render(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t *dst, uint32_t stride);

} // namespace shapoco::gfx3d
