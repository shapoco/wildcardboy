#include "card_io.hpp"

#include <cstdio>
#include <cstring>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include "aux_i2c.hpp"
#include "board_pins.hpp"

namespace wcb {

struct Line {
  uint8_t f = 0;
  uint8_t m = 0;
  bool configured = false;  // owned by card_io (GPIO or PCA9555 bit)
  bool asserted = false;
};

static Line sLines[NUM_LCIO];
static uint8_t sKeymap[NUM_BUTTONS];
static int sResetIdx = -1;
static int sBootselIdx = -1;
static uint8_t sIspMethod = 0;
static char sIspMcu[17];
static bool sUseTfCard = false;
static PortCfg sIsp[NUM_LCIO];
static uint16_t sLastKeys = 0;

// PCA9555 (LCIO32-47) output image and configuration.
static uint16_t sPcaOut = 0xFFFF;
static uint16_t sPcaCfg = 0xFFFF;  // 1 = input (Hi-Z)
static bool sPcaUsed = false;
static bool sPcaOutDirty = false;
static bool sPcaCfgDirty = false;

static constexpr uint8_t PCA_REG_OUTPUT0 = 0x02;
static constexpr uint8_t PCA_REG_CONFIG0 = 0x06;
static constexpr uint32_t PCA_TIMEOUT_US = 2000;

static inline uint pinOf(int lcio) { return PIN_LCIO_BASE + static_cast<uint>(lcio); }
static inline bool isNegative(const Line& l) { return (l.m & mode::NEGATIVE) != 0; }

static bool pcaWrite(uint8_t reg, uint16_t value) {
  auxSelectLcaux();
  uint8_t buf[3] = {reg, static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8)};
  if (i2c_write_timeout_us(i2c0, ADDR_CARD_KEYPAD, buf, 3, false, PCA_TIMEOUT_US) != 3) {
    printf("[card_io] PCA9555 write 0x%02x failed\n", reg);
    auxInvalidate();
    return false;
  }
  return true;
}

// Push the pending OUTPUT / CONFIG images. Output levels go first so a
// pin switched to output (open-drain assert) drives the right level.
static void pcaFlush() {
  if (!sPcaUsed) return;
  if (sPcaOutDirty && pcaWrite(PCA_REG_OUTPUT0, sPcaOut)) sPcaOutDirty = false;
  if (sPcaCfgDirty && pcaWrite(PCA_REG_CONFIG0, sPcaCfg)) sPcaCfgDirty = false;
}

// Configure one line as "assertable output" in its released state.
static void setupLine(int i, const PortCfg& cfg) {
  Line& l = sLines[i];
  l.f = cfg.f;
  l.m = cfg.m;
  l.asserted = false;
  uint8_t dir = cfg.m & mode::DIR_MASK;
  if (dir == 0 || cfg.f == func::LCD) return;  // unused / owned by the LCD capture

  if (lcioIsPca(i)) {
    uint16_t bit = 1u << (i - LCIO_PCA_FIRST);
    if (dir == mode::OUTPUT) {
      sPcaCfg &= static_cast<uint16_t>(~bit);  // push-pull output
      if (isNegative(l)) sPcaOut |= bit; else sPcaOut &= static_cast<uint16_t>(~bit);  // released level
    } else if (dir == mode::OPEN_DRAIN) {
      sPcaCfg |= bit;                           // released = input (Hi-Z)
      sPcaOut &= static_cast<uint16_t>(~bit);   // drives low whenever it is an output
    } else {
      return;  // validated away by the parser
    }
    sPcaUsed = true;
    l.configured = true;
    return;
  }

  uint pin = pinOf(i);
  gpio_init(pin);
  if (cfg.m & mode::PULL_UP) {
    gpio_pull_up(pin);
  } else if (cfg.m & mode::PULL_DOWN) {
    gpio_pull_down(pin);
  } else {
    gpio_disable_pulls(pin);
  }
  switch (dir) {
    case mode::INPUT:
      gpio_set_dir(pin, GPIO_IN);
      break;
    case mode::OUTPUT:
      gpio_put(pin, isNegative(l) ? 1 : 0);  // released level
      gpio_set_dir(pin, GPIO_OUT);
      break;
    case mode::OPEN_DRAIN:
      gpio_put(pin, 0);  // only ever drives low; direction does the rest
      gpio_set_dir(pin, GPIO_IN);
      break;
    default: break;
  }
  l.configured = true;
}

// Set the line state without flushing the PCA9555 (see pcaFlush()).
static void driveLine(int i, bool assert) {
  Line& l = sLines[i];
  if (!l.configured || l.asserted == assert) return;
  if (lcioIsPca(i)) {
    uint16_t bit = 1u << (i - LCIO_PCA_FIRST);
    if ((l.m & mode::DIR_MASK) == mode::OPEN_DRAIN) {
      // assert = output (low), release = input (Hi-Z)
      if (assert) sPcaCfg &= static_cast<uint16_t>(~bit); else sPcaCfg |= bit;
      sPcaCfgDirty = true;
    } else {
      bool high = assert ? !isNegative(l) : isNegative(l);
      if (high) sPcaOut |= bit; else sPcaOut &= static_cast<uint16_t>(~bit);
      sPcaOutDirty = true;
    }
    l.asserted = assert;
    return;
  }
  uint pin = pinOf(i);
  switch (l.m & mode::DIR_MASK) {
    case mode::OUTPUT:
      gpio_put(pin, assert ? (isNegative(l) ? 0 : 1) : (isNegative(l) ? 1 : 0));
      break;
    case mode::OPEN_DRAIN:
      gpio_set_dir(pin, assert ? GPIO_OUT : GPIO_IN);
      break;
    default: return;  // inputs cannot be asserted
  }
  l.asserted = assert;
}

void cardIoConfigure(const CardProfile& p) {
  cardIoRelease();
  memcpy(sKeymap, p.keymap, sizeof(sKeymap));
  sIspMethod = p.ispMethod;
  memcpy(sIspMcu, p.ispMcu, sizeof(sIspMcu));
  sUseTfCard = p.useTfCard;
  memcpy(sIsp, p.isp, sizeof(sIsp));
  sPcaOut = 0xFFFF;
  sPcaCfg = 0xFFFF;
  sPcaUsed = false;

  for (int i = 0; i < NUM_LCIO; ++i) {
    if (lcioIsValid(i)) setupLine(i, p.lcio[i]);
  }

  // RESET / BOOTSEL: prefer the lcio table; fall back to the isp table.
  auto pick = [&](uint8_t fn) {
    int idx = profileFindPort(p.lcio, fn);
    if (idx < 0) {
      idx = profileFindPort(p.isp, fn);
      if (idx >= 0 && lcioIsGpio(idx)) setupLine(idx, p.isp[idx]);
      else idx = -1;
    }
    return idx;
  };
  sResetIdx = pick(func::RESET);
  sBootselIdx = pick(func::BOOTSEL);
  if (sResetIdx < 0) printf("[card_io] profile has no RESET line\n");

  if (sPcaUsed) {
    // Released levels first, then switch the push-pull pins to outputs.
    pcaWrite(PCA_REG_OUTPUT0, sPcaOut);
    pcaWrite(PCA_REG_CONFIG0, sPcaCfg);
    sPcaOutDirty = false;
    sPcaCfgDirty = false;
  }
  sLastKeys = 0;
}

void cardIoRelease() {
  for (int i = 0; i < NUM_LCIO; ++i) {
    if (sLines[i].configured && lcioIsGpio(i)) {
      uint pin = pinOf(i);
      gpio_init(pin);  // SIO input
      gpio_disable_pulls(pin);
    }
    sLines[i] = Line{};
  }
  if (sPcaUsed) pcaWrite(PCA_REG_CONFIG0, 0xFFFF);  // all inputs again
  sPcaUsed = false;
  sPcaOutDirty = false;
  sPcaCfgDirty = false;
  sResetIdx = -1;
  sBootselIdx = -1;
  sLastKeys = 0;
}

void cardKeysSet(uint16_t hostKeys) {
  if (hostKeys == sLastKeys) return;
  sLastKeys = hostKeys;
  bool want[NUM_BUTTONS] = {};
  for (int s = 0; s < NUM_BUTTONS; ++s) {
    if ((hostKeys & (1u << s)) && sKeymap[s] < NUM_BUTTONS) want[sKeymap[s]] = true;
  }
  for (int i = 0; i < NUM_LCIO; ++i) {
    const Line& l = sLines[i];
    if (!l.configured || l.f < func::BTN_FIRST || l.f > func::BTN_LAST) continue;
    driveLine(i, want[l.f - func::BTN_FIRST]);
  }
  pcaFlush();
}

void cardKeysRelease() { cardKeysSet(0); }

bool cardHasReset() { return sResetIdx >= 0; }
void cardResetAssert() { if (sResetIdx >= 0) driveLine(sResetIdx, true); }
void cardResetRelease() { if (sResetIdx >= 0) driveLine(sResetIdx, false); }

bool cardHasBootsel() { return sBootselIdx >= 0; }
void cardBootselAssert() { if (sBootselIdx >= 0) driveLine(sBootselIdx, true); }
void cardBootselRelease() { if (sBootselIdx >= 0) driveLine(sBootselIdx, false); }

IspMode cardIspMode() {
  if (sIspMethod == isp_method::SPI) return IspMode::SPI;
  if (sIspMethod == isp_method::USB_MSC) return IspMode::USB;
  return IspMode::NONE;
}

const char* cardIspMcu() { return sIspMcu; }

bool cardUseTfCard() { return sUseTfCard; }

bool cardIspPins(IspPins* out) {
  if (sIspMethod != isp_method::SPI) return false;
  int mosi = profileFindPort(sIsp, func::ISP_MOSI);
  int sck = profileFindPort(sIsp, func::ISP_SCK);
  int miso = profileFindPort(sIsp, func::ISP_MISO);
  if (mosi < 0 || sck < 0 || miso < 0 || sResetIdx < 0) return false;
  if (!lcioIsGpio(mosi) || !lcioIsGpio(sck) || !lcioIsGpio(miso)) return false;
  out->mosi = pinOf(mosi);
  out->sck = pinOf(sck);
  out->miso = pinOf(miso);
  return true;
}

}  // namespace wcb
