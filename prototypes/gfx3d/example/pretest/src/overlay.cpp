#include "overlay.hpp"

#include <lcdtap/font8x16.hpp>

namespace wcb {

namespace font = lcdtap::font8x16;

static_assert(font::GLYPH_WIDTH == OVERLAY_GLYPH_W && font::GLYPH_HEIGHT == OVERLAY_GLYPH_H,
              "overlay layout assumes 8x16 glyphs");

void overlayDrawText(uint16_t* buf, int stride, int w, int h, int x, int y,
                     const char* text, uint16_t fg, uint16_t bg) {
  for (const char* p = text; *p; ++p, x += OVERLAY_GLYPH_W) {
    char c = *p;
    uint32_t idx = 0;
    if (c >= font::CODE_FIRST && c <= font::CODE_LAST) {
      idx = static_cast<uint32_t>(c - font::CODE_FIRST);
    }
    const uint8_t* rows = &font::bitmap[idx * OVERLAY_GLYPH_H];
    for (int r = 0; r < OVERLAY_GLYPH_H; ++r) {
      const int py = y + r;
      if (py < 0 || py >= h) continue;
      uint16_t* line = buf + py * stride;
      const uint8_t bits = rows[r];
      for (int b = 0; b < OVERLAY_GLYPH_W; ++b) {
        const int px = x + b;
        if (px < 0 || px >= w) continue;
        line[px] = (bits & (1u << b)) ? fg : bg;  // bit0 = leftmost
      }
    }
  }
}

}  // namespace wcb
