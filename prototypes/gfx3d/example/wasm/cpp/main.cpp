// gfx3d プロトタイプのブラウザ動作確認用サンプル。
// ネイティブビルドでも動作確認できるよう Emscripten 依存は WCB_EXPORT のみに留める。

#include <cmath>
#include <cstdint>

#include "shapoco/gfx3d/gfx3d.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define WCB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WCB_EXPORT
#endif

namespace g3 = shapoco::gfx3d;
using g3::colorf;
using g3::vec2f;
using g3::vec3f;

static constexpr int SCREEN_W = 480;
static constexpr int SCREEN_H = 320;
static constexpr float PI = 3.14159265358979f;

static uint16_t fb[SCREEN_W * SCREEN_H];
static uint8_t arena[128 * 1024];

// ---------------------------------------------------------------------------
// テクスチャ (実機では Flash 配置想定の const データ。ここでは起動時に生成)

static uint16_t checkerPixels[64 * 64];
static uint16_t envPixels[64 * 64];

static const g3::Texture texChecker = {64, 64, checkerPixels};
static const g3::Texture texEnv = {64, 64, envPixels};

static uint16_t rgb565(float r, float g, float b) {
    auto clamp01 = [](float v) { return v < 0 ? 0.0f : (v > 1 ? 1.0f : v); };
    return (uint16_t)(((int)(clamp01(r) * 31 + 0.5f) << 11) |
                      ((int)(clamp01(g) * 63 + 0.5f) << 5) |
                      (int)(clamp01(b) * 31 + 0.5f));
}

// 決定的な 2D ハッシュ (0..1)
static float hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xffff) / 65535.0f;
}

// バリューノイズ (滑らかに補間したハッシュ)
static float valueNoise(float x, float y) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); // smoothstep
    fy = fy * fy * (3 - 2 * fy);
    float n00 = hash2(xi, yi), n10 = hash2(xi + 1, yi);
    float n01 = hash2(xi, yi + 1), n11 = hash2(xi + 1, yi + 1);
    float n0 = n00 + (n10 - n00) * fx;
    float n1 = n01 + (n11 - n01) * fx;
    return n0 + (n1 - n0) * fy;
}

static void generateTextures() {
    // 市松模様
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            bool c = ((x >> 4) ^ (y >> 4)) & 1;
            checkerPixels[y * 64 + x] = c ? rgb565(0.85f, 0.85f, 0.9f) : rgb565(0.35f, 0.4f, 0.5f);
        }
    }
    // 環境マップ:
    // - 上半球 (v < 0.5): 空のグラデーションに雲模様 (ノイズ) を加算合成
    // - 下半球 (v >= 0.5): 床のチェック柄が写り込んでいるように見せる市松模様
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            float u = (x + 0.5f) / 64.0f;
            float v = (y + 0.5f) / 64.0f;
            float r, g, b;
            if (v < 0.5f) {
                // 空: 天頂 (v=0) が明るく、地平線 (v=0.5) に向かって落とす
                float sky = 1.0f - v * 2.0f;
                r = 0.15f + 0.4f * sky;
                g = 0.2f + 0.5f * sky;
                b = 0.35f + 0.55f * sky;
                // 雲: 2 オクターブのバリューノイズを加算合成
                float n = valueNoise(u * 6.0f, v * 6.0f) * 0.65f +
                          valueNoise(u * 13.0f, v * 13.0f) * 0.35f;
                float cloud = (n - 0.45f) * 1.6f;
                if (cloud > 0) {
                    r += cloud;
                    g += cloud;
                    b += cloud * 0.9f;
                }
            } else {
                // 床の写り込み: 床と同じ配色の市松模様、下端に向かって減衰させる
                bool c = ((x >> 3) ^ ((y - 32) >> 3)) & 1;
                float fade = 1.0f - (v - 0.5f) * 1.2f;
                if (c) {
                    r = 0.85f * fade;
                    g = 0.85f * fade;
                    b = 0.9f * fade;
                } else {
                    r = 0.35f * fade;
                    g = 0.4f * fade;
                    b = 0.5f * fade;
                }
            }
            envPixels[y * 64 + x] = rgb565(r, g, b);
        }
    }
}

// ---------------------------------------------------------------------------
// マテリアル

static const g3::Material matFloor = {
    {0.9f, 0.9f, 0.9f, 1.0f}, {0.9f, 0.9f, 0.9f, 1.0f},
    &texChecker, g3::BlendMode::NONE, g3::MaterialFlags::TEXTURE,
};
static const g3::Material matChrome = {
    {0.9f, 0.95f, 1.0f, 1.0f}, {0.9f, 0.95f, 1.0f, 1.0f},
    &texEnv, g3::BlendMode::NONE, g3::MaterialFlags::TEXTURE | g3::MaterialFlags::ENV_MAP,
};
static const g3::Material matRed = {
    {0.9f, 0.15f, 0.1f, 1.0f}, {0.9f, 0.15f, 0.1f, 1.0f},
    nullptr, g3::BlendMode::NONE, 0,
};
static const g3::Material matGlass = {
    {0.4f, 0.7f, 1.0f, 0.45f}, {0.4f, 0.7f, 1.0f, 1.0f},
    nullptr, g3::BlendMode::ALPHA, g3::MaterialFlags::DOUBLE_SIDED,
};
static const g3::Material matGlow = {
    {1.0f, 0.7f, 0.2f, 0.8f}, {1.0f, 0.7f, 0.2f, 1.0f},
    nullptr, g3::BlendMode::ADD, g3::MaterialFlags::DOUBLE_SIDED,
};

// ---------------------------------------------------------------------------
// トーラス (TRIANGLE_STRIP の動作確認を兼ねる)

static constexpr int TORUS_MAJOR = 24; // 大円の分割数
static constexpr int TORUS_MINOR = 12; // 断面の分割数
static constexpr float TORUS_R = 1.0f;  // 大円の半径
static constexpr float TORUS_r = 0.35f; // 断面の半径

static g3::Vertex torusVerts[TORUS_MAJOR * TORUS_MINOR];
static uint16_t torusIndices[TORUS_MAJOR * (TORUS_MINOR * 2 + 2) + (TORUS_MAJOR - 1) * 2];
static g3::VertexBuffer torusVb;
static g3::Primitive torusPrim;

static void generateTorus() {
    for (int i = 0; i < TORUS_MAJOR; i++) {
        float a = 2 * PI * i / TORUS_MAJOR;
        float ca = std::cos(a), sa = std::sin(a);
        for (int j = 0; j < TORUS_MINOR; j++) {
            float b = 2 * PI * j / TORUS_MINOR;
            float cb = std::cos(b), sb = std::sin(b);
            g3::Vertex &v = torusVerts[i * TORUS_MINOR + j];
            v.position = {(TORUS_R + TORUS_r * cb) * ca, TORUS_r * sb, (TORUS_R + TORUS_r * cb) * sa};
            v.normal = {cb * ca, sb, cb * sa};
            v.uv = {(float)i / TORUS_MAJOR, (float)j / TORUS_MINOR};
        }
    }
    // リングごとにストリップを作り、縮退三角形でつなぐ
    int n = 0;
    for (int i = 0; i < TORUS_MAJOR; i++) {
        int i1 = (i + 1) % TORUS_MAJOR;
        for (int j = 0; j <= TORUS_MINOR; j++) {
            int jj = j % TORUS_MINOR;
            torusIndices[n++] = (uint16_t)(i1 * TORUS_MINOR + jj);
            torusIndices[n++] = (uint16_t)(i * TORUS_MINOR + jj);
        }
        if (i < TORUS_MAJOR - 1) {
            // 縮退接続: 直前のインデックスと次のリングの先頭を重複させる
            torusIndices[n] = torusIndices[n - 1];
            n++;
            torusIndices[n] = (uint16_t)((i1 + 1) % TORUS_MAJOR * TORUS_MINOR);
            n++;
        }
    }
    torusVb = {TORUS_MAJOR * TORUS_MINOR, torusVerts};
    torusPrim = {g3::PrimitiveType::TRIANGLE_STRIP, &torusVb, (uint16_t)n, torusIndices, nullptr};
}

// ---------------------------------------------------------------------------
// エクスポート API

extern "C" {

WCB_EXPORT uint16_t *wcb_get_fb() { return fb; }
WCB_EXPORT int wcb_get_width() { return SCREEN_W; }
WCB_EXPORT int wcb_get_height() { return SCREEN_H; }

WCB_EXPORT void wcb_init() {
    generateTextures();
    generateTorus();
    g3::init(SCREEN_W, SCREEN_H, arena, sizeof(arena));
}

// t: 経過秒、yaw/pitch: カメラ角 (ラジアン)、dist: カメラ距離
WCB_EXPORT void wcb_frame(float t, float yaw, float pitch, float dist) {
    g3::setPerspectiveProjection(60.0f * PI / 180.0f, (float)SCREEN_W / SCREEN_H, 0.3f, 100.0f);
    g3::setClearColor({0.04f, 0.05f, 0.11f, 1.0f});

    g3::beginScene();

    // カメラ
    g3::loadIdentity();
    g3::translate(0, 0, -dist);
    g3::rotate(pitch, 1, 0, 0);
    g3::rotate(yaw, 0, 1, 0);
    g3::translate(0, -0.2f, 0);

    // ライト (ワールド空間で指定)
    g3::enableParallelLight({-0.5f, -1.0f, -0.6f}, {1.0f, 0.98f, 0.9f, 1.0f});
    g3::enableEnvironmentLight({0.25f, 0.28f, 0.38f, 1.0f});

    // 床 (面積が大きくアフィン補間の歪みが目立つため分割する)
    g3::setMaterial(matFloor);
    g3::putCube({0, -1.35f, 0}, {7.0f, 0.3f, 7.0f}, 4);

    // クロームのトーラス
    g3::pushState();
    g3::translate(0, 0.5f, 0);
    g3::rotate(t * 0.6f, 0, 1, 0);
    g3::rotate(0.9f + 0.3f * std::sin(t * 0.4f), 1, 0, 0.2f);
    g3::setMaterial(matChrome);
    g3::putPrimitive(torusPrim);
    g3::popState();

    // 赤いキューブ
    g3::pushState();
    g3::translate(-2.2f, -0.55f, 1.0f);
    g3::rotate(t * 0.9f, 0.3f, 1, 0);
    g3::setMaterial(matRed);
    g3::putCube({0, 0, 0}, {1.0f, 1.0f, 1.0f});
    g3::popState();

    // 半透明のキューブ (周回)
    g3::pushState();
    g3::rotate(t * 0.5f, 0, 1, 0);
    g3::translate(2.4f, -0.3f, 0);
    g3::rotate(t * 1.2f, 1, 0.5f, 0);
    g3::setMaterial(matGlass);
    g3::putCube({0, 0, 0}, {1.2f, 1.2f, 1.2f});
    g3::popState();

    // 加算合成の光球 (逆回りに周回)
    g3::pushState();
    g3::rotate(-t * 0.8f, 0, 1, 0);
    g3::translate(1.7f, 0.4f + 0.4f * std::sin(t * 1.7f), 1.7f);
    g3::rotate(t * 2.0f, 1, 1, 0);
    g3::setMaterial(matGlow);
    g3::putCube({0, 0, 0}, {0.5f, 0.5f, 0.5f});
    g3::popState();

    g3::endScene();

    g3::beginRender();
    // 実機での分割転送を想定して 4 バンドに分けてレンダリングする
    constexpr int BAND_H = SCREEN_H / 4;
    for (int i = 0; i < 4; i++) {
        int y = i * BAND_H;
        g3::render(0, (int16_t)y, SCREEN_W, BAND_H, fb + (size_t)y * SCREEN_W, SCREEN_W);
    }
    g3::endRender();
}

} // extern "C"
