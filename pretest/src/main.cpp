// WildCardBoy pretest firmware: logic card bring-up (TJP card).
//
//   1. Detect the card: its profile EEPROM must ACK 3 times (200 ms apart),
//      then the profile is read and validated (length / CRC32 / CBOR).
//   2. Configure the WildCardBus from the profile (key lines, RESET, LcdTap
//      preset + overrides, ISP pins, key map) and start LcdTap as an I2C
//      slave on LCIO2/3.
//   3. Reset the card MCU.
//   4. Loop: host keypad -> card key lines, LcdTap framebuffer -> host LCD.
//   HOME opens the system menu: Launch / Apps (program the card MCU from
//   the TF card) / Profile (write a profile .hex into the card EEPROM and
//   re-detect the card).
//
// Debug output: USB CDC. See ../spec/ for the pin map.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include <lcdtap/lcdtap.hpp>
#include <lcdtap/pico2/i2c_slave.hpp>

#include "app_image.hpp"
#include "aux_i2c.hpp"
#include "board_pins.hpp"
#include "card_eeprom.hpp"
#include "card_io.hpp"
#include "card_profile.hpp"
#include "ili9488.hpp"
#include "isp_tiny85.hpp"
#include "lcd_pump.hpp"
#include "sd_spi.hpp"
#include "text_draw.hpp"
#include "ui_menu.hpp"

using namespace wcb;

//-----------------------------------------------------------------------------
// Tunables
//-----------------------------------------------------------------------------
static constexpr uint32_t CARD_PROBE_INTERVAL_MS = 200;
// The card counts as present only after this many consecutive EEPROM ACKs
// (debounces the hot-plug contact chatter).
static constexpr uint32_t CARD_PROBE_SUCCESS_COUNT = 3;
static constexpr uint32_t CARD_INVALID_POLL_MS = 500;
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
static UiMenu gUi;
static lcdtap::LcdTap* gTap = nullptr;
static lcdtap::pico2::I2cSlaveState gI2c;
static lcdtap::pico2::I2cSlaveConfig gI2cCfg;
static uint32_t gI2cRingBuf[I2C_RING_WORDS];

static CardState gCardState = CardState::NONE;
static CardProfile gProfile;                    // profile of the running card
static ProfileError gProfileError = ProfileError::EMPTY;
static char gAppsDir[64];
static uint8_t gFrameBuf[PROFILE_FRAME_MAX];    // EEPROM read / verify buffer

static uint8_t gStageBuf[PROFILE_FRAME_MAX];    // profile staged for writing
static uint32_t gStageLen = 0;
static CardProfile gStageProfile;

static uint8_t gAppImage[TINY85_FLASH_SIZE];

static inline uint32_t nowMs() { return to_ms_since_boot(get_absolute_time()); }

static void lcdtapLog(void*, const char* msg) { printf("[lcdtap] %s\n", msg); }

//-----------------------------------------------------------------------------
// Card bus (I2C1 slave for the LCD stream)
//-----------------------------------------------------------------------------

static void cardBusAttach() {
  gI2c.inst = nullptr;
  lcdtap::pico2::i2cSlaveInit(&gI2c, gI2cCfg, gI2cRingBuf, I2C_RING_WORDS);
  gI2c.inst = gTap;
}

static void cardBusDetach() {
  lcdtap::pico2::i2cSlaveDeinit(&gI2c);
  gI2c.inst = nullptr;
}

//-----------------------------------------------------------------------------
// Card start / stop
//-----------------------------------------------------------------------------

static void cardStart(const CardProfile& p) {
  cardIoConfigure(p);
  snprintf(gAppsDir, sizeof(gAppsDir), "%s/%s/Apps", CARDS_DIR, p.id);

  // LcdTap: preset + profile overrides; output raster = host LCD so one
  // fillScanline() call is one LCD line.
  lcdtap::LcdTapConfig cfg;
  profileBuildLcdTapConfig(p, &cfg);
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
  gI2cCfg.i2c = i2c1;
  gI2cCfg.pinSda = PIN_LC_LCD_SDA;
  gI2cCfg.pinScl = PIN_LC_LCD_SCL;
  gI2cCfg.slaveAddr = cfg.i2cSlaveAddr;
  gI2c.dropWords = 0;
  gI2c.hwOverflowCount = 0;
  gI2c.backlogMaxWords = 0;
  cardBusAttach();

  gPump.init(gTap, &gLcd);

  printf("card started: id=%s name=\"%s\" preset=%s ctrl=%s bus=%s addr=0x%02x %ux%u\n",
         p.id, p.name, p.lcdtapPreset,
         lcdtap::CONTROLLER_NAMES[static_cast<int>(cfg.controllerFamily)],
         lcdtap::BUS_NAMES[static_cast<int>(cfg.busInterface)], cfg.i2cSlaveAddr,
         cfg.buffWidth, cfg.buffHeight);

  // Reset the card MCU; mirror it into LcdTap so its controller state
  // matches the init sequence that follows.
  gTap->inputReset(true);
  cardResetPulse(10);
  gTap->inputReset(false);
  gCardState = CardState::READY;
}

static void cardStop() {
  if (gTap) {
    cardBusDetach();
    delete gTap;
    gTap = nullptr;
  }
  cardIoRelease();
  gAppsDir[0] = '\0';
  gCardState = CardState::NONE;
}

//-----------------------------------------------------------------------------
// Idle screens (outside the menu)
//-----------------------------------------------------------------------------

static void drawIdleScreen() {
  gLcd.clear(COLOR_BLACK);
  drawTextCentered(gLcd, 120, "WildCardBoy pretest", COLOR_WHITE, COLOR_BLACK, 3);
  if (gCardState == CardState::INVALID) {
    char line[48];
    snprintf(line, sizeof(line), "Profile: %s", profileErrorText(gProfileError));
    drawTextCentered(gLcd, 200, line, COLOR_RED, COLOR_BLACK, 2);
    drawTextCentered(gLcd, 240, "HOME > Profile to write one", COLOR_GRAY, COLOR_BLACK, 1);
  } else {
    drawTextCentered(gLcd, 200, "No Logic Card", COLOR_YELLOW, COLOR_BLACK, 2);
  }
  drawTextCentered(gLcd, 300, "HOME: system menu", COLOR_GRAY, COLOR_BLACK, 1);
}

//-----------------------------------------------------------------------------
// UI hooks
//-----------------------------------------------------------------------------

static void ispProgressWrite(int pct, void*) { gUi.progress("Write", pct); }
static void ispProgressVerify(int pct, void*) { gUi.progress("Verify", pct); }
static void eepromProgressWrite(int pct, void*) { gUi.progress("Write", pct); }

static CardState hookCardState(void*) { return gCardState; }
static const char* hookAppsDir(void*) { return gAppsDir[0] ? gAppsDir : nullptr; }

static const char* hookProgramApp(const char* path, void*) {
  if (gCardState != CardState::READY) return "No Logic Card";
  IspPins pins;
  if (!cardIspPins(&pins)) return "ISP not available";

  printf("[prog] loading %s\n", path);
  gUi.progress("Load", 0);
  uint32_t len = 0;
  LoadResult lr = appImageLoad(path, gAppImage, sizeof(gAppImage), &len);
  if (lr != LoadResult::OK) {
    printf("[prog] load failed: %s\n", loadResultName(lr));
    return loadResultName(lr);
  }
  gUi.progress("Load", 100);
  printf("[prog] image: %lu bytes\n", static_cast<unsigned long>(len));

  // Quiesce the card bus: no key drive, no I2C1 slave on LCIO2/3.
  cardKeysRelease();
  cardBusDetach();
  gTap->inputReset(true);

  const char* err = nullptr;
  uint64_t t0 = time_us_64();
  do {
    if (!ispBegin(pins)) {
      err = "ISP enable failed";
      break;
    }
    IspDeviceInfo dev;
    ispReadDevice(&dev);
    printf("[prog] signature %02x %02x %02x  fuses L=%02x H=%02x E=%02x lock=%02x\n",
           dev.signature[0], dev.signature[1], dev.signature[2], dev.fuseLow,
           dev.fuseHigh, dev.fuseExt, dev.lock);
    if (!ispIsTiny85(dev)) {
      err = "Not an ATtiny85";
      break;
    }
    gUi.progress("Erase", 0);
    if (!ispChipErase()) {
      err = "Chip erase failed";
      break;
    }
    gUi.progress("Erase", 100);
    gUi.progress("Write", 0);
    if (!ispWriteFlash(gAppImage, len, ispProgressWrite, nullptr)) {
      err = "Flash write failed";
      break;
    }
    gUi.progress("Verify", 0);
    uint32_t bad = 0;
    if (!ispVerifyFlash(gAppImage, len, &bad, ispProgressVerify, nullptr)) {
      printf("[prog] verify mismatch at 0x%04lx\n", static_cast<unsigned long>(bad));
      err = "Verify failed";
      break;
    }
  } while (false);
  uint64_t t1 = time_us_64();

  // Back to game mode: data pins Hi-Z, RESET released (new program starts),
  // key lines re-armed, I2C1 slave listening again.
  ispEnd();
  cardIoConfigure(gProfile);
  cardBusAttach();
  gTap->inputReset(false);

  printf("[prog] %s (%llu ms)\n", err ? err : "done",
         static_cast<unsigned long long>((t1 - t0) / 1000));
  return err;
}

static const char* hookValidateProfile(const char* path, char* id, size_t idCap,
                                       char* name, size_t nameCap, void*) {
  printf("[profile] loading %s\n", path);
  gStageLen = 0;
  uint32_t imgLen = 0;
  LoadResult lr = ihexLoad(path, gStageBuf, sizeof(gStageBuf), &imgLen);
  if (lr == LoadResult::TOO_LARGE) return "Profile too large";
  if (lr != LoadResult::OK) return loadResultName(lr);

  uint32_t cborLen = 0;
  ProfileError pe = profileParseFrame(gStageBuf, imgLen, &gStageProfile, &cborLen);
  if (pe != ProfileError::OK) {
    printf("[profile] invalid: %s\n", profileErrorText(pe));
    return profileErrorText(pe);
  }
  gStageLen = 4 + cborLen + 4;
  snprintf(id, idCap, "%s", gStageProfile.id);
  snprintf(name, nameCap, "%s", gStageProfile.name);
  printf("[profile] valid: id=%s name=\"%s\" cbor=%lu bytes\n", gStageProfile.id,
         gStageProfile.name, static_cast<unsigned long>(cborLen));
  return nullptr;
}

static const char* hookWriteProfile(void*) {
  if (gStageLen == 0) return "Nothing to write";
  if (!cardEepromProbe()) return "Card EEPROM not responding";

  gUi.progress("Write", 0);
  uint64_t t0 = time_us_64();
  if (!eepromWrite(0, gStageBuf, gStageLen, eepromProgressWrite, nullptr)) {
    return "EEPROM write failed";
  }
  gUi.progress("Verify", 0);
  uint32_t readLen = 0, cborLen = 0;
  CardProfile check;
  ProfileError pe = eepromReadProfile(gFrameBuf, &readLen, &check, &cborLen);
  if (pe != ProfileError::OK) {
    printf("[profile] read-back failed: %s\n", profileErrorText(pe));
    return "Verify failed";
  }
  if (readLen != gStageLen || memcmp(gFrameBuf, gStageBuf, gStageLen) != 0) {
    printf("[profile] read-back differs\n");
    return "Verify mismatch";
  }
  gUi.progress("Verify", 100);
  printf("[profile] written %lu bytes (%llu ms); restarting card detection\n",
         static_cast<unsigned long>(gStageLen),
         static_cast<unsigned long long>((time_us_64() - t0) / 1000));

  // Tear the running card down; the main loop re-detects it from scratch.
  cardStop();
  return nullptr;
}

//-----------------------------------------------------------------------------
// Statistics
//-----------------------------------------------------------------------------
static void printStats(uint16_t keys, bool keysOk, uint32_t loopsPerSec) {
  printf("[stat] card=%s keys=0x%04x%s loops=%lu",
         gCardState == CardState::READY ? "ready" : gCardState == CardState::INVALID ? "invalid" : "none",
         keys, keysOk ? "" : "(err)", static_cast<unsigned long>(loopsPerSec));
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
  printf("\n=== WildCardBoy pretest ===\n");

  // TF card mux to the host, SPI0 parked (the card itself is probed when
  // the browser opens).
  sdBusInit();

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
  drawTextCentered(gLcd, 120, "WildCardBoy pretest", COLOR_WHITE, COLOR_BLACK, 3);
  drawTextCentered(gLcd, 300, "note: on-board USER button = LCD D7",
                   COLOR_GRAY, COLOR_BLACK, 1);

  // AUX I2C (keypad + card EEPROM).
  auxI2cInit();

  // System menu.
  UiHooks hooks;
  hooks.cardState = hookCardState;
  hooks.appsDir = hookAppsDir;
  hooks.programApp = hookProgramApp;
  hooks.validateProfile = hookValidateProfile;
  hooks.writeProfile = hookWriteProfile;
  hooks.user = nullptr;
  gUi.init(&gLcd, hooks);

  bool idleShown = false;
  uint32_t probeOkCount = 0;
  uint32_t lastProbeMs = 0;
  uint32_t lastKeyPollMs = 0;
  uint32_t lastStatsMs = nowMs();
  uint32_t loops = 0;
  uint16_t keys = 0;
  uint16_t prevKeys = 0;
  bool keysOk = false;

  while (true) {
    const uint32_t now = nowMs();
    const bool menuWasVisible = gUi.isVisible();
    const CardState stateAtTop = gCardState;

    //--- card detection -------------------------------------------------
    if (gCardState == CardState::NONE) {
      // One probe per interval; the card counts as present only after
      // CARD_PROBE_SUCCESS_COUNT consecutive ACKs, then its profile must
      // validate.
      if (now - lastProbeMs >= CARD_PROBE_INTERVAL_MS || lastProbeMs == 0) {
        lastProbeMs = now;
        if (cardEepromProbe()) {
          probeOkCount++;
          printf("card EEPROM (0x%02x) ACK %lu/%lu\n", ADDR_CARD_EEPROM,
                 static_cast<unsigned long>(probeOkCount),
                 static_cast<unsigned long>(CARD_PROBE_SUCCESS_COUNT));
          if (probeOkCount >= CARD_PROBE_SUCCESS_COUNT) {
            probeOkCount = 0;
            uint32_t frameLen = 0, cborLen = 0;
            gProfileError = eepromReadProfile(gFrameBuf, &frameLen, &gProfile, &cborLen);
            if (gProfileError == ProfileError::OK) {
              printf("card profile OK: id=%s name=\"%s\" preset=%s cbor=%lu bytes\n",
                     gProfile.id, gProfile.name, gProfile.lcdtapPreset,
                     static_cast<unsigned long>(cborLen));
              if (gUi.isVisible()) gUi.close();  // the pump takes the screen
              cardStart(gProfile);
            } else {
              printf("card profile invalid: %s (len field %lu)\n",
                     profileErrorText(gProfileError), static_cast<unsigned long>(cborLen));
              gCardState = CardState::INVALID;
              idleShown = false;
            }
          }
        } else {
          if (probeOkCount > 0) {
            printf("card EEPROM (0x%02x) NAK; ACK streak reset\n", ADDR_CARD_EEPROM);
          }
          probeOkCount = 0;
          if (!idleShown) {
            printf("no card (EEPROM 0x%02x NAK); retrying every %lu ms\n",
                   ADDR_CARD_EEPROM, static_cast<unsigned long>(CARD_PROBE_INTERVAL_MS));
          }
        }
      }
    } else if (gCardState == CardState::INVALID) {
      // Card present but unusable: wait for it to be pulled (NAK) or for a
      // profile to be written (the writer resets the state itself).
      if (now - lastProbeMs >= CARD_INVALID_POLL_MS) {
        lastProbeMs = now;
        if (!cardEepromProbe()) {
          printf("card removed\n");
          gCardState = CardState::NONE;
          idleShown = false;
        }
      }
    }

    if (!idleShown && gCardState != CardState::READY && !gUi.isVisible()) {
      drawIdleScreen();
      idleShown = true;
    }

    //--- host keypad -----------------------------------------------------
    if (now - lastKeyPollMs >= KEY_POLL_INTERVAL_MS) {
      lastKeyPollMs = now;
      uint16_t k;
      keysOk = hostKeysRead(&k);
      keys = keysOk ? k : 0;
      const uint16_t edge = keys & ~prevKeys;
      prevKeys = keys;
      if (edge) printf("keys: 0x%04x (edge 0x%04x)\n", keys, edge);

      if (edge & HKEY_HOME) {
        if (gUi.isVisible()) {
          gUi.close();
        } else {
          if (gCardState == CardState::READY) cardKeysRelease();
          gUi.open();
        }
      } else if (gUi.isVisible()) {
        gUi.onKeysPressed(edge);  // may close itself (Launch) or stop the card
      } else if (gCardState == CardState::READY) {
        cardKeysSet(keys);
      }
    }

    //--- menu closed this iteration: hand the screen back -----------------
    if (menuWasVisible && !gUi.isVisible()) {
      if (gCardState == CardState::READY) {
        gPump.requestFullRepaint();
      } else {
        drawIdleScreen();
        idleShown = true;
      }
    }
    // Card stopped while the menu was open (profile written): the idle
    // screen is drawn when the menu closes.
    if (stateAtTop == CardState::READY && gCardState != CardState::READY) {
      idleShown = gUi.isVisible();
      lastProbeMs = now;
    }

    //--- card LCD stream -> host LCD ---------------------------------------
    if (gCardState == CardState::READY) {
      lcdtap::pico2::i2cSlaveProcess(&gI2c);
      gTap->tick(now);
      if (!gUi.isVisible()) gPump.process(PUMP_MAX_LINES_PER_CALL);
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
