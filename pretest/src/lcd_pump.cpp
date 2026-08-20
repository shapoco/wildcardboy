#include "lcd_pump.hpp"

#include <cstring>

namespace wcb {

// Force every process() call to repaint the whole screen (debug aid to
// compare against the incremental path).
#ifndef WCB_PUMP_ALWAYS_FULL
#define WCB_PUMP_ALWAYS_FULL 0
#endif

// First output line whose mapped (crop-relative) source line index is t.
// srcLine(y) = ((y - destY) * stepV) >> 16 is monotonic, so the duplicate
// group of t is [firstLine(t), firstLine(t+1) - 1]. (Same math as
// lcdtap example/pico2_universal/include/displaylink_map.hpp.)
static inline uint32_t groupFirstLine(const lcdtap::OutputMapInfo& mi,
                                      uint32_t t) {
  if (mi.stepV == 0) return mi.destY;
  return mi.destY +
         static_cast<uint32_t>(
             ((static_cast<uint64_t>(t) << 16) + mi.stepV - 1u) / mi.stepV);
}

void LcdPump::init(lcdtap::LcdTap* tap, Ili9488* lcd) {
  tap_ = tap;
  lcd_ = lcd;
  tap_->setDirtyTracking(true);
  lastEpoch_ = tap_->getPresentationEpoch();
  dirtyRow_ = 0;
  linesSent_ = 0;
  fullRepaints_ = 0;
  dirtyGroups_ = 0;
  requestFullRepaint();
}

void LcdPump::requestFullRepaint() {
  // Clear the map first: rows written after this point get re-marked and
  // are picked up by the incremental path once the sweep is done.
  memset(tap_->dirtyMap(), 0, tap_->getConfig().buffHeight);
  fullActive_ = true;
  fullY_ = 0;
  fullRepaints_++;
}

void LcdPump::sendLine(uint16_t y) {
  tap_->fillScanline(y, lineBuf_);
  lcd_->writeLineAsync(lineBuf_, Ili9488::LINE_PIXELS);
  linesSent_++;
}

uint32_t LcdPump::fullStep(uint32_t maxLines) {
  uint32_t n = 0;
  if (fullY_ == 0) {
    lcd_->setWindow(0, 0, Ili9488::LCD_W - 1, Ili9488::LCD_H - 1);
  }
  // The RAM window set above persists between calls: nothing else touches
  // the LCD while the pump owns it, so the write pointer simply continues.
  while (fullY_ < Ili9488::LCD_H && n < maxLines) {
    sendLine(fullY_);
    fullY_++;
    n++;
  }
  if (fullY_ >= Ili9488::LCD_H) {
    fullActive_ = false;
    fullY_ = 0;
  }
  return n;
}

uint32_t LcdPump::dirtyStep(const lcdtap::OutputMapInfo& mi,
                            uint32_t maxLines) {
  const uint16_t fbH = mi.fbHeight;
  uint8_t* map = tap_->dirtyMap();
  const uint32_t yMax = static_cast<uint32_t>(mi.destY + mi.destH) - 1u;
  uint32_t n = 0;

  for (uint32_t scanned = 0; scanned < fbH; ++scanned) {
    uint32_t r = dirtyRow_;
    if (map[r] == 0) {
      dirtyRow_ = static_cast<uint16_t>((r + 1u) % fbH);
      continue;
    }

    // Rows outside the crop never reach the output.
    if (r < mi.srcY || r >= static_cast<uint32_t>(mi.srcY + mi.srcH)) {
      map[r] = 0;
      dirtyRow_ = static_cast<uint16_t>((r + 1u) % fbH);
      continue;
    }

    const uint32_t t = (mi.rotation == 0)
                           ? (r - mi.srcY)
                           : (static_cast<uint32_t>(mi.srcH) - 1u -
                              (r - mi.srcY));
    uint32_t y0 = groupFirstLine(mi, t);
    uint32_t y1 = groupFirstLine(mi, t + 1u);
    y1 = (y1 > y0) ? y1 - 1u : y0;
    if (y0 > yMax) {
      map[r] = 0;
      dirtyRow_ = static_cast<uint16_t>((r + 1u) % fbH);
      continue;
    }
    if (y1 > yMax) y1 = yMax;

    const uint32_t groupLines = y1 - y0 + 1u;
    if (n > 0 && n + groupLines > maxLines) {
      return n;  // out of budget; retry this row next call (still marked)
    }

    // Claim before reading pixels: writes landing while we send re-mark it.
    map[r] = 0;
    dirtyRow_ = static_cast<uint16_t>((r + 1u) % fbH);

    // Full-width lines (column segments ignored: cheap for a 128px source).
    lcd_->setWindow(0, static_cast<uint16_t>(y0), Ili9488::LCD_W - 1,
                    static_cast<uint16_t>(y1));
    for (uint32_t y = y0; y <= y1; ++y) sendLine(static_cast<uint16_t>(y));
    n += groupLines;
    dirtyGroups_++;
  }
  return n;
}

uint32_t LcdPump::process(uint32_t maxLines) {
  if (!tap_ || !lcd_) return 0;

  const uint32_t epoch = tap_->getPresentationEpoch();
  if (epoch != lastEpoch_) {
    lastEpoch_ = epoch;
    requestFullRepaint();
  }

#if WCB_PUMP_ALWAYS_FULL
  if (!fullActive_) requestFullRepaint();
#endif

  if (fullActive_) return fullStep(maxLines);

  lcdtap::OutputMapInfo mi;
  tap_->getOutputMapInfo(&mi);

  if (mi.rotation & 1u) {
    // Columns map to output lines in the transposed orientations; not needed
    // for the TinyJoypad preset, so fall back to a full repaint if anything
    // is dirty.
    uint8_t* map = tap_->dirtyMap();
    for (uint32_t r = 0; r < mi.fbHeight; ++r) {
      if (map[r]) {
        requestFullRepaint();
        return fullStep(maxLines);
      }
    }
    return 0;
  }

  return dirtyStep(mi, maxLines);
}

}  // namespace wcb
