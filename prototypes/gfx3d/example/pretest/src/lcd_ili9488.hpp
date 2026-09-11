#pragma once

// ILI9488 (320x480) driver over an 8080 8-bit parallel bus, using one PIO
// state machine (pretest/src/lcd8080.pio) and one DMA channel.
//
// Derived from pretest/src/ili9488.{hpp,cpp} (same init sequence and PIO
// setup); the per-line double buffer is replaced by an asynchronous bulk DMA
// of caller-owned, already byte-swapped pixel bands so that rendering the
// next band overlaps with the transfer of the current one.

#include <cstdint>

#include "hardware/pio.h"

namespace wcb {

class Ili9488 {
 public:
  static constexpr int LCD_W = 480;
  static constexpr int LCD_H = 320;

  struct Pins {
    uint d0;  // D0..D7 = d0 .. d0+7
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
  // issue RAMWR. Waits for any pending DMA first.
  void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

  // Start a DMA of `bytes` bytes that are already in bus order (see
  // swapBytes()). Waits for the previous DMA, then returns immediately;
  // `data` must stay untouched until waitDma() returns.
  void writeBytesAsync(const void* data, uint32_t bytes);

  // Block until the DMA has finished (the PIO may still be shifting out the
  // last few bytes; waitIdle() also waits for that).
  void waitDma();

  // Block until the DMA and the PIO state machine are both idle.
  void waitIdle();

  // Solid rectangle (landscape coordinates), clipped to the screen.
  void fillRect(int x, int y, int w, int h, uint16_t color);
  void clear(uint16_t color) { fillRect(0, 0, LCD_W, LCD_H, color); }

  // Native RGB565 -> bus order (high byte first), in place. `buf` must be
  // 4-byte aligned.
  static void swapBytes(uint16_t* buf, uint32_t n);

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

  // 2-byte pattern used with a DMA read ring for fillRect.
  alignas(4) uint8_t fillPattern_[4];
};

}  // namespace wcb
