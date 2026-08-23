#pragma once

// AUX I2C buses (both on the I2C0 controller, see board_pins.hpp):
//   HAUX  (GPIO36/37) - host keypad PCA9555
//   LCAUX (GPIO28/29) - logic card ID EEPROM
// Each access selects its bus by switching the pad functions; the other
// pair is left Hi-Z so card hot-plug cannot disturb a host-side transfer.

#include <cstdint>

namespace wcb {

// Initialize I2C0 as a 400 kHz master; both pin pairs start Hi-Z.
void auxI2cInit();

// Route I2C0 to one of the buses (the other pair goes Hi-Z). The read/
// probe helpers below select their bus themselves; external users of I2C0
// (card_eeprom) call these explicitly.
void auxSelectHaux();
void auxSelectLcaux();
// Force a controller re-init on the next selection (after a failed transfer).
void auxInvalidate();

// True if the card EEPROM (24LC256 @0x50, LCAUX) ACKs a 1-byte read.
// Contents are irrelevant for the pretest; the ACK alone means "card
// present".
bool cardEepromProbe();

// Read the host keypad (HAUX). Returns a bitmask of HKEY_* with 1 = pressed
// (the PCA9555 inputs are active-low and get inverted here). Returns false
// and leaves *keys = 0 on an I2C error.
bool hostKeysRead(uint16_t* keys);

}  // namespace wcb
