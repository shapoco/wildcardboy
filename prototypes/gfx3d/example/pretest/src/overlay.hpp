#pragma once

// Text overlay drawn into a render band (native RGB565, before the byte
// swap), using the 8x16 glyphs bundled with LcdTap.

#include <cstdint>

namespace wcb {

static constexpr int OVERLAY_GLYPH_W = 8;
static constexpr int OVERLAY_GLYPH_H = 16;

// Draw `text` with its top-left corner at (x, y) of a w x h pixel buffer
// with row pitch `stride` (pixels). Cells are filled with `bg`; anything
// outside the buffer is clipped.
void overlayDrawText(uint16_t* buf, int stride, int w, int h, int x, int y,
                     const char* text, uint16_t fg, uint16_t bg);

}  // namespace wcb
