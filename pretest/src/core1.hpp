#pragma once

// Core 1 engine. Core 0 owns the UI, the host LCD and the card control;
// core 1 runs one of:
//   LCDTAP   : logic card LCD stream capture -> LcdTap -> line queue
//   USB_HOST : TinyUSB host (PIO-USB) for the USB ISP
//   IDLE     : nothing
// Mode changes and a few LcdTap calls are requested from core 0 through
// the inter-core FIFO; each command is acknowledged once executed.

#include <cstdint>

#include <lcdtap/lcdtap.hpp>

#include "line_queue.hpp"

namespace wcb {

enum class Core1Cmd : uint32_t {
  NOP = 0,
  LCDTAP_ATTACH,   // args: core1Shared().tap / bus / i2cAddr
  LCDTAP_DETACH,
  LCDTAP_RESET_ASSERT,
  LCDTAP_RESET_RELEASE,
  USB_START,
  USB_STOP,
};

enum class Core1Mode : uint8_t { IDLE, LCDTAP, USB_HOST };

struct Core1Shared {
  lcdtap::LcdTap* tap;
  lcdtap::BusType bus;
  uint8_t i2cAddr;
  LineQueue queue;
  volatile Core1Mode mode;
  volatile uint32_t loops;  // core1 loop counter (stats)
};

Core1Shared& core1Shared();

// Launch core 1 (call once from core 0 after the clocks are set up).
void core1Launch();

// Send a command and wait for core 1 to finish it.
void core1Call(Core1Cmd cmd);

}  // namespace wcb
