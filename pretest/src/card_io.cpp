#include "card_io.hpp"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "board_pins.hpp"

namespace wcb {

struct KeyPin {
  uint16_t hostBit;
  uint pin;
};

static const KeyPin KEY_PINS[] = {
    {HKEY_L, PIN_LC_KEY_L}, {HKEY_R, PIN_LC_KEY_R}, {HKEY_U, PIN_LC_KEY_U},
    {HKEY_D, PIN_LC_KEY_D}, {HKEY_A, PIN_LC_KEY_A},
};

static uint16_t sLastKeys = 0;

static void odInit(uint pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  // Must be off: the default pull-down would load the card's ADC dividers
  // (and RP2350-E9 makes a pulled-down input pad misbehave near 3.3 V).
  gpio_disable_pulls(pin);
  gpio_put(pin, 0);  // output level is always low; direction does the rest
}

static inline void odAssert(uint pin) { gpio_set_dir(pin, GPIO_OUT); }
static inline void odRelease(uint pin) { gpio_set_dir(pin, GPIO_IN); }

void cardIoInit() {
  for (const KeyPin& k : KEY_PINS) odInit(k.pin);
  odInit(PIN_LC_RESET);
  sLastKeys = 0;
}

void cardKeysSet(uint16_t hostKeys) {
  hostKeys &= CKEY_MASK;
  if (hostKeys == sLastKeys) return;
  for (const KeyPin& k : KEY_PINS) {
    bool now = (hostKeys & k.hostBit) != 0;
    bool was = (sLastKeys & k.hostBit) != 0;
    if (now == was) continue;
    if (now) {
      odAssert(k.pin);
    } else {
      odRelease(k.pin);
    }
  }
  sLastKeys = hostKeys;
}

void cardKeysRelease() { cardKeysSet(0); }

void cardResetPulse(uint32_t lowMs) {
  cardKeysRelease();
  odAssert(PIN_LC_RESET);
  sleep_ms(lowMs);
  odRelease(PIN_LC_RESET);
}

}  // namespace wcb
