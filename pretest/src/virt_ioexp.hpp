#pragma once

// Virtual I/O expander: the host emulates the logic card's I/O expander
// (MCP23017 or PCA9555, per virtIoExp.chip) as an I2C slave on LCIO6/7
// (i2c1), per spec/02 and spec/04. The card's MCU polls it exactly like
// the real chip; key states flow in through vioSetPin() (called by
// card_io's driveLine for LCIO64-79).
//
// The I2C1_IRQ handler runs on core 0: cardKeysSet() (the only writer of
// the pin image) also runs on core 0, so no cross-core synchronization is
// needed, and core 1 keeps its tight LcdTap / PIO-USB timing. The DW i2c
// block clock-stretches reads until the handler supplies the byte; the
// elevated IRQ priority keeps that stall in the low microseconds.

#include <cstdint>

namespace wcb {

enum class VioChip : uint8_t {
  MCP23017,  // BANK=0 register map 0x00-0x15, linear auto-increment
  PCA9555,   // command registers 0-7, auto-increment toggles within the pair
};

// Map virtIoExp.chip to a VioChip; unknown ids fall back to MCP23017
// (the profile validation rejects them before this is reached).
VioChip vioChipById(const char* id);

struct VioStats {
  uint32_t writeBytes;  // register bytes written by the card
  uint32_t readBytes;   // register bytes read by the card
  uint32_t aborts;      // TX_ABRT events
  uint8_t lastReg;      // last register pointer latched
  bool bankWarn;        // card tried to set IOCON.BANK = 1 (MCP23017 only)
};

// Start the emulation: i2c1 slave on LCIO6/7 at `addr`, registers at their
// power-on defaults. Must not be called while the LcdTap I2C capture owns
// i2c1 (the profile validation forbids that combination).
void vioInit(VioChip chip, uint8_t addr);

// Stop and put LCIO6/7 back to Hi-Z.
void vioDeinit();

// Registers back to power-on defaults (MCP23017: IODIR = 0xFF, everything
// else 0; PCA9555: Output = 0xFF, Polarity = 0x00, Config = 0xFF).
void vioReset();

// Emulated pad level, pin 0..15 = port A/B bit 0..7 (= LCIO64..79).
// Asserted = driven low (open-drain active-low key), released = pulled up
// high.
void vioSetPin(int pin, bool asserted);

const VioStats& vioStats();

}  // namespace wcb
