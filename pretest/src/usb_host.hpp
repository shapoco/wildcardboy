#pragma once

// Core 1: TinyUSB host over Pico-PIO-USB (LCUSB_DP/DM) with the MSC class,
// used by the USB ISP to push UF2 blocks into an RP2040/RP2350 in BOOTSEL
// mode. usbHostStart/Stop/Task run on core 1; the query / write functions
// are called from core 0 and hand the work to core 1.

#include <cstdint>

namespace wcb {

// Core 1 ---------------------------------------------------------------
void usbHostStart();
void usbHostStop();
void usbHostTask();

// Core 0 ---------------------------------------------------------------
bool usbHostInitFailed();
bool usbHostDeviceAttached();  // a device is attached (enumerated)
void usbHostCounters(uint32_t* attach, uint32_t* detach, uint32_t* mount, uint32_t* unmount);
bool usbHostMscMounted();      // an MSC LUN is ready
uint32_t usbHostMscBlockSize();
uint32_t usbHostMscBlockCount();

// Blocking write of `count` blocks (512 B each) at `lba`. Returns false on
// error or timeout.
bool usbHostWriteBlocks(uint32_t lba, const uint8_t* buf, uint32_t count,
                        uint32_t timeoutMs);

}  // namespace wcb
