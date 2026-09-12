// gfx3d prototype performance check on the pretest host shell (prototype
// board). Renders the same scene as the WASM example (example/common/
// scene.cpp) on the host LCD, with the camera driven by the host keypad.
//
//   Rendering: gfx3d renders one band (BAND_H lines) at a time into one of
//              two band buffers; the band is byte-swapped and DMA'd to the
//              LCD while the next band is rendered into the other buffer.
//   Timing   : per-stage averages are printed over USB CDC once a second
//              and shown in the top-left corner of the screen.
//
// Board support (pin map, 312 MHz clock, keypad PCA9555 on HAUX, LCD PIO
// program) comes unmodified from ../../../../pretest. The logic card
// interface is not used.

#include <cmath>
#include <cstdio>

#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include "shapoco/gfx3d/gfx3d.hpp"

#include "aux_i2c.hpp"
#include "board_pins.hpp"
#include "lcd_ili9488.hpp"
#include "overlay.hpp"
#include "scene.hpp"
#include "sys_clock.hpp"

using namespace wcb;
namespace g3 = shapoco::gfx3d;
namespace scene = gfx3d_example;

//-----------------------------------------------------------------------------
// Tunables
//-----------------------------------------------------------------------------
static constexpr int SCREEN_W = LCD_WIDTH;
static constexpr int SCREEN_H = LCD_HEIGHT;
static constexpr int BAND_H = 32;
static constexpr int NUM_BANDS = SCREEN_H / BAND_H;
static_assert(SCREEN_H % BAND_H == 0, "BAND_H must divide the screen height");
static constexpr int OVERLAY_LINES = 3;  // top-left stats text (may straddle bands)

static constexpr size_t ARENA_SIZE = 128 * 1024;  // same as the WASM example
static constexpr float LCD_PIO_BASE_HZ = 150e6f;  // 40 ns/byte at clkdiv 1.0
static constexpr uint64_t STATS_INTERVAL_US = 1000000;

// Camera speeds while a key is held.
static constexpr float CAM_ROT_SPEED = 1.5f;   // rad/s
static constexpr float CAM_ZOOM_SPEED = 1.0f;  // dist *= exp(+-speed * dt)
static constexpr float CAM_DT_MAX = 0.1f;      // clamp for slow frames

#if defined(GFX3D_PERSPECTIVE_CORRECT_UV) && GFX3D_PERSPECTIVE_CORRECT_UV
static constexpr int PERSPECTIVE_UV = 1;
#else
static constexpr int PERSPECTIVE_UV = 0;
#endif

//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------
static Ili9488 gLcd;
alignas(4) static uint16_t gBand[2][SCREEN_W * BAND_H];
alignas(8) static uint8_t gArena[ARENA_SIZE];

struct Camera {
  float yaw, pitch, dist;
};

static void cameraReset(Camera& c) {
  c.yaw = scene::CAM_YAW_INIT;
  c.pitch = scene::CAM_PITCH_INIT;
  c.dist = scene::CAM_DIST_INIT;
}

// Same directions as the arrow / zoom keys of docs/prototypes/gfx3d/main.js.
static void cameraUpdate(Camera& c, uint16_t keys, float dt) {
  if (dt > CAM_DT_MAX) dt = CAM_DT_MAX;
  const float rot = CAM_ROT_SPEED * dt;
  if (keys & HKEY_L) c.yaw -= rot;
  if (keys & HKEY_R) c.yaw += rot;
  if (keys & HKEY_U) c.pitch += rot;
  if (keys & HKEY_D) c.pitch -= rot;
  if (keys & HKEY_A) c.dist *= std::exp(-CAM_ZOOM_SPEED * dt);
  if (keys & HKEY_B) c.dist *= std::exp(CAM_ZOOM_SPEED * dt);
  if (c.pitch < scene::CAM_PITCH_MIN) c.pitch = scene::CAM_PITCH_MIN;
  if (c.pitch > scene::CAM_PITCH_MAX) c.pitch = scene::CAM_PITCH_MAX;
  if (c.dist < scene::CAM_DIST_MIN) c.dist = scene::CAM_DIST_MIN;
  if (c.dist > scene::CAM_DIST_MAX) c.dist = scene::CAM_DIST_MAX;
}

// Per-stage time accumulators (us), reset every STATS_INTERVAL_US.
struct Stats {
  uint32_t frames;
  uint64_t keyUs;     // keypad read (I2C)
  uint64_t sceneUs;   // sceneBuild(): transform + lighting + projection
  uint64_t sortUs;    // beginRender(): depth sort
  uint64_t renderUs;  // render() over all bands
  uint64_t swapUs;    // overlay + byte swap
  uint64_t waitUs;    // waiting for the previous band's DMA + setWindow
  uint32_t frameMaxUs;
  uint32_t renderBandMaxUs;
};

static inline float avgMs(uint64_t us, uint32_t frames) {
  return frames ? static_cast<float>(us) / frames / 1000.0f : 0.0f;
}

//-----------------------------------------------------------------------------
// main
//-----------------------------------------------------------------------------
int main() {
  sysClockInit312();
  stdio_init_all();
  printf("\n=== gfx3d pretest ===\n");
  printf("clk_sys %lu Hz, %dx%d, band %d lines, arena %u B, perspective UV %d\n",
         static_cast<unsigned long>(clock_get_hz(clk_sys)), SCREEN_W, SCREEN_H, BAND_H,
         static_cast<unsigned>(ARENA_SIZE), PERSPECTIVE_UV);

  Ili9488::Pins lcdPins = {PIN_LCD_D0, PIN_LCD_DC, PIN_LCD_WR,
                           PIN_LCD_RD, PIN_LCD_CS, PIN_LCD_RST};
  const float lcdClkDiv = static_cast<float>(clock_get_hz(clk_sys)) / LCD_PIO_BASE_HZ;
  if (!gLcd.init(pio0, 0, lcdPins, lcdClkDiv)) panic("LCD PIO init failed");
  gLcd.clear(0x0000);

  auxI2cInit();

  scene::sceneInit();
  g3::init(SCREEN_W, SCREEN_H, gArena, sizeof(gArena));

  Camera cam;
  cameraReset(cam);
  bool paused = false;
  bool overlay = true;
  float animT = 0.0f;
  uint16_t prevKeys = 0;

  Stats st = {};
  char ovLine[OVERLAY_LINES][SCREEN_W / OVERLAY_GLYPH_W + 1] = {"measuring...", "", ""};

  uint64_t prevFrameUs = time_us_64();
  uint64_t lastStatsUs = prevFrameUs;

  while (true) {
    const uint64_t frameStart = time_us_64();
    const float dt = static_cast<float>(frameStart - prevFrameUs) * 1e-6f;
    const uint32_t frameUs = static_cast<uint32_t>(frameStart - prevFrameUs);
    prevFrameUs = frameStart;
    if (frameUs > st.frameMaxUs) st.frameMaxUs = frameUs;

    //--- keypad / camera ---------------------------------------------------
    uint16_t keys = 0;
    hostKeysRead(&keys);  // keys = 0 on error
    const uint16_t edge = keys & ~prevKeys;
    prevKeys = keys;
    if (edge & HKEY_STA) paused = !paused;
    if (edge & HKEY_SEL) overlay = !overlay;
    if (edge & HKEY_X) cameraReset(cam);
    cameraUpdate(cam, keys, dt);
    if (!paused) animT += dt;
    const uint64_t t0 = time_us_64();

    //--- scene -------------------------------------------------------------
    scene::sceneBuild(animT, cam.yaw, cam.pitch, cam.dist,
                      static_cast<float>(SCREEN_W) / SCREEN_H);
    const uint64_t t1 = time_us_64();
    g3::beginRender();
    const uint64_t t2 = time_us_64();

    //--- bands -------------------------------------------------------------
    // Band i goes to gBand[i & 1]. While band i renders, band i-1 (the other
    // buffer) is on the DMA; band i-2 used this buffer and was waited for
    // before band i-1 started.
    for (int i = 0; i < NUM_BANDS; ++i) {
      uint16_t* buf = gBand[i & 1];
      const int y = i * BAND_H;

      const uint64_t b0 = time_us_64();
      g3::render(0, static_cast<int16_t>(y), SCREEN_W, BAND_H, buf, SCREEN_W);
      const uint64_t b1 = time_us_64();

      if (overlay && y < OVERLAY_LINES * OVERLAY_GLYPH_H) {
        // Rows outside this band are clipped by overlayDrawText().
        for (int l = 0; l < OVERLAY_LINES; ++l) {
          overlayDrawText(buf, SCREEN_W, SCREEN_W, BAND_H, 0, l * OVERLAY_GLYPH_H - y, ovLine[l],
                          0xFFFF, 0x0000);
        }
      }
      Ili9488::swapBytes(buf, SCREEN_W * BAND_H);
      const uint64_t b2 = time_us_64();

      if (i == 0) {
        gLcd.setWindow(0, 0, SCREEN_W - 1, SCREEN_H - 1);  // waits for the last DMA
      } else {
        gLcd.waitDma();
      }
      const uint64_t b3 = time_us_64();
      gLcd.writeBytesAsync(buf, SCREEN_W * BAND_H * 2);

      const uint32_t bandUs = static_cast<uint32_t>(b1 - b0);
      st.renderUs += bandUs;
      if (bandUs > st.renderBandMaxUs) st.renderBandMaxUs = bandUs;
      st.swapUs += b2 - b1;
      st.waitUs += b3 - b2;
    }
    g3::endRender();
    const g3::Stats gs = g3::getStats();

    st.keyUs += t0 - frameStart;
    st.sceneUs += t1 - t0;
    st.sortUs += t2 - t1;
    st.frames++;

    //--- statistics ----------------------------------------------------------
    const uint64_t now = time_us_64();
    if (now - lastStatsUs >= STATS_INTERVAL_US) {
      const float fps = st.frames * 1e6f / static_cast<float>(now - lastStatsUs);
      const float frameMs = st.frames ? static_cast<float>(now - lastStatsUs) / st.frames / 1000.0f : 0.0f;
      printf("[gfx3d] fps=%.2f frame=%.2fms (max %.2f) key=%.2f scene=%.2f sort=%.2f"
             " render=%.2f (band max %.2f) swap=%.2f lcdwait=%.2f"
             " | tri=%d/%d(drop %d) span=%d/%d(drop %d) arena=%u/%u"
             " | cam yaw=%.2f pitch=%.2f dist=%.2f%s\n",
             fps, frameMs, st.frameMaxUs / 1000.0f, avgMs(st.keyUs, st.frames),
             avgMs(st.sceneUs, st.frames), avgMs(st.sortUs, st.frames),
             avgMs(st.renderUs, st.frames), st.renderBandMaxUs / 1000.0f,
             avgMs(st.swapUs, st.frames), avgMs(st.waitUs, st.frames), gs.triCount,
             gs.triCapacity, gs.triDropped, gs.spanPeak, gs.spanCapacity, gs.spanDropped,
             static_cast<unsigned>(gs.arenaUsed), static_cast<unsigned>(gs.arenaSize), cam.yaw,
             cam.pitch, cam.dist, paused ? " (paused)" : "");
      snprintf(ovLine[0], sizeof(ovLine[0]), "%5.1f fps  %6.2f ms/frame%s", fps, frameMs,
               paused ? "  PAUSED" : "");
      snprintf(ovLine[1], sizeof(ovLine[1]), "scene %.2f  sort %.2f  render %.2f  lcd %.2f",
               avgMs(st.sceneUs, st.frames), avgMs(st.sortUs, st.frames),
               avgMs(st.renderUs, st.frames), avgMs(st.waitUs, st.frames));
      // Triangle / span pool usage (last frame) and the arena bytes they occupy.
      snprintf(ovLine[2], sizeof(ovLine[2]), "tri %d/%d%s  span %d/%d%s  arena %.1f/%.0fKB %d%%",
               gs.triCount, gs.triCapacity, gs.triDropped ? "!" : "", gs.spanPeak,
               gs.spanCapacity, gs.spanDropped ? "!" : "", gs.arenaUsed / 1024.0f,
               gs.arenaSize / 1024.0f,
               static_cast<int>(gs.arenaUsed * 100 / (gs.arenaSize ? gs.arenaSize : 1)));
      st = {};
      lastStatsUs = now;
    }
  }
  return 0;
}
