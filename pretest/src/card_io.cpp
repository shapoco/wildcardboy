#include "card_io.hpp"

#include <cstdio>
#include <cstring>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "board_pins.hpp"

namespace wcb {

struct Line {
  uint8_t f = 0;
  uint8_t m = 0;
  bool configured = false;  // GPIO owned by card_io
  bool asserted = false;
};

static Line sLines[NUM_LCIO];
static uint8_t sKeymap[NUM_BUTTONS];
static int sResetIdx = -1;
static uint8_t sIspMethod = 0;
static PortCfg sIsp[NUM_LCIO];
static uint16_t sLastKeys = 0;

static inline uint pinOf(int lcio) { return PIN_LCIO_BASE + static_cast<uint>(lcio); }

static inline bool isNegative(const Line& l) { return (l.m & mode::NEGATIVE) != 0; }

// Configure one line as "assertable output" in its released state.
static void setupLine(int i, const PortCfg& cfg) {
  Line& l = sLines[i];
  l.f = cfg.f;
  l.m = cfg.m;
  l.asserted = false;
  uint8_t dir = cfg.m & mode::DIR_MASK;
  if (dir == 0 || cfg.f == func::LCD) return;  // unused / owned by the I2C slave

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

static void driveLine(int i, bool assert) {
  Line& l = sLines[i];
  if (!l.configured || l.asserted == assert) return;
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
  memcpy(sIsp, p.isp, sizeof(sIsp));

  for (int i = 0; i < NUM_LCIO; ++i) setupLine(i, p.lcio[i]);

  // RESET: prefer the lcio table; fall back to the isp table's definition.
  sResetIdx = profileFindPort(p.lcio, func::RESET);
  if (sResetIdx < 0) {
    sResetIdx = profileFindPort(p.isp, func::RESET);
    if (sResetIdx >= 0) setupLine(sResetIdx, p.isp[sResetIdx]);
  }
  if (sResetIdx < 0) printf("[card_io] profile has no RESET line\n");
  sLastKeys = 0;
}

void cardIoRelease() {
  for (int i = 0; i < NUM_LCIO; ++i) {
    if (sLines[i].configured) {
      uint pin = pinOf(i);
      gpio_init(pin);  // SIO input
      gpio_disable_pulls(pin);
    }
    sLines[i] = Line{};
  }
  sResetIdx = -1;
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
}

void cardKeysRelease() { cardKeysSet(0); }

bool cardHasReset() { return sResetIdx >= 0; }

void cardResetAssert() {
  if (sResetIdx >= 0) driveLine(sResetIdx, true);
}

void cardResetRelease() {
  if (sResetIdx >= 0) driveLine(sResetIdx, false);
}

void cardResetPulse(uint32_t lowMs) {
  cardKeysRelease();
  if (sResetIdx < 0) return;
  cardResetAssert();
  sleep_ms(lowMs);
  cardResetRelease();
}

bool cardIspPins(IspPins* out) {
  if (sIspMethod != isp_method::SPI) return false;
  int mosi = profileFindPort(sIsp, func::ISP_MOSI);
  int sck = profileFindPort(sIsp, func::ISP_SCK);
  int miso = profileFindPort(sIsp, func::ISP_MISO);
  if (mosi < 0 || sck < 0 || miso < 0 || sResetIdx < 0) return false;
  out->mosi = pinOf(mosi);
  out->sck = pinOf(sck);
  out->miso = pinOf(miso);
  return true;
}

}  // namespace wcb
