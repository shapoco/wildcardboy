#pragma once

// ATtiny85 in-system (serial) programmer, bit-banged on the WildCardBus:
// LCIO2 = MOSI, LCIO3 = SCK, LCIO9 = MISO, LCIO13 = RESET (via card_io).
//
// Caller protocol: release all other card lines and stop the I2C1 slave
// (the MOSI/SCK pins are its SDA/SCL), then ispBegin() .. ispEnd(). After
// ispEnd() the three data pins are Hi-Z and RESET is released, so the new
// program starts immediately; re-run cardIoInit() afterwards.

#include <cstdint>

namespace wcb {

using IspProgressFn = void (*)(int percent, void* user);

struct IspDeviceInfo {
  uint8_t signature[3];
  uint8_t fuseLow, fuseHigh, fuseExt, lock;
};

static constexpr uint8_t TINY85_SIGNATURE[3] = {0x1E, 0x93, 0x0B};
static constexpr uint32_t TINY85_PAGE_BYTES = 64;  // 32 words

// Hold RESET low and enter serial programming mode (with retries).
bool ispBegin();

// Leave programming mode: data pins Hi-Z, RESET released.
void ispEnd();

void ispReadDevice(IspDeviceInfo* info);
bool ispIsTiny85(const IspDeviceInfo& info);

bool ispChipErase();

// Program `len` bytes from `img` starting at flash address 0. Pages that
// are entirely 0xFF are skipped (the chip was just erased).
bool ispWriteFlash(const uint8_t* img, uint32_t len, IspProgressFn cb,
                   void* user);

// Read back and compare. On mismatch *firstMismatch is the byte address.
bool ispVerifyFlash(const uint8_t* img, uint32_t len, uint32_t* firstMismatch,
                    IspProgressFn cb, void* user);

}  // namespace wcb
