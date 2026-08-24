#include "isp_tiny85.hpp"

#include <cstdio>
#include <initializer_list>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "board_pins.hpp"
#include "card_io.hpp"

namespace wcb {

// SCK half period. 5 us -> ~100 kHz, safe for any ATtiny85 clock fuse
// (ISP needs SCK < f_cpu / 4; the factory 1 MHz allows up to 250 kHz).
static constexpr uint32_t SCK_HALF_US = 5;
static constexpr uint32_t TWD_FLASH_MS = 5;   // tWD_FLASH 4.5 ms
static constexpr uint32_t TWD_ERASE_MS = 11;  // tWD_ERASE 9.0 ms (t85) / 10.5 ms (m32u4)
static constexpr int PE_RETRIES = 5;

static IspPins sPins = {0, 0, 0};
static const AvrDevice* sDev = nullptr;

//-----------------------------------------------------------------------------
// Bit level
//-----------------------------------------------------------------------------

static uint8_t xferByte(uint8_t out) {
  uint8_t in = 0;
  for (int b = 7; b >= 0; --b) {
    gpio_put(sPins.mosi, (out >> b) & 1);
    busy_wait_us(SCK_HALF_US);
    gpio_put(sPins.sck, 1);
    busy_wait_us(SCK_HALF_US);
    in = static_cast<uint8_t>((in << 1) | (gpio_get(sPins.miso) ? 1 : 0));
    gpio_put(sPins.sck, 0);
  }
  return in;
}

// 4-byte instruction; returns the 4 bytes clocked back.
static void cmd4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t out[4]) {
  out[0] = xferByte(a);
  out[1] = xferByte(b);
  out[2] = xferByte(c);
  out[3] = xferByte(d);
}

static uint8_t cmd4r(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  uint8_t r[4];
  cmd4(a, b, c, d, r);
  return r[3];
}

static bool pollReady(uint32_t timeoutMs) {
  absolute_time_t deadline = make_timeout_time_ms(timeoutMs);
  while (cmd4r(0xF0, 0x00, 0x00, 0x00) & 1) {
    if (time_reached(deadline)) return false;
  }
  return true;
}

//-----------------------------------------------------------------------------
// Session
//-----------------------------------------------------------------------------

static void pinsProgramming() {
  gpio_init(sPins.sck);
  gpio_disable_pulls(sPins.sck);
  gpio_put(sPins.sck, 0);
  gpio_set_dir(sPins.sck, GPIO_OUT);

  gpio_init(sPins.mosi);
  gpio_disable_pulls(sPins.mosi);
  gpio_put(sPins.mosi, 0);
  gpio_set_dir(sPins.mosi, GPIO_OUT);

  gpio_init(sPins.miso);
  gpio_disable_pulls(sPins.miso);
  gpio_set_dir(sPins.miso, GPIO_IN);
}

static void pinsRelease() {
  for (uint pin : {sPins.sck, sPins.mosi, sPins.miso}) {
    gpio_init(pin);
    gpio_disable_pulls(pin);
    gpio_set_dir(pin, GPIO_IN);
  }
}

bool ispBegin(const IspPins& pins, const AvrDevice* dev) {
  sPins = pins;
  sDev = dev;
  if (!sDev) return false;
  cardKeysRelease();
  pinsProgramming();

  for (int attempt = 0; attempt < PE_RETRIES; ++attempt) {
    // RESET low with SCK low, then >= 20 ms before the first instruction.
    cardResetRelease();
    busy_wait_us(1000);
    cardResetAssert();
    sleep_ms(20);

    uint8_t r[4];
    cmd4(0xAC, 0x53, 0x00, 0x00, r);
    if (r[2] == 0x53) {
      printf("[isp] programming enabled (attempt %d)\n", attempt + 1);
      return true;
    }
    printf("[isp] programming enable failed (echo 0x%02x), retrying\n", r[2]);
  }
  pinsRelease();
  cardResetRelease();
  return false;
}

void ispEnd() {
  pinsRelease();
  cardResetRelease();
}

void ispReadDevice(IspDeviceInfo* info) {
  for (uint8_t i = 0; i < 3; ++i) {
    info->signature[i] = cmd4r(0x30, 0x00, i, 0x00);
  }
  info->fuseLow = cmd4r(0x50, 0x00, 0x00, 0x00);
  info->fuseHigh = cmd4r(0x58, 0x08, 0x00, 0x00);
  info->fuseExt = cmd4r(0x50, 0x08, 0x00, 0x00);
  info->lock = cmd4r(0x58, 0x00, 0x00, 0x00);
}

bool ispSignatureMatches(const IspDeviceInfo& info) {
  return sDev && info.signature[0] == sDev->signature[0] &&
         info.signature[1] == sDev->signature[1] &&
         info.signature[2] == sDev->signature[2];
}

bool ispChipErase() {
  cmd4r(0xAC, 0x80, 0x00, 0x00);
  sleep_ms(TWD_ERASE_MS);
  return pollReady(100);
}

static bool pageIsBlank(const uint8_t* p, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    if (p[i] != 0xFF) return false;
  }
  return true;
}

bool ispWriteFlash(const uint8_t* img, uint32_t len, IspProgressFn cb,
                   void* user) {
  if (!sDev || len > sDev->flashSize) return false;
  const uint32_t pageBytes = sDev->pageBytes;
  uint32_t written = 0;
  for (uint32_t base = 0; base < len; base += pageBytes) {
    uint32_t n = len - base;
    if (n > pageBytes) n = pageBytes;
    if (!pageIsBlank(img + base, n)) {
      // Load the page buffer (word address within page = byte/2).
      for (uint32_t i = 0; i < pageBytes; i += 2) {
        uint8_t lo = (i < n) ? img[base + i] : 0xFF;
        uint8_t hi = (i + 1 < n) ? img[base + i + 1] : 0xFF;
        uint8_t w = static_cast<uint8_t>(i >> 1);
        cmd4r(0x40, 0x00, w, lo);
        cmd4r(0x48, 0x00, w, hi);
      }
      uint16_t wordAddr = static_cast<uint16_t>(base >> 1);
      cmd4r(0x4C, static_cast<uint8_t>(wordAddr >> 8),
            static_cast<uint8_t>(wordAddr & 0xFF), 0x00);
      sleep_ms(TWD_FLASH_MS);
      if (!pollReady(50)) return false;
      written++;
    }
    if (cb) cb(static_cast<int>((base + n) * 100 / len), user);
  }
  printf("[isp] %lu pages written (%s)\n", static_cast<unsigned long>(written), sDev->name);
  return true;
}

bool ispVerifyFlash(const uint8_t* img, uint32_t len, uint32_t* firstMismatch,
                    IspProgressFn cb, void* user) {
  for (uint32_t addr = 0; addr < len; addr += 2) {
    uint16_t w = static_cast<uint16_t>(addr >> 1);
    uint8_t aH = static_cast<uint8_t>(w >> 8), aL = static_cast<uint8_t>(w);
    uint8_t lo = cmd4r(0x20, aH, aL, 0x00);
    if (lo != img[addr]) {
      *firstMismatch = addr;
      return false;
    }
    if (addr + 1 < len) {
      uint8_t hi = cmd4r(0x28, aH, aL, 0x00);
      if (hi != img[addr + 1]) {
        *firstMismatch = addr + 1;
        return false;
      }
    }
    if (cb && (addr & 0xFF) == 0) cb(static_cast<int>(addr * 100 / len), user);
  }
  if (cb) cb(100, user);
  return true;
}

}  // namespace wcb
