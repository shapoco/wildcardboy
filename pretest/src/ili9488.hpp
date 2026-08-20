#pragma once

// ILI9488 (320x480) driver over an 8080 8-bit parallel bus, using one PIO
// state machine (see lcd8080.pio) and one DMA channel for pixel data.
//
// The panel is driven in landscape (LCD_WIDTH x LCD_HEIGHT = 480 x 320)
// with 16 bpp RGB565 (COLMOD 0x55, legal on the parallel interface).

#include <cstdint>

#include "hardware/pio.h"

namespace wcb {

class Ili9488 {
 public:
  static constexpr uint32_t LINE_PIXELS = 480;

  struct Pins {
    uint d0;   // D0..D7 = d0 .. d0+7
    uint dc;
    uint wr;
    uint rd;
    uint cs;
    uint rst;
  };

  // Loads the PIO program, claims a DMA channel, resets and initializes the
  // panel. Returns false if the PIO program could not be loaded.
  bool init(PIO pio, uint sm, const Pins& pins, float pioClkDiv = 1.0f);

  // Set the RAM write window (inclusive coordinates, landscape space) and
  // issue RAMWR. Following pixel writes fill the window row-major.
  void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

  // Queue one line (n <= LINE_PIXELS pixels, native RGB565) for DMA. Returns
  // as soon as the previous line's DMA has finished and this one has been
  // started; the caller may reuse `src` immediately (it is byte-swapped into
  // an internal buffer first).
  void writeLineAsync(const uint16_t* src, uint32_t n);

  // Write an arbitrary number of pixels (blocking until all are queued; call
  // waitIdle() to be sure they reached the panel).
  void writePixels(const uint16_t* src, uint32_t n);

  // Block until the DMA and the PIO state machine are both idle.
  void waitIdle();

  // Solid rectangle (landscape coordinates), clipped to the screen.
  void fillRect(int x, int y, int w, int h, uint16_t color);
  void clear(uint16_t color) { fillRect(0, 0, LCD_W, LCD_H, color); }

  static constexpr int LCD_W = 480;
  static constexpr int LCD_H = 320;

 private:
  void writeCommand(uint8_t cmd);
  void writeData(uint8_t data);
  void writeData(const uint8_t* data, uint32_t n);
  void putByte(uint8_t b);
  void waitSmIdle();
  void startDma(const void* src, uint32_t bytes, bool ringPattern);

  PIO pio_ = nullptr;
  uint sm_ = 0;
  int dmaCh_ = -1;
  Pins pins_{};

  // Two byte-swapped line buffers for double buffering (LINE_PIXELS px each).
  alignas(4) uint8_t lineBuf_[2][LINE_PIXELS * 2];
  uint8_t cur_ = 0;

  // 2-byte pattern used with a DMA read ring for fillRect.
  alignas(4) uint8_t fillPattern_[4];
};

}  // namespace wcb
