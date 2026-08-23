#include "lcd_pump.hpp"


namespace wcb {

// Force every process() call to repaint the whole screen (debug aid to
// compare against the incremental path).
#ifndef WCB_PUMP_ALWAYS_FULL
#define WCB_PUMP_ALWAYS_FULL 0
#endif

void LcdPump::init(lcdtap::LcdTap* tap, Ili9488* lcd, LineQueue* queue) {
  tap_ = tap;
  lcd_ = lcd;
  queue_ = queue;
  linesSent_ = 0;
  fullRepaints_ = 0;
  dirtyGroups_ = 0;
  requestFullRepaint();
}

void LcdPump::requestFullRepaint() {
  if (queue_) queue_->clear();
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

uint32_t LcdPump::queueStep(uint32_t maxLines) {
  uint32_t n = 0;
  uint32_t msg;
  while (n < maxLines && queue_->pop(&msg)) {
    if (msg == LineQueue::MSG_FULL_REPAINT) {
      requestFullRepaint();
      return n + fullStep(maxLines - n);
    }
    uint32_t y0 = LineQueue::lineY0(msg), y1 = LineQueue::lineY1(msg);
    if (y1 >= Ili9488::LCD_H) y1 = Ili9488::LCD_H - 1;
    if (y0 > y1) continue;
    // Full-width lines (column segments ignored).
    lcd_->setWindow(0, static_cast<uint16_t>(y0), Ili9488::LCD_W - 1, static_cast<uint16_t>(y1));
    for (uint32_t y = y0; y <= y1; ++y) sendLine(static_cast<uint16_t>(y));
    n += y1 - y0 + 1;
    dirtyGroups_++;
  }
  return n;
}

uint32_t LcdPump::process(uint32_t maxLines) {
  if (!tap_ || !lcd_ || !queue_) return 0;
#if WCB_PUMP_ALWAYS_FULL
  if (!fullActive_) requestFullRepaint();
#endif
  if (fullActive_) return fullStep(maxLines);
  return queueStep(maxLines);
}

}  // namespace wcb
