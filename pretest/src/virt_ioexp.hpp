#pragma once

// Virtual I/O expander: the host emulates the logic card's MCP23017 as an
// I2C slave on LCIO6/7 (i2c1), per spec/02 and spec/04. The card's MCU
// polls it exactly like the real chip; key states flow in through
// vioSetPin() (called by card_io's driveLine for LCIO64-79).
//
// The I2C1_IRQ handler runs on core 0: cardKeysSet() (the only writer of
// the pin image) also runs on core 0, so no cross-core synchronization is
// needed, and core 1 keeps its tight LcdTap / PIO-USB timing. The DW i2c
// block clock-stretches reads until the handler supplies the byte; the
// elevated IRQ priority keeps that stall in the low microseconds.

#include <cstdint>

namespace wcb {

struct VioStats {
  uint32_t writeBytes;  // register bytes written by the card
  uint32_t readBytes;   // register bytes read by the card
  uint32_t aborts;      // TX_ABRT events
  uint8_t lastReg;      // last register pointer latched
  bool bankWarn;        // card tried to set IOCON.BANK = 1 (unsupported)
};

// Start the emulation: i2c1 slave on LCIO6/7 at `addr`, registers at their
// power-on defaults. Must not be called while the LcdTap I2C capture owns
// i2c1 (the profile validation forbids that combination).
void vioInit(uint8_t addr);

// Stop and put LCIO6/7 back to Hi-Z.
void vioDeinit();

// Registers back to power-on defaults (IODIR = 0xFF, everything else 0).
void vioReset();

// Emulated pad level, pin 0..15 = GPA0..GPB7 (= LCIO64..79). Asserted =
// driven low (open-drain active-low key), released = pulled up high.
void vioSetPin(int pin, bool asserted);

const VioStats& vioStats();

}  // namespace wcb
