#include "sd_spi.hpp"

#include <cstdio>
#include <initializer_list>

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "board_pins.hpp"

namespace wcb {

static constexpr uint32_t SPI_BAUD_INIT = 400 * 1000;
static constexpr uint32_t SPI_BAUD_RUN = 25 * 1000 * 1000;
static constexpr uint32_t INIT_TIMEOUT_MS = 1000;
static constexpr uint32_t READ_TOKEN_TIMEOUT_MS = 200;
static constexpr uint32_t BUSY_TIMEOUT_MS = 500;

// SD commands (SPI mode).
static constexpr uint8_t CMD0 = 0;    // GO_IDLE_STATE
static constexpr uint8_t CMD8 = 8;    // SEND_IF_COND
static constexpr uint8_t CMD16 = 16;  // SET_BLOCKLEN
static constexpr uint8_t CMD17 = 17;  // READ_SINGLE_BLOCK
static constexpr uint8_t CMD55 = 55;  // APP_CMD
static constexpr uint8_t CMD58 = 58;  // READ_OCR
static constexpr uint8_t ACMD41 = 41;  // SD_SEND_OP_COND

static constexpr uint8_t R1_IDLE = 0x01;
static constexpr uint8_t R1_ILLEGAL_CMD = 0x04;
static constexpr uint8_t TOKEN_START_BLOCK = 0xFE;

static SdCardType sType = SdCardType::NONE;
static bool sReady = false;

//-----------------------------------------------------------------------------
// Low level
//-----------------------------------------------------------------------------

static inline void csLow() { gpio_put(PIN_HTF_CS, 0); }
static inline void csHigh() { gpio_put(PIN_HTF_CS, 1); }

static inline uint8_t xfer(uint8_t out) {
  uint8_t in;
  spi_write_read_blocking(spi0, &out, &in, 1);
  return in;
}

static inline void clocks(uint n) {
  for (uint i = 0; i < n; ++i) xfer(0xFF);
}

static bool waitReady(uint32_t timeoutMs) {
  absolute_time_t deadline = make_timeout_time_ms(timeoutMs);
  while (xfer(0xFF) != 0xFF) {
    if (time_reached(deadline)) return false;
  }
  return true;
}

// Send a command; returns R1 (0xFF on no response). CS must be low.
static uint8_t sendCmd(uint8_t cmd, uint32_t arg) {
  if (cmd != CMD0 && !waitReady(BUSY_TIMEOUT_MS)) return 0xFF;
  uint8_t crc = 0x01;
  if (cmd == CMD0) crc = 0x95;
  if (cmd == CMD8) crc = 0x87;
  uint8_t frame[6] = {static_cast<uint8_t>(0x40 | cmd),
                      static_cast<uint8_t>(arg >> 24),
                      static_cast<uint8_t>(arg >> 16),
                      static_cast<uint8_t>(arg >> 8),
                      static_cast<uint8_t>(arg),
                      crc};
  spi_write_blocking(spi0, frame, sizeof(frame));
  for (int i = 0; i < 10; ++i) {
    uint8_t r = xfer(0xFF);
    if (!(r & 0x80)) return r;
  }
  return 0xFF;
}

static uint8_t sendAcmd(uint8_t cmd, uint32_t arg) {
  uint8_t r = sendCmd(CMD55, 0);
  if (r > R1_IDLE) return r;
  return sendCmd(cmd, arg);
}

static void release() {
  csHigh();
  xfer(0xFF);  // 8 clocks after deselect so the card releases MISO
}

//-----------------------------------------------------------------------------
// API
//-----------------------------------------------------------------------------

static bool sHostOwns = false;

void sdBusAcquire() {
  gpio_put(PIN_LCTF_ENAX, 1);  // TF card belongs to the host controller

  gpio_init(PIN_HTF_CS);
  gpio_set_dir(PIN_HTF_CS, GPIO_OUT);
  gpio_put(PIN_HTF_CS, 1);

  spi_init(spi0, SPI_BAUD_INIT);
  spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  gpio_set_function(PIN_HTF_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_HTF_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_HTF_MOSI, GPIO_FUNC_SPI);
  gpio_pull_up(PIN_HTF_MISO);  // idle high when no card is inserted

  sType = SdCardType::NONE;
  sReady = false;
  sHostOwns = true;
}

void sdBusRelease() {
  sReady = false;
  sType = SdCardType::NONE;
  spi_deinit(spi0);
  for (uint pin : {PIN_HTF_MISO, PIN_HTF_CS, PIN_HTF_SCK, PIN_HTF_MOSI}) {
    gpio_init(pin);  // SIO input = Hi-Z
    gpio_disable_pulls(pin);
  }
  gpio_put(PIN_LCTF_ENAX, 0);  // TF card belongs to the logic card
  sHostOwns = false;
}

bool sdBusOwnedByHost() { return sHostOwns; }

void sdBusInit() {
  gpio_init(PIN_LCTF_ENAX);
  gpio_set_dir(PIN_LCTF_ENAX, GPIO_OUT);
  gpio_put(PIN_LCTF_ENAX, 1);
  sdBusAcquire();
}

bool sdInit() {
  sReady = false;
  sType = SdCardType::NONE;
  spi_set_baudrate(spi0, SPI_BAUD_INIT);

  // >= 74 clocks with CS high to enter SPI mode.
  csHigh();
  clocks(10);
  csLow();

  // CMD0: go idle.
  uint8_t r = 0xFF;
  for (int i = 0; i < 5 && r != R1_IDLE; ++i) r = sendCmd(CMD0, 0);
  if (r != R1_IDLE) {
    release();
    printf("[sd] no response to CMD0 (0x%02x)\n", r);
    return false;
  }

  // CMD8: voltage check / v2 detection.
  bool v2 = false;
  r = sendCmd(CMD8, 0x1AA);
  if (r == R1_IDLE) {
    uint8_t r7[4];
    for (auto& b : r7) b = xfer(0xFF);
    if (r7[2] != 0x01 || r7[3] != 0xAA) {
      release();
      printf("[sd] CMD8 pattern mismatch\n");
      return false;
    }
    v2 = true;
  } else if (!(r & R1_ILLEGAL_CMD)) {
    release();
    printf("[sd] CMD8 failed (0x%02x)\n", r);
    return false;
  }

  // ACMD41 until the card leaves idle.
  absolute_time_t deadline = make_timeout_time_ms(INIT_TIMEOUT_MS);
  do {
    r = sendAcmd(ACMD41, v2 ? 0x40000000u : 0u);
    if (r == 0) break;
    if (r > R1_IDLE) {
      release();
      printf("[sd] ACMD41 failed (0x%02x)\n", r);
      return false;
    }
    sleep_ms(2);
  } while (!time_reached(deadline));
  if (r != 0) {
    release();
    printf("[sd] ACMD41 timeout (MMC cards are not supported)\n");
    return false;
  }

  bool blockAddr = false;
  if (v2) {
    r = sendCmd(CMD58, 0);
    if (r != 0) {
      release();
      printf("[sd] CMD58 failed (0x%02x)\n", r);
      return false;
    }
    uint8_t ocr[4];
    for (auto& b : ocr) b = xfer(0xFF);
    blockAddr = (ocr[0] & 0x40) != 0;  // CCS
    sType = blockAddr ? SdCardType::SD_V2_HC : SdCardType::SD_V2_SC;
  } else {
    sType = SdCardType::SD_V1;
  }

  if (!blockAddr) {
    r = sendCmd(CMD16, 512);
    if (r != 0) {
      release();
      printf("[sd] CMD16 failed (0x%02x)\n", r);
      return false;
    }
  }

  release();
  spi_set_baudrate(spi0, SPI_BAUD_RUN);
  sReady = true;
  printf("[sd] card ready: %s\n", sdCardTypeName());
  return true;
}

bool sdIsReady() { return sReady; }
void sdMarkNotReady() { sReady = false; }
SdCardType sdCardType() { return sType; }

const char* sdCardTypeName() {
  switch (sType) {
    case SdCardType::SD_V1: return "SD v1";
    case SdCardType::SD_V2_SC: return "SD v2 (SDSC)";
    case SdCardType::SD_V2_HC: return "SD v2 (SDHC/SDXC)";
    default: return "none";
  }
}

bool sdReadBlocks(uint32_t lba, uint8_t* buf, uint32_t count) {
  if (!sReady) return false;
  const bool blockAddr = (sType == SdCardType::SD_V2_HC);

  csLow();
  for (uint32_t i = 0; i < count; ++i, ++lba, buf += 512) {
    uint32_t arg = blockAddr ? lba : lba * 512u;
    if (sendCmd(CMD17, arg) != 0) {
      release();
      sReady = false;
      return false;
    }
    // Wait for the data token.
    absolute_time_t deadline = make_timeout_time_ms(READ_TOKEN_TIMEOUT_MS);
    uint8_t tok;
    do {
      tok = xfer(0xFF);
    } while (tok == 0xFF && !time_reached(deadline));
    if (tok != TOKEN_START_BLOCK) {
      release();
      sReady = false;
      return false;
    }
    spi_read_blocking(spi0, 0xFF, buf, 512);
    xfer(0xFF);  // CRC
    xfer(0xFF);
  }
  release();
  return true;
}

}  // namespace wcb
