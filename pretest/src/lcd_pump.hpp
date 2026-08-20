#pragma once

// Pushes the LcdTap output raster to the host LCD.
//
// The LcdTap instance is configured with dviWidth/dviHeight equal to the LCD
// size, so LcdTap::fillScanline(y) yields exactly one LCD line. Only the
// lines whose source framebuffer rows changed (LcdTap dirty map) are resent;
// a presentation epoch change (inversion, sleep, reset...) triggers a full
// repaint. Single-core: the caller must run this on the same core that
// feeds LcdTap.

#include <cstdint>

#include <lcdtap/lcdtap.hpp>

#include "ili9488.hpp"

namespace wcb {

class LcdPump {
 public:
  void init(lcdtap::LcdTap* tap, Ili9488* lcd);

  // Send up to `maxLines` LCD lines. Returns the number of lines sent.
  // Call frequently from the main loop.
  uint32_t process(uint32_t maxLines);

  // Schedule a full-screen resend (also clears the dirty map).
  void requestFullRepaint();

  bool isFullRepaintActive() const { return fullActive_; }

  // Statistics (free-running).
  uint32_t linesSent() const { return linesSent_; }
  uint32_t fullRepaints() const { return fullRepaints_; }
  uint32_t dirtyGroups() const { return dirtyGroups_; }

 private:
  uint32_t fullStep(uint32_t maxLines);
  uint32_t dirtyStep(const lcdtap::OutputMapInfo& mi, uint32_t maxLines);
  void sendLine(uint16_t y);

  lcdtap::LcdTap* tap_ = nullptr;
  Ili9488* lcd_ = nullptr;

  uint32_t lastEpoch_ = 0;
  bool fullActive_ = false;
  uint16_t fullY_ = 0;
  uint16_t dirtyRow_ = 0;  // scan cursor into the dirty map

  uint32_t linesSent_ = 0;
  uint32_t fullRepaints_ = 0;
  uint32_t dirtyGroups_ = 0;

  uint16_t lineBuf_[Ili9488::LINE_PIXELS];
};

}  // namespace wcb
