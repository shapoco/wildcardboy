#include "aux_i2c.hpp"

#include <initializer_list>

#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "board_pins.hpp"

namespace wcb {

static constexpr uint32_t I2C_TIMEOUT_US = 2000;

// PCA9555 register addresses.
static constexpr uint8_t PCA9555_REG_INPUT0 = 0x00;
static constexpr uint8_t PCA9555_REG_CONFIG0 = 0x06;

//-----------------------------------------------------------------------------
// Bus selection
//-----------------------------------------------------------------------------
// Both pairs land on I2C0 (GPIO % 4 == 0 -> SDA, == 1 -> SCL); only the pad
// functions move. The idle pair is parked as a Hi-Z SIO input.
//
// Two pitfalls, both of which leave the DW I2C master silently refusing to
// start a transfer (no waveform on either bus, every call times out):
//  * A peripheral input with no GPIO attached reads as 0, so a
//    "detach all, then attach" sequence shows the controller SDA=SCL=Low,
//    which it takes as another master's START (bus busy until a STOP).
//    -> attach the new pair before parking the old one.
//  * The SDK's timeout path leaves the controller as-is (stale FIFO, busy
//    state). -> after every switch, and after any failed transfer, the
//    controller is re-initialized (i2c_init() does a hardware reset).

enum class AuxBus : uint8_t { NONE, HAUX, LCAUX };

static AuxBus sCurrent = AuxBus::NONE;

static void parkPin(uint pin) {
  gpio_set_function(pin, GPIO_FUNC_SIO);
  gpio_set_dir(pin, GPIO_IN);
}

static void attachPin(uint pin) { gpio_set_function(pin, GPIO_FUNC_I2C); }

static void selectBus(AuxBus bus) {
  if (bus == sCurrent) return;

  if (bus == AuxBus::HAUX) {
    attachPin(PIN_HAUX_SDA);
    attachPin(PIN_HAUX_SCL);
    parkPin(PIN_LCAUX_SDA);
    parkPin(PIN_LCAUX_SCL);
  } else {
    attachPin(PIN_LCAUX_SDA);
    attachPin(PIN_LCAUX_SCL);
    parkPin(PIN_HAUX_SDA);
    parkPin(PIN_HAUX_SCL);
  }

  // Hardware reset + reconfigure: wipes any bus-busy / stale-FIFO state
  // from the previous bus or a timed-out transfer. Costs a few us.
  i2c_init(i2c0, AUX_I2C_BAUD);
  sCurrent = bus;
}

// Force a controller re-init on the next access (after a failed transfer).
static void invalidateBus() { sCurrent = AuxBus::NONE; }

void auxSelectHaux() { selectBus(AuxBus::HAUX); }
void auxSelectLcaux() { selectBus(AuxBus::LCAUX); }
void auxInvalidate() { invalidateBus(); }

//-----------------------------------------------------------------------------
// API
//-----------------------------------------------------------------------------

void auxI2cInit() {
  // Both pairs: parked Hi-Z, weak internal pull-up as a fallback (external
  // pull-ups are expected on each bus).
  for (uint pin : {PIN_HAUX_SDA, PIN_HAUX_SCL, PIN_LCAUX_SDA, PIN_LCAUX_SCL}) {
    parkPin(pin);
    gpio_pull_up(pin);
  }
  sCurrent = AuxBus::NONE;

  // PCA9555 defaults to all-inputs after power-up; write it anyway so a
  // warm reset of the host controller cannot leave stale configuration.
  selectBus(AuxBus::HAUX);  // also performs the first i2c_init()
  uint8_t cfg[3] = {PCA9555_REG_CONFIG0, 0xFF, 0xFF};
  if (i2c_write_timeout_us(i2c0, ADDR_HOST_KEYPAD, cfg, sizeof(cfg), false,
                           I2C_TIMEOUT_US) != sizeof(cfg)) {
    invalidateBus();
  }
}

bool cardEepromProbe() {
  selectBus(AuxBus::LCAUX);
  uint8_t b = 0;
  int r = i2c_read_timeout_us(i2c0, ADDR_CARD_EEPROM, &b, 1, false,
                              I2C_TIMEOUT_US);
  if (r != 1) invalidateBus();
  return r == 1;
}

bool hostKeysRead(uint16_t* keys) {
  *keys = 0;
  selectBus(AuxBus::HAUX);
  uint8_t reg = PCA9555_REG_INPUT0;
  if (i2c_write_timeout_us(i2c0, ADDR_HOST_KEYPAD, &reg, 1, true,
                           I2C_TIMEOUT_US) != 1) {
    invalidateBus();
    return false;
  }
  uint8_t in[2] = {0xFF, 0xFF};
  if (i2c_read_timeout_us(i2c0, ADDR_HOST_KEYPAD, in, 2, false,
                          I2C_TIMEOUT_US) != 2) {
    invalidateBus();
    return false;
  }
  uint16_t raw = static_cast<uint16_t>(in[0] | (in[1] << 8));
  *keys = static_cast<uint16_t>(~raw & 0x1FFFu);  // active-low -> 1 = pressed
  return true;
}

}  // namespace wcb
