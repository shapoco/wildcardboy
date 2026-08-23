#pragma once

// Logic card profile EEPROM (24LC256 @0x50 on LCAUX): raw read/write and
// the profile frame read used by card detection.

#include <cstdint>

#include "card_profile.hpp"

namespace wcb {

using EepromProgressFn = void (*)(int percent, void* user);

bool eepromRead(uint16_t addr, uint8_t* dst, uint32_t len);

// Page-wise write (64-byte pages, ACK polling after each page).
bool eepromWrite(uint16_t addr, const uint8_t* src, uint32_t len,
                 EepromProgressFn cb = nullptr, void* user = nullptr);

// Read the profile frame at address 0 into buf (capacity PROFILE_FRAME_MAX)
// and parse it. *frameLen = bytes actually read. Returns BAD_LENGTH when
// the EEPROM does not answer.
ProfileError eepromReadProfile(uint8_t* buf, uint32_t* frameLen,
                               CardProfile* out, uint32_t* cborLen);

}  // namespace wcb
