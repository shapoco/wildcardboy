#pragma once

// UART ISP for ESP8266 logic cards (isp.method = 2): drives the Espressif
// serial bootloader protocol through esp-serial-flasher over uart1
// (LCIO10/11), with RESET / BOOTSEL sequenced through card_io. The app
// binary is streamed from the TF card (no size limit from gAppImage).
// Runs on core 0, blocking. The card must be stopped (RESET asserted) on
// entry and is left stopped.

#include "isp_usb.hpp"  // IspUsbProgressFn

namespace wcb {

// Returns nullptr on success or a short error message.
const char* ispEspProgram(const char* path, IspUsbProgressFn progress, void* user);

}  // namespace wcb
