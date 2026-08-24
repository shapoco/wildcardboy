#pragma once

// Logic card profile EEPROM (24LC256 @0x50 on LCAUX): raw read/write and
// the profile frame read used by card detection.

#include <cstdint>

#include "card_profile.hpp"

namespace wcb {

using EepromProgressFn = void (*)(int percent, void* user);

bool eepromRead(uint16_t addr, uint8_t* dst, uint32_t len);

// Detect the write-page size (8..256 bytes) by exploiting the in-page
// address wrap of an oversized page write: a 256-byte pattern 0,1,..,255
// written at address 0 leaves (256 - pageSize) in byte 0. Destroys the
// first 256 bytes (the caller overwrites them right after). Returns 0 when
// no plausible size is detected (e.g. a small single-byte-address device).
uint32_t eepromDetectPageSize();

// Page-wise write (ACK polling after each page). pageSize must be a power
// of two, 8..256 (see eepromDetectPageSize()).
bool eepromWrite(uint16_t addr, const uint8_t* src, uint32_t len,
                 uint32_t pageSize, EepromProgressFn cb = nullptr,
                 void* user = nullptr);

// Read the profile frame at address 0 into buf (capacity PROFILE_FRAME_MAX)
// and parse it. *frameLen = bytes actually read. Returns BAD_LENGTH when
// the EEPROM does not answer.
ProfileError eepromReadProfile(uint8_t* buf, uint32_t* frameLen,
                               CardProfile* out, uint32_t* cborLen);

}  // namespace wcb
