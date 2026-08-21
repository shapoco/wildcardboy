// WildCardBoy pretest firmware: TJP (TinyJoypad) logic card bring-up.
//
//   1. Detect the card by probing its ID EEPROM over LCAUX I2C
//      (3 consecutive ACKs, 200 ms apart, debounce the hot-plug).
//   2. Set up the WildCardBus for the TJP card and an LcdTap instance
//      (SSD1306 / I2C slave @0x3C on LCIO2/3).
//   3. Reset the ATtiny85 via LCIO13.
//   4. Loop: host keypad -> card key lines, LcdTap framebuffer -> host LCD.
//
// Debug output: USB CDC. See ../SPEC.md for the pin map.

#include <cstdio>
#include <cstdlib>

#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include <lcdtap/lcdtap.hpp>
#include <lcdtap/pico2/i2c_slave.hpp>

#include "aux_i2c.hpp"
#include "board_pins.hpp"
#include "card_io.hpp"
#include "ili9488.hpp"
#include "lcd_pump.hpp"
#include "text_draw.hpp"

using namespace wcb;

//-----------------------------------------------------------------------------
// Tunables
//-----------------------------------------------------------------------------
static constexpr uint32_t CARD_PROBE_INTERVAL_MS = 200;
// The card counts as present only after this many consecutive EEPROM ACKs
// (debounces the hot-plug contact chatter).
static constexpr uint32_t CARD_PROBE_SUCCESS_COUNT = 3;
static constexpr uint32_t KEY_POLL_INTERVAL_MS = 5;
static constexpr uint32_t STATS_INTERVAL_MS = 1000;
static constexpr uint32_t PUMP_MAX_LINES_PER_CALL = 48;
static constexpr uint32_t I2C_RING_WORDS = 1024;  // power of 2, 4 KB
static constexpr float LCD_PIO_CLKDIV = 1.0f;     // 40 ns/byte at 150 MHz

//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------
static Ili9488 gLcd;
static LcdPump gPump;
static lcdtap::LcdTap* gTap = nullptr;
static lcdtap::pico2::I2cSlaveState gI2c;
static uint32_t gI2cRingBuf[I2C_RING_WORDS];

static inline uint32_t nowMs() { return to_ms_since_boot(get_absolute_time()); }

static void lcdtapLog(void*, const char* msg) { printf("[lcdtap] %s\n", msg); }

//-----------------------------------------------------------------------------
// Card bring-up
//-----------------------------------------------------------------------------
static void startTjpCard() {
  // WildCardBus: key lines / reset released.
  cardIoInit();

  // LcdTap: TinyJoypad preset (SSD1306, I2C, 128x64), output raster equal
  // to the host LCD so one fillScanline() call is one LCD line.
  lcdtap::LcdTapConfig cfg;
  lcdtap::getPresetConfig(lcdtap::ConfigPreset::TINYJOYPAD, &cfg);
  cfg.i2cSlaveAddr = ADDR_LC_LCD;
  cfg.dviWidth = LCD_WIDTH;
  cfg.dviHeight = LCD_HEIGHT;
  cfg.scaleMode = lcdtap::ScaleMode::FIT;

  lcdtap::HostInterface host;
  host.alloc = malloc;
  host.free = free;
  host.log = lcdtapLog;
  host.userData = nullptr;

  gTap = new lcdtap::LcdTap(cfg, host);
  if (gTap->getStatus() != lcdtap::Status::OK) {
    panic("LcdTap init failed (%d)", static_cast<int>(gTap->getStatus()));
  }

  // I2C1 hardware slave on LCIO2/3, feeding the ring buffer from IRQ.
  lcdtap::pico2::I2cSlaveConfig i2cCfg;
  i2cCfg.i2c = i2c1;
  i2cCfg.pinSda = PIN_LC_LCD_SDA;
  i2cCfg.pinScl = PIN_LC_LCD_SCL;
  i2cCfg.slaveAddr = cfg.i2cSlaveAddr;
  gI2c.inst = nullptr;
  gI2c.dropWords = 0;
  gI2c.hwOverflowCount = 0;
  gI2c.backlogMaxWords = 0;
  lcdtap::pico2::i2cSlaveInit(&gI2c, i2cCfg, gI2cRingBuf, I2C_RING_WORDS);
  gI2c.inst = gTap;

  gPump.init(gTap, &gLcd);

  // Reset the ATtiny85; mirror it into LcdTap so its controller state
  // matches the fresh SSD1306 init sequence that follows.
  printf("resetting ATtiny85...\n");
  gTap->inputReset(true);
  cardResetPulse(10);
  gTap->inputReset(false);
  printf("TJP card running\n");
}

//-----------------------------------------------------------------------------
// Statistics
//-----------------------------------------------------------------------------
static void printStats(uint16_t keys, bool keysOk, uint32_t loopsPerSec) {
  printf("[stat] keys=0x%04x%s loops=%lu", keys, keysOk ? "" : "(err)",
         static_cast<unsigned long>(loopsPerSec));
  if (gTap) {
    printf(" rxCmd=%lu rxData=%lu unk=%lu(0x%02x) hwrst=%lu drop=%lu ovf=%lu "
           "backlog=%lu | lines=%lu full=%lu groups=%lu",
           static_cast<unsigned long>(gTap->getRxCmdBytes()),
           static_cast<unsigned long>(gTap->getRxDataBytes()),
           static_cast<unsigned long>(gTap->getUnknownCmdCount()),
           gTap->getLastUnknownCmd(),
           static_cast<unsigned long>(gTap->getHwResetCount()),
           static_cast<unsigned long>(gI2c.dropWords),
           static_cast<unsigned long>(gI2c.hwOverflowCount),
           static_cast<unsigned long>(gI2c.backlogMaxWords),
           static_cast<unsigned long>(gPump.linesSent()),
           static_cast<unsigned long>(gPump.fullRepaints()),
           static_cast<unsigned long>(gPump.dirtyGroups()));
  }
  printf("\n");
}

//-----------------------------------------------------------------------------
// main
//-----------------------------------------------------------------------------
int main() {
  stdio_init_all();
  printf("\n=== WildCardBoy pretest (TJP card) ===\n");

  // Host LCD.
  Ili9488::Pins lcdPins = {PIN_LCD_D0, PIN_LCD_DC, PIN_LCD_WR,
                           PIN_LCD_RD, PIN_LCD_CS, PIN_LCD_RST};
  if (!gLcd.init(pio0, 0, lcdPins, LCD_PIO_CLKDIV)) {
    panic("LCD PIO init failed");
  }
  {
    // Colour bars + timing of a full-screen fill (bus throughput check).
    static const uint16_t bars[] = {COLOR_RED,   COLOR_GREEN, COLOR_BLUE,
                                    COLOR_YELLOW, COLOR_WHITE, COLOR_GRAY};
    for (int i = 0; i < 6; ++i) gLcd.fillRect(i * 80, 0, 80, 100, bars[i]);
    uint64_t t0 = time_us_64();
    gLcd.fillRect(0, 100, LCD_WIDTH, LCD_HEIGHT - 100, COLOR_BLACK);
    uint64_t t1 = time_us_64();
    printf("LCD ready: %ux%u, %d px fill in %llu us\n", LCD_WIDTH, LCD_HEIGHT,
           LCD_WIDTH * (LCD_HEIGHT - 100),
           static_cast<unsigned long long>(t1 - t0));
  }
  drawTextCentered(gLcd, 120, "WildCardBoy pretest", COLOR_WHITE, COLOR_BLACK,
                   3);
  drawTextCentered(gLcd, 300, "note: on-board USER button = LCD D7",
                   COLOR_GRAY, COLOR_BLACK, 1);

  // AUX I2C (keypad + card EEPROM).
  auxI2cInit();

  bool cardPresent = false;
  bool noCardShown = false;
  uint32_t probeOkCount = 0;
  uint32_t lastProbeMs = 0;
  uint32_t lastKeyPollMs = 0;
  uint32_t lastStatsMs = nowMs();
  uint32_t loops = 0;
  uint16_t keys = 0;
  bool keysOk = false;

  while (true) {
    const uint32_t now = nowMs();

    //--- card detection -------------------------------------------------
    // Non-blocking: one probe per interval; the card counts as present
    // only after CARD_PROBE_SUCCESS_COUNT consecutive ACKs so hot-plug
    // contact chatter cannot trigger a premature bring-up.
    if (!cardPresent) {
      if (now - lastProbeMs >= CARD_PROBE_INTERVAL_MS || lastProbeMs == 0) {
        lastProbeMs = now;
        if (cardEepromProbe()) {
          probeOkCount++;
          printf("card EEPROM (0x%02x) ACK %lu/%lu\n", ADDR_CARD_EEPROM,
                 static_cast<unsigned long>(probeOkCount),
                 static_cast<unsigned long>(CARD_PROBE_SUCCESS_COUNT));
          if (probeOkCount >= CARD_PROBE_SUCCESS_COUNT) {
            printf("TJP card detected\n");
            cardPresent = true;
            startTjpCard();
          }
        } else {
          if (probeOkCount > 0) {
            printf("card EEPROM (0x%02x) NAK; ACK streak reset\n",
                   ADDR_CARD_EEPROM);
          }
          probeOkCount = 0;
          if (!noCardShown) {
            printf("no card (EEPROM 0x%02x NAK); retrying every %lu ms\n",
                   ADDR_CARD_EEPROM,
                   static_cast<unsigned long>(CARD_PROBE_INTERVAL_MS));
            drawTextCentered(gLcd, 200, "No Logic Card", COLOR_YELLOW,
                             COLOR_BLACK, 2);
            noCardShown = true;
          }
        }
      }
    }

    //--- host keypad -> card ---------------------------------------------
    if (now - lastKeyPollMs >= KEY_POLL_INTERVAL_MS) {
      lastKeyPollMs = now;
      uint16_t k;
      keysOk = hostKeysRead(&k);
      if (keysOk && k != keys) {
        printf("keys: 0x%04x\n", k);
      }
      keys = keysOk ? k : 0;
      if (cardPresent) cardKeysSet(keys);
    }

    //--- card LCD stream -> host LCD ---------------------------------------
    if (cardPresent) {
      lcdtap::pico2::i2cSlaveProcess(&gI2c);
      gTap->tick(now);
      gPump.process(PUMP_MAX_LINES_PER_CALL);
    }

    //--- periodic stats -----------------------------------------------------
    loops++;
    if (now - lastStatsMs >= STATS_INTERVAL_MS) {
      lastStatsMs = now;
      printStats(keys, keysOk, loops);
      loops = 0;
    }
  }
  return 0;
}
