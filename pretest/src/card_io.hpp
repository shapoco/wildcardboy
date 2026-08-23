#pragma once

// WildCardBus line control driven by the card profile: key lines, RESET,
// BOOTSEL and the ISP pin lookup. Each LCIO is configured from its PortCfg
// (mode bits) — open-drain lines are emulated with the pad direction
// (output low = asserted, input = released), push-pull lines switch the
// level. LCIO32-47 live on the card-side PCA9555 (push-pull outputs,
// written over LCAUX I2C).

#include <cstdint>

#include "card_profile.hpp"
#include "isp_tiny85.hpp"

namespace wcb {

enum class IspMode : uint8_t { NONE, SPI, USB };

// Configure the GPIOs / PCA9555 for every port in the profile (LCD I/F pins
// are left to the LcdTap capture). All asserted lines start released.
void cardIoConfigure(const CardProfile& p);

// Put every LCIO touched by the profile back to Hi-Z (card removed).
void cardIoRelease();

// Drive the card key lines from a host key bitmask (HKEY_* bits, 1 =
// pressed) through the profile key map. Only changed lines are touched.
void cardKeysSet(uint16_t hostKeys);
void cardKeysRelease();

// RESET (function 32) / BOOTSEL (function 33). No-ops when absent.
bool cardHasReset();
void cardResetAssert();
void cardResetRelease();
bool cardHasBootsel();
void cardBootselAssert();
void cardBootselRelease();

IspMode cardIspMode();
bool cardUseTfCard();

// SPI ISP pins from the profile's isp table; false if not available.
bool cardIspPins(IspPins* out);

}  // namespace wcb
