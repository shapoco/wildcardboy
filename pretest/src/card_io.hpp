#pragma once

// WildCardBus line control driven by the card profile: key lines, RESET
// and the ISP pin lookup. Each LCIO is configured from its PortCfg (mode
// bits) — open-drain lines are emulated with the pad direction (output
// low = asserted, input = released), push-pull lines switch the level.

#include <cstdint>

#include "card_profile.hpp"
#include "isp_tiny85.hpp"

namespace wcb {

// Configure the GPIOs for every port in the profile (LCD I/F pins are left
// to the I2C slave setup). All asserted lines start released.
void cardIoConfigure(const CardProfile& p);

// Put every LCIO touched by the profile back to Hi-Z (card stopped).
void cardIoRelease();

// Drive the card key lines from a host key bitmask (HKEY_* bits, 1 =
// pressed) through the profile key map. Only changed lines are touched.
void cardKeysSet(uint16_t hostKeys);
void cardKeysRelease();

// RESET line (function 32). No-ops (with a log) when the profile has none.
bool cardHasReset();
void cardResetAssert();
void cardResetRelease();
void cardResetPulse(uint32_t lowMs = 10);

// SPI ISP pins from the profile's isp table; false if not available.
bool cardIspPins(IspPins* out);

}  // namespace wcb
