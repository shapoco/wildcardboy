#include "card_eeprom.hpp"

#include <cstdio>

#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include "aux_i2c.hpp"
#include "board_pins.hpp"

namespace wcb {

static constexpr uint32_t PAGE_SIZE = 64;
static constexpr uint32_t READ_CHUNK = 128;
static constexpr uint32_t XFER_TIMEOUT_US = 20000;
static constexpr uint32_t WRITE_CYCLE_TIMEOUT_MS = 20;  // tWC = 5 ms typ.

bool eepromRead(uint16_t addr, uint8_t* dst, uint32_t len) {
  auxSelectLcaux();
  while (len > 0) {
    uint32_t n = len > READ_CHUNK ? READ_CHUNK : len;
    uint8_t a[2] = {static_cast<uint8_t>(addr >> 8), static_cast<uint8_t>(addr)};
    if (i2c_write_timeout_us(i2c0, ADDR_CARD_EEPROM, a, 2, true, XFER_TIMEOUT_US) != 2) {
      auxInvalidate();
      return false;
    }
    if (i2c_read_timeout_us(i2c0, ADDR_CARD_EEPROM, dst, n, false, XFER_TIMEOUT_US) !=
        static_cast<int>(n)) {
      auxInvalidate();
      return false;
    }
    addr = static_cast<uint16_t>(addr + n);
    dst += n;
    len -= n;
  }
  return true;
}

// Wait until the device ACKs its address again after a page write.
static bool waitWriteCycle() {
  absolute_time_t deadline = make_timeout_time_ms(WRITE_CYCLE_TIMEOUT_MS);
  do {
    uint8_t dummy;
    int r = i2c_read_timeout_us(i2c0, ADDR_CARD_EEPROM, &dummy, 1, false, 2000);
    if (r == 1) return true;
    sleep_us(500);
  } while (!time_reached(deadline));
  return false;
}

bool eepromWrite(uint16_t addr, const uint8_t* src, uint32_t len,
                 EepromProgressFn cb, void* user) {
  auxSelectLcaux();
  const uint32_t total = len;
  uint32_t done = 0;
  while (len > 0) {
    uint32_t room = PAGE_SIZE - (addr % PAGE_SIZE);
    uint32_t n = len < room ? len : room;
    uint8_t frame[2 + PAGE_SIZE];
    frame[0] = static_cast<uint8_t>(addr >> 8);
    frame[1] = static_cast<uint8_t>(addr);
    for (uint32_t i = 0; i < n; ++i) frame[2 + i] = src[i];
    if (i2c_write_timeout_us(i2c0, ADDR_CARD_EEPROM, frame, 2 + n, false, XFER_TIMEOUT_US) !=
        static_cast<int>(2 + n)) {
      printf("[eeprom] write failed at 0x%04x\n", addr);
      auxInvalidate();
      return false;
    }
    if (!waitWriteCycle()) {
      printf("[eeprom] write cycle timeout at 0x%04x\n", addr);
      auxInvalidate();
      return false;
    }
    addr = static_cast<uint16_t>(addr + n);
    src += n;
    len -= n;
    done += n;
    if (cb) cb(static_cast<int>(done * 100 / total), user);
  }
  return true;
}

ProfileError eepromReadProfile(uint8_t* buf, uint32_t* frameLen,
                               CardProfile* out, uint32_t* cborLen) {
  *frameLen = 0;
  // Header + a few bytes: enough for profileParseFrame() to classify an
  // empty or oversized image without reading the whole chip.
  if (!eepromRead(0, buf, 8)) return ProfileError::BAD_LENGTH;
  *frameLen = 8;
  uint32_t total = profileFrameLength(buf);
  if (total == 0) return profileParseFrame(buf, 8, out, cborLen);
  if (total > 8) {
    if (!eepromRead(8, buf + 8, total - 8)) return ProfileError::BAD_LENGTH;
  }
  *frameLen = total;
  return profileParseFrame(buf, total, out, cborLen);
}

}  // namespace wcb
