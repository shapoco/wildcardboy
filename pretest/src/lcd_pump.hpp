#pragma once

// Pushes the LcdTap output raster to the host LCD (core 0).
//
// The LcdTap instance is configured with dviWidth/dviHeight equal to the LCD
// size, so LcdTap::fillScanline(y) yields exactly one LCD line. Core 1 (which
// feeds LcdTap) scans the dirty map and publishes output line groups / full
// repaint requests through a LineQueue; this side only renders them.

#include <cstdint>

#include <lcdtap/lcdtap.hpp>

#include "ili9488.hpp"
#include "line_queue.hpp"

namespace wcb {

class LcdPump {
 public:
  void init(lcdtap::LcdTap* tap, Ili9488* lcd, LineQueue* queue);

  // Send up to `maxLines` LCD lines. Returns the number of lines sent.
  // Call frequently from the main loop.
  uint32_t process(uint32_t maxLines);

  // Schedule a full-screen resend (drops queued partial groups).
  void requestFullRepaint();

  bool isFullRepaintActive() const { return fullActive_; }

  // Statistics (free-running).
  uint32_t linesSent() const { return linesSent_; }
  uint32_t fullRepaints() const { return fullRepaints_; }
  uint32_t dirtyGroups() const { return dirtyGroups_; }

 private:
  uint32_t fullStep(uint32_t maxLines);
  uint32_t queueStep(uint32_t maxLines);
  void sendLine(uint16_t y);

  lcdtap::LcdTap* tap_ = nullptr;
  Ili9488* lcd_ = nullptr;
  LineQueue* queue_ = nullptr;

  bool fullActive_ = false;
  uint16_t fullY_ = 0;

  uint32_t linesSent_ = 0;
  uint32_t fullRepaints_ = 0;
  uint32_t dirtyGroups_ = 0;

  uint16_t lineBuf_[Ili9488::LINE_PIXELS];
};

}  // namespace wcb
