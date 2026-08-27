#include "virt_ioexp.hpp"

#include <cstring>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/regs/i2c.h"
#include "hardware/structs/i2c.h"
#include "pico/stdlib.h"

#include "board_pins.hpp"

namespace wcb {

// MCP23017 BANK=0 register map.
static constexpr int MCP_NUM_REGS = 0x16;
static constexpr int REG_IODIRA = 0x00;
static constexpr int REG_IODIRB = 0x01;
static constexpr int REG_IOCONA = 0x0A;
static constexpr int REG_IOCONB = 0x0B;
static constexpr int REG_INTFA = 0x0E;
static constexpr int REG_INTFB = 0x0F;
static constexpr int REG_INTCAPA = 0x10;
static constexpr int REG_INTCAPB = 0x11;
static constexpr int REG_GPIOA = 0x12;
static constexpr int REG_GPIOB = 0x13;
static constexpr int REG_OLATA = 0x14;
static constexpr int REG_OLATB = 0x15;

// PCA9555 command registers.
static constexpr int PREG_INPUT0 = 0;
static constexpr int PREG_INPUT1 = 1;
static constexpr int PREG_OUTPUT0 = 2;
static constexpr int PREG_POLINV0 = 4;
static constexpr int PREG_CONFIG0 = 6;
static constexpr int PCA_NUM_REGS = 8;

static VioChip sChip = VioChip::MCP23017;
static volatile uint8_t sRegs[MCP_NUM_REGS];  // MCP23017: 0x00-0x15, PCA9555: 0-7
static volatile uint8_t sRegPtr = 0;
// Emulated pad levels, bit n = port A/B pin n; 1 = high (released / pull-up).
static volatile uint16_t sPinLevels = 0xFFFF;
static bool sInited = false;
static VioStats sStats;

//-----------------------------------------------------------------------------
// MCP23017
//-----------------------------------------------------------------------------

// GPIOx read: inputs follow the pad levels (unassigned pins float high on
// the emulated pull-up), outputs read back their OLAT bit. IPOL is stored
// but not applied (spec/04).
static inline uint8_t __not_in_flash_func(mcpInputByte)(int port) {
  uint8_t iodir = sRegs[REG_IODIRA + port];
  uint8_t olat = sRegs[REG_OLATA + port];
  uint8_t lvl = static_cast<uint8_t>(sPinLevels >> (8 * port));
  return static_cast<uint8_t>((lvl & iodir) | (olat & static_cast<uint8_t>(~iodir)));
}

static inline uint8_t __not_in_flash_func(mcpReadReg)(uint8_t idx) {
  switch (idx) {
    case REG_GPIOA: return mcpInputByte(0);
    case REG_GPIOB: return mcpInputByte(1);
    case REG_INTFA:
    case REG_INTFB:
    case REG_INTCAPA:
    case REG_INTCAPB:
      return 0;  // interrupts are not emulated (spec/04)
    default: return sRegs[idx];
  }
}

static inline void __not_in_flash_func(mcpWriteReg)(uint8_t idx, uint8_t v) {
  switch (idx) {
    case REG_GPIOA: sRegs[REG_OLATA] = v; break;  // GPIO writes land in OLAT
    case REG_GPIOB: sRegs[REG_OLATB] = v; break;
    case REG_INTFA:
    case REG_INTFB:
    case REG_INTCAPA:
    case REG_INTCAPB:
      break;  // read-only
    case REG_IOCONA:
    case REG_IOCONB:
      if (v & 0x80) sStats.bankWarn = true;  // IOCON.BANK = 1 not supported
      sRegs[REG_IOCONA] = v;  // IOCON is one shared register
      sRegs[REG_IOCONB] = v;
      break;
    default: sRegs[idx] = v; break;
  }
}

//-----------------------------------------------------------------------------
// PCA9555
//-----------------------------------------------------------------------------

// Input port read: input bits follow the pad levels, output bits read back
// the Output register, and the Polarity Inversion register is applied to
// the whole byte (spec/04 - unlike the MCP23017 emulation, polarity
// inversion is honored, for future cards that use it).
static inline uint8_t __not_in_flash_func(pcaInputByte)(int port) {
  uint8_t cfg = sRegs[PREG_CONFIG0 + port];  // 1 = input
  uint8_t out = sRegs[PREG_OUTPUT0 + port];
  uint8_t pol = sRegs[PREG_POLINV0 + port];
  uint8_t lvl = static_cast<uint8_t>(sPinLevels >> (8 * port));
  return static_cast<uint8_t>(((lvl & cfg) | (out & static_cast<uint8_t>(~cfg))) ^ pol);
}

static inline uint8_t __not_in_flash_func(pcaReadReg)(uint8_t idx) {
  switch (idx) {
    case PREG_INPUT0: return pcaInputByte(0);
    case PREG_INPUT1: return pcaInputByte(1);
    default: return sRegs[idx];
  }
}

static inline void __not_in_flash_func(pcaWriteReg)(uint8_t idx, uint8_t v) {
  if (idx == PREG_INPUT0 || idx == PREG_INPUT1) return;  // read-only
  sRegs[idx] = v;
}

//-----------------------------------------------------------------------------
// I2C slave IRQ
//-----------------------------------------------------------------------------

// Auto-increment: MCP23017 walks the whole map linearly (SEQOP = 0);
// PCA9555 toggles within the register pair (0<->1, 2<->3, ...).
static inline uint8_t __not_in_flash_func(nextPtr)(bool pca, uint8_t p) {
  return pca ? static_cast<uint8_t>(p ^ 1u) : static_cast<uint8_t>((p + 1u) % MCP_NUM_REGS);
}

static void __not_in_flash_func(vioIrqHandler)() {
  i2c_hw_t* hw = i2c_get_hw(i2c1);
  uint32_t intr = hw->intr_stat;
  const bool pca = sChip == VioChip::PCA9555;

  if (intr & I2C_IC_INTR_STAT_R_RX_FULL_BITS) {
    uint32_t raw = hw->data_cmd;
    uint8_t byte = static_cast<uint8_t>(raw & 0xFFu);
    if (raw & I2C_IC_DATA_CMD_FIRST_DATA_BYTE_BITS) {
      // First byte after the address = register pointer / command byte. It
      // survives STOP (both chips: pointer write, STOP, then a read).
      if (pca) {
        sRegPtr = static_cast<uint8_t>(byte % PCA_NUM_REGS);
      } else {
        sRegPtr = byte < MCP_NUM_REGS ? byte : static_cast<uint8_t>(byte % MCP_NUM_REGS);
      }
      sStats.lastReg = sRegPtr;
    } else {
      if (pca) pcaWriteReg(sRegPtr, byte); else mcpWriteReg(sRegPtr, byte);
      sRegPtr = nextPtr(pca, sRegPtr);
      sStats.writeBytes = sStats.writeBytes + 1;
    }
  }

  if (intr & I2C_IC_INTR_STAT_R_RD_REQ_BITS) {
    hw->data_cmd = pca ? pcaReadReg(sRegPtr) : mcpReadReg(sRegPtr);
    (void)hw->clr_rd_req;
    sRegPtr = nextPtr(pca, sRegPtr);
    sStats.readBytes = sStats.readBytes + 1;
  }

  if (intr & I2C_IC_INTR_STAT_R_TX_ABRT_BITS) {
    (void)hw->clr_tx_abrt;
    sStats.aborts = sStats.aborts + 1;
  }
  if (intr & I2C_IC_INTR_STAT_R_RX_OVER_BITS) {
    (void)hw->clr_rx_over;
  }
}

//-----------------------------------------------------------------------------
// Public API
//-----------------------------------------------------------------------------

VioChip vioChipById(const char* id) {
  if (id != nullptr && strcmp(id, "pca9555") == 0) return VioChip::PCA9555;
  return VioChip::MCP23017;
}

void vioReset() {
  for (int i = 0; i < MCP_NUM_REGS; ++i) sRegs[i] = 0;
  if (sChip == VioChip::PCA9555) {
    sRegs[PREG_OUTPUT0] = 0xFF;
    sRegs[PREG_OUTPUT0 + 1] = 0xFF;
    sRegs[PREG_CONFIG0] = 0xFF;  // all inputs
    sRegs[PREG_CONFIG0 + 1] = 0xFF;
  } else {
    sRegs[REG_IODIRA] = 0xFF;
    sRegs[REG_IODIRB] = 0xFF;
  }
  sRegPtr = 0;
}

void vioInit(VioChip chip, uint8_t addr) {
  if (sInited) vioDeinit();
  sChip = chip;
  sPinLevels = 0xFFFF;
  memset(&sStats, 0, sizeof(sStats));
  vioReset();

  i2c_init(i2c1, 400 * 1000);  // baudrate irrelevant in slave mode
  gpio_set_function(PIN_LCVIO_SDA, GPIO_FUNC_I2C);
  gpio_set_function(PIN_LCVIO_SCL, GPIO_FUNC_I2C);
  // The card carries the real 4.7k pull-ups; the internal ones just keep
  // the bus defined on a bench without a card plugged.
  gpio_pull_up(PIN_LCVIO_SDA);
  gpio_pull_up(PIN_LCVIO_SCL);

  i2c_set_slave_mode(i2c1, true, addr);

  i2c_get_hw(i2c1)->intr_mask =
      I2C_IC_INTR_MASK_M_RX_FULL_BITS | I2C_IC_INTR_MASK_M_RD_REQ_BITS |
      I2C_IC_INTR_MASK_M_TX_ABRT_BITS | I2C_IC_INTR_MASK_M_RX_OVER_BITS;

  // The LcdTap I2C capture (other cards) leaves its exclusive handler
  // installed after deinit; evict whatever is there before claiming.
  irq_handler_t cur = irq_get_exclusive_handler(I2C1_IRQ);
  if (cur && cur != vioIrqHandler) irq_remove_handler(I2C1_IRQ, cur);
  irq_set_exclusive_handler(I2C1_IRQ, vioIrqHandler);
  // Elevated priority: reads clock-stretch until the handler answers, and
  // equal-priority IRQs cannot preempt each other on Cortex-M33.
  irq_set_priority(I2C1_IRQ, PICO_DEFAULT_IRQ_PRIORITY >> 1);
  irq_set_enabled(I2C1_IRQ, true);
  sInited = true;
}

void vioDeinit() {
  if (!sInited) return;
  irq_set_enabled(I2C1_IRQ, false);
  irq_remove_handler(I2C1_IRQ, vioIrqHandler);
  i2c_deinit(i2c1);
  gpio_init(PIN_LCVIO_SDA);
  gpio_init(PIN_LCVIO_SCL);
  gpio_disable_pulls(PIN_LCVIO_SDA);
  gpio_disable_pulls(PIN_LCVIO_SCL);
  sInited = false;
}

void vioSetPin(int pin, bool asserted) {
  if (pin < 0 || pin > 15) return;
  uint16_t bit = static_cast<uint16_t>(1u << pin);
  if (asserted) {
    sPinLevels = sPinLevels & static_cast<uint16_t>(~bit);
  } else {
    sPinLevels = sPinLevels | bit;
  }
}

const VioStats& vioStats() { return sStats; }

}  // namespace wcb
