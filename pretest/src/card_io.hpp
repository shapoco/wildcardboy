#pragma once

// WildCardBus signals used by the TJP card: open-drain key outputs
// (LCIO5..9) and the ATtiny85 RESET line (LCIO13).
//
// "Open-drain" is emulated with the pad direction: output-low = asserted,
// input (Hi-Z) = released. Pulls are disabled because the card's key lines
// are resistor dividers read by the ATtiny85 ADC.

#include <cstdint>

namespace wcb {

// Configure the pins (all released / Hi-Z). Idempotent.
void cardIoInit();

// Drive the card key lines from a host key bitmask (HKEY_* bits, 1 = pressed).
// Only L/R/U/D/A are forwarded; the rest is ignored.
void cardKeysSet(uint16_t hostKeys);

// Release all key lines.
void cardKeysRelease();

// RESET line control (open-drain: assert = drive low, release = Hi-Z).
void cardResetAssert();
void cardResetRelease();

// Pulse RESET low. Keys are released first so the ATtiny85 sees idle inputs
// when it comes out of reset.
void cardResetPulse(uint32_t lowMs = 10);

}  // namespace wcb
