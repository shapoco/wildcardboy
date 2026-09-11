// gfx3d プロトタイプのブラウザ動作確認用サンプル。
// ネイティブビルドでも動作確認できるよう Emscripten 依存は WCB_EXPORT のみに留める。
// シーンは example/common/scene.cpp (pretest 実機サンプルと共通)。

#include <cstdint>

#include "shapoco/gfx3d/gfx3d.hpp"

#include "scene.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define WCB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WCB_EXPORT
#endif

namespace g3 = shapoco::gfx3d;

static constexpr int SCREEN_W = 480;
static constexpr int SCREEN_H = 320;

static uint16_t fb[SCREEN_W * SCREEN_H];
static uint8_t arena[128 * 1024];

// ---------------------------------------------------------------------------
// エクスポート API

extern "C" {

WCB_EXPORT uint16_t *wcb_get_fb() { return fb; }
WCB_EXPORT int wcb_get_width() { return SCREEN_W; }
WCB_EXPORT int wcb_get_height() { return SCREEN_H; }

WCB_EXPORT void wcb_init() {
    gfx3d_example::sceneInit();
    g3::init(SCREEN_W, SCREEN_H, arena, sizeof(arena));
}

// t: 経過秒、yaw/pitch: カメラ角 (ラジアン)、dist: カメラ距離
WCB_EXPORT void wcb_frame(float t, float yaw, float pitch, float dist) {
    gfx3d_example::sceneBuild(t, yaw, pitch, dist, (float)SCREEN_W / SCREEN_H);

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
