#include "aux_i2c.hpp"

#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "board_pins.hpp"

namespace wcb {

static constexpr uint32_t I2C_TIMEOUT_US = 2000;

// PCA9555 register addresses.
static constexpr uint8_t PCA9555_REG_INPUT0 = 0x00;
static constexpr uint8_t PCA9555_REG_CONFIG0 = 0x06;

void auxI2cInit() {
  i2c_init(i2c0, AUX_I2C_BAUD);
  gpio_set_function(PIN_AUX_SDA, GPIO_FUNC_I2C);
  gpio_set_function(PIN_AUX_SCL, GPIO_FUNC_I2C);
  // External pull-ups are expected on the board; the internal ones only
  // help when the bus is left floating.
  gpio_pull_up(PIN_AUX_SDA);
  gpio_pull_up(PIN_AUX_SCL);

  // PCA9555 defaults to all-inputs after power-up; write it anyway so a
  // warm reset of the host controller cannot leave stale configuration.
  uint8_t cfg[3] = {PCA9555_REG_CONFIG0, 0xFF, 0xFF};
  i2c_write_timeout_us(i2c0, ADDR_HOST_KEYPAD, cfg, sizeof(cfg), false,
                       I2C_TIMEOUT_US);
}

bool cardEepromProbe() {
  uint8_t b = 0;
  int r = i2c_read_timeout_us(i2c0, ADDR_CARD_EEPROM, &b, 1, false,
                              I2C_TIMEOUT_US);
  return r == 1;
}

bool hostKeysRead(uint16_t* keys) {
  *keys = 0;
  uint8_t reg = PCA9555_REG_INPUT0;
  if (i2c_write_timeout_us(i2c0, ADDR_HOST_KEYPAD, &reg, 1, true,
                           I2C_TIMEOUT_US) != 1) {
    return false;
  }
  uint8_t in[2] = {0xFF, 0xFF};
  if (i2c_read_timeout_us(i2c0, ADDR_HOST_KEYPAD, in, 2, false,
                          I2C_TIMEOUT_US) != 2) {
    return false;
  }
  uint16_t raw = static_cast<uint16_t>(in[0] | (in[1] << 8));
  *keys = static_cast<uint16_t>(~raw & 0x1FFFu);  // active-low -> 1 = pressed
  return true;
}

}  // namespace wcb
