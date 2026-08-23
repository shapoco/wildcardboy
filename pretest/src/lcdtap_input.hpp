#pragma once

// Core 1: logic card LCD stream -> LcdTap. Owns the I2C slave (LCIO2/3)
// or the 4-line SPI slave (LCIO1-4, RST on LCIO0) capture, drains their
// ring buffers into the LcdTap instance, scans the dirty map and publishes
// output line groups to core 0 through the LineQueue.
//
// All functions here run on core 1 (called from core1.cpp). The LcdTap
// instance is created/destroyed by core 0 but only core 1 feeds it while
// attached; core 0 limits itself to fillScanline()/getOutputMapInfo().

#include <cstdint>

#include <lcdtap/lcdtap.hpp>

#include "line_queue.hpp"

namespace wcb {

struct LcdtapInputStats {
  volatile uint32_t dropWords;
  volatile uint32_t hwOverflow;
  volatile uint32_t backlogMax;
  volatile uint32_t groups;        // line groups published
  volatile uint32_t fullRepaints;  // FULL_REPAINT messages published
  volatile uint32_t queueFull;     // groups dropped (queue full -> full repaint)
};

void lcdtapInputAttach(lcdtap::LcdTap* tap, lcdtap::BusType bus, uint8_t i2cAddr,
                       LineQueue* queue);
void lcdtapInputDetach();
bool lcdtapInputAttached();

// Drain the capture ring, tick LcdTap and publish dirty line groups.
void lcdtapInputProcess(uint32_t nowMs);

// Mirror of the card RESET line into LcdTap.
void lcdtapInputSetReset(bool assert);

const LcdtapInputStats& lcdtapInputStats();

}  // namespace wcb
