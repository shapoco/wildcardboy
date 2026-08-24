#pragma once

// AVR in-system (serial) programmer, bit-banged on the WildCardBus
// (MOSI/SCK/MISO per card profile, RESET via card_io). The device geometry
// (signature, flash size, page size) comes from an AvrDevice (card_profile).
//
// Caller protocol: release all other card lines and stop the I2C1 slave
// (the MOSI/SCK pins are its SDA/SCL), then ispBegin() .. ispEnd(). After
// ispEnd() the three data pins are Hi-Z and RESET is released, so the new
// program starts immediately; re-run cardIoInit() afterwards.

#include <cstdint>

#include "card_profile.hpp"

namespace wcb {

using IspProgressFn = void (*)(int percent, void* user);

struct IspPins {
  uint32_t mosi;
  uint32_t sck;
  uint32_t miso;
};

struct IspDeviceInfo {
  uint8_t signature[3];
  uint8_t fuseLow, fuseHigh, fuseExt, lock;
};

// Hold RESET low and enter serial programming mode (with retries). The
// data pins come from the card profile (see cardIspPins()); RESET is driven
// through card_io. `dev` selects the flash geometry / expected signature
// for the calls below.
bool ispBegin(const IspPins& pins, const AvrDevice* dev);

// Leave programming mode: data pins Hi-Z, RESET released.
void ispEnd();

void ispReadDevice(IspDeviceInfo* info);
bool ispSignatureMatches(const IspDeviceInfo& info);

bool ispChipErase();

// Program `len` bytes from `img` starting at flash address 0 (len must fit
// the device flash). Pages that are entirely 0xFF are skipped (the chip was
// just erased).
bool ispWriteFlash(const uint8_t* img, uint32_t len, IspProgressFn cb,
                   void* user);

// Read back and compare. On mismatch *firstMismatch is the byte address.
bool ispVerifyFlash(const uint8_t* img, uint32_t len, uint32_t* firstMismatch,
                    IspProgressFn cb, void* user);

}  // namespace wcb
