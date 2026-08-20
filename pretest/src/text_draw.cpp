#include "text_draw.hpp"

#include <cstring>

#include <lcdtap/font8x16.hpp>

namespace wcb {

using lcdtap::font8x16::GLYPH_HEIGHT;
using lcdtap::font8x16::GLYPH_WIDTH;

static constexpr int MAX_SCALE = 4;

// One glyph row (8*scale pixels), scaled and repeated `scale` times
// vertically, is sent as one write of 8*scale*scale pixels: the RAM window
// covers only the glyph cell so row-major order wraps automatically.
static void drawGlyph(Ili9488& lcd, int x, int y, char c, uint16_t fg,
                      uint16_t bg, int scale) {
  uint8_t idx = 0;
  if (c >= lcdtap::font8x16::CODE_FIRST && c <= lcdtap::font8x16::CODE_LAST) {
    idx = static_cast<uint8_t>(c - lcdtap::font8x16::CODE_FIRST);
  }
  const uint8_t* rows =
      &lcdtap::font8x16::bitmap[static_cast<uint32_t>(idx) * GLYPH_HEIGHT];

  const int cellW = GLYPH_WIDTH * scale;
  const int cellH = GLYPH_HEIGHT * scale;
  if (x < 0 || y < 0 || x + cellW > Ili9488::LCD_W ||
      y + cellH > Ili9488::LCD_H) {
    return;
  }

  lcd.setWindow(static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                static_cast<uint16_t>(x + cellW - 1),
                static_cast<uint16_t>(y + cellH - 1));

  uint16_t line[GLYPH_WIDTH * MAX_SCALE * MAX_SCALE];
  for (int r = 0; r < GLYPH_HEIGHT; ++r) {
    uint8_t bits = rows[r];
    uint16_t* p = line;
    for (int rep = 0; rep < scale; ++rep) {
      for (int b = 0; b < GLYPH_WIDTH; ++b) {
        uint16_t col = (bits & (1u << b)) ? fg : bg;  // bit0 = leftmost
        for (int s = 0; s < scale; ++s) *p++ = col;
      }
    }
    lcd.writePixels(line, static_cast<uint32_t>(cellW * scale));
  }
}

void drawText(Ili9488& lcd, int x, int y, const char* text, uint16_t fg,
              uint16_t bg, int scale) {
  if (scale < 1) scale = 1;
  if (scale > MAX_SCALE) scale = MAX_SCALE;
  for (const char* p = text; *p; ++p) {
    drawGlyph(lcd, x, y, *p, fg, bg, scale);
    x += GLYPH_WIDTH * scale;
  }
  lcd.waitIdle();
}

void drawTextCentered(Ili9488& lcd, int y, const char* text, uint16_t fg,
                      uint16_t bg, int scale) {
  if (scale < 1) scale = 1;
  if (scale > MAX_SCALE) scale = MAX_SCALE;
  int w = static_cast<int>(strlen(text)) * GLYPH_WIDTH * scale;
  drawText(lcd, (Ili9488::LCD_W - w) / 2, y, text, fg, bg, scale);
}

}  // namespace wcb
