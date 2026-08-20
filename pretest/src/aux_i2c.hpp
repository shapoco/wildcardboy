#pragma once

// AUX I2C bus (I2C0 master, GPIO28/29): host keypad PCA9555 and the logic
// card's ID EEPROM.

#include <cstdint>

namespace wcb {

// Initialize I2C0 as a 400 kHz master on the AUX pins.
void auxI2cInit();

// True if the card EEPROM (24LC256 @0x50) ACKs a 1-byte read. Contents are
// irrelevant for the pretest; the ACK alone means "card present".
bool cardEepromProbe();

// Read the host keypad. Returns a bitmask of HKEY_* with 1 = pressed
// (the PCA9555 inputs are active-low and get inverted here). Returns false
// and leaves *keys = 0 on an I2C error.
bool hostKeysRead(uint16_t* keys);

}  // namespace wcb
