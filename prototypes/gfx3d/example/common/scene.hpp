#pragma once

// gfx3d サンプル共通のデモシーン (WASM / pretest 実機サンプルで共有)。
// 床 + クロームのトーラス + キューブ 3 個 (不透明・半透明・加算合成)。

namespace gfx3d_example {

// カメラの初期値と可動範囲 (docs/prototypes/gfx3d/main.js と同じ値)
constexpr float CAM_YAW_INIT = 0.6f;
constexpr float CAM_PITCH_INIT = 0.35f;
constexpr float CAM_DIST_INIT = 7.0f;
constexpr float CAM_PITCH_MIN = -0.2f, CAM_PITCH_MAX = 1.4f;
constexpr float CAM_DIST_MIN = 3.0f, CAM_DIST_MAX = 20.0f;

// テクスチャとトーラスを生成する (起動時に 1 回)
void sceneInit();

// シーンを構築する (投影設定と beginScene() 〜 endScene())。
// レンダリング (beginRender() / render() / endRender()) は呼び出し側で行う。
// t: 経過秒、yaw/pitch: カメラ角 (ラジアン)、dist: カメラ距離、aspect: 画面の縦横比 (幅 / 高さ)
void sceneBuild(float t, float yaw, float pitch, float dist, float aspect);

} // namespace gfx3d_example
