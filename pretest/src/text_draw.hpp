#pragma once

// Minimal text rendering straight into the host LCD (no framebuffer), using
// the 8x16 glyphs bundled with LcdTap. Used for boot banner / status text.

#include <cstdint>

#include "ili9488.hpp"

namespace wcb {

static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) |
                               (b >> 3));
}

static constexpr uint16_t COLOR_BLACK = 0x0000;
static constexpr uint16_t COLOR_WHITE = 0xFFFF;
static constexpr uint16_t COLOR_RED = rgb565(255, 0, 0);
static constexpr uint16_t COLOR_GREEN = rgb565(0, 255, 0);
static constexpr uint16_t COLOR_BLUE = rgb565(0, 0, 255);
static constexpr uint16_t COLOR_YELLOW = rgb565(255, 255, 0);
static constexpr uint16_t COLOR_GRAY = rgb565(96, 96, 96);

// Draw `text` with its top-left corner at (x, y), each glyph 8*scale x
// 16*scale pixels. Background is filled with `bg`.
void drawText(Ili9488& lcd, int x, int y, const char* text, uint16_t fg,
              uint16_t bg, int scale = 2);

// Same, horizontally centered on the screen.
void drawTextCentered(Ili9488& lcd, int y, const char* text, uint16_t fg,
                      uint16_t bg, int scale = 2);

}  // namespace wcb
