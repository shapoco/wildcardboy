#include "core1.hpp"

#include <cstdio>

#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "lcdtap_input.hpp"
#include "usb_host.hpp"

namespace wcb {

static Core1Shared sShared;

Core1Shared& core1Shared() { return sShared; }

static inline uint32_t nowMs() { return to_ms_since_boot(get_absolute_time()); }

static void handle(Core1Cmd cmd) {
  switch (cmd) {
    case Core1Cmd::NOP: break;
    case Core1Cmd::LCDTAP_ATTACH:
      if (sShared.mode == Core1Mode::USB_HOST) usbHostStop();
      lcdtapInputAttach(sShared.tap, sShared.bus, sShared.i2cAddr, &sShared.queue);
      sShared.mode = Core1Mode::LCDTAP;
      break;
    case Core1Cmd::LCDTAP_DETACH:
      lcdtapInputDetach();
      if (sShared.mode == Core1Mode::LCDTAP) sShared.mode = Core1Mode::IDLE;
      break;
    case Core1Cmd::LCDTAP_RESET_ASSERT: lcdtapInputSetReset(true); break;
    case Core1Cmd::LCDTAP_RESET_RELEASE: lcdtapInputSetReset(false); break;
    case Core1Cmd::USB_START:
      if (sShared.mode == Core1Mode::LCDTAP) lcdtapInputDetach();
      usbHostStart();
      sShared.mode = Core1Mode::USB_HOST;
      break;
    case Core1Cmd::USB_STOP:
      usbHostStop();
      if (sShared.mode == Core1Mode::USB_HOST) sShared.mode = Core1Mode::IDLE;
      break;
  }
}

static void core1Main() {
  while (true) {
    while (multicore_fifo_rvalid()) {
      uint32_t raw = multicore_fifo_pop_blocking();
      handle(static_cast<Core1Cmd>(raw));
      multicore_fifo_push_blocking(raw);  // ack
    }
    sShared.loops = sShared.loops + 1;
    switch (sShared.mode) {
      case Core1Mode::LCDTAP: lcdtapInputProcess(nowMs()); break;
      case Core1Mode::USB_HOST: usbHostTask(); break;
      default: __wfe(); break;
    }
  }
}

void core1Launch() {
  sShared.mode = Core1Mode::IDLE;
  sShared.tap = nullptr;
  multicore_launch_core1(core1Main);
}

void core1Call(Core1Cmd cmd) {
  multicore_fifo_push_blocking(static_cast<uint32_t>(cmd));
  uint32_t ack = multicore_fifo_pop_blocking();
  if (ack != static_cast<uint32_t>(cmd)) {
    printf("[core1] unexpected ack %lu for cmd %lu\n", static_cast<unsigned long>(ack),
           static_cast<unsigned long>(cmd));
  }
}

}  // namespace wcb
