// WildCardBoy pretest firmware: logic card bring-up (TJP / PP1 / PP2).
//
//   Detect : the card's profile EEPROM must ACK 3 times (200 ms apart), then
//            the profile is read and validated (length / CRC32 / CBOR).
//   Stopped: RESET asserted, LCTF_ENAX high. Entered after detection (the
//            card auto-starts only when it was present at boot).
//   Running: RESET released; LcdTap captures the card's LCD stream on core 1
//            (I2C or 4-line SPI) and core 0 pushes the changed lines to the
//            host LCD; host keys go to the card's key lines (GPIO or the
//            card PCA9555); with useTfCard the TF card is handed to the card.
//   HOME opens the system menu: Start/Stop card, Apps (program the card MCU
//   from the TF card: ATtiny ISP over SPI or UF2 over USB MSC), Profile
//   (write a profile .hex into the card EEPROM and re-detect the card).
//
// Debug output: USB CDC. See ../spec/ for the pin map.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include <lcdtap/lcdtap.hpp>

#include "app_image.hpp"
#include "aux_i2c.hpp"
#include "board_pins.hpp"
#include "card_eeprom.hpp"
#include "card_io.hpp"
#include "card_profile.hpp"
#include "core1.hpp"
#include "ili9488.hpp"
#include "isp_tiny85.hpp"
#include "isp_usb.hpp"
#include "lcd_pump.hpp"
#include "lcdtap_input.hpp"
#include "sd_spi.hpp"
#include "sys_clock.hpp"
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
static constexpr uint32_t CARD_IDLE_POLL_MS = 500;      // INVALID / STOPPED removal check
static constexpr uint32_t KEY_POLL_INTERVAL_MS = 5;
static constexpr uint32_t STATS_INTERVAL_MS = 1000;
static constexpr uint32_t PUMP_MAX_LINES_PER_CALL = 48;
static constexpr float LCD_PIO_BASE_HZ = 150e6f;  // 40 ns/byte at clkdiv 1.0

//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------
static Ili9488 gLcd;
static LcdPump gPump;
static UiMenu gUi;
static lcdtap::LcdTap* gTap = nullptr;

static CardState gCardState = CardState::NONE;
static CardProfile gProfile;                    // profile of the present card
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
// Card lifecycle
//-----------------------------------------------------------------------------

// Profile accepted: configure the bus, hold the card in reset (STOPPED).
static void cardPrepare(const CardProfile& p) {
  cardIoConfigure(p);
  cardResetAssert();
  if (!sdBusOwnedByHost()) sdBusAcquire();
  snprintf(gAppsDir, sizeof(gAppsDir), "%s/%s/Apps", CARDS_DIR, p.id);
  gCardState = CardState::STOPPED;
  printf("card prepared: id=%s name=\"%s\" preset=%s isp=%u useTfCard=%d (stopped)\n", p.id,
         p.name, p.lcdtapPreset, p.ispMethod, p.useTfCard);
}

// STOPPED -> RUNNING.
static bool cardStart() {
  if (gCardState != CardState::STOPPED) return false;

  // LcdTap: preset + profile overrides; output raster = host LCD so one
  // fillScanline() call is one LCD line.
  lcdtap::LcdTapConfig cfg;
  profileBuildLcdTapConfig(gProfile, &cfg);
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
    printf("LcdTap init failed (%d)\n", static_cast<int>(gTap->getStatus()));
    delete gTap;
    gTap = nullptr;
    return false;
  }

  // Core 1 takes over the capture; core 0 renders from the line queue.
  Core1Shared& sh = core1Shared();
  sh.tap = gTap;
  sh.bus = cfg.busInterface;
  sh.i2cAddr = cfg.i2cSlaveAddr;
  sh.queue.clear();
  core1Call(Core1Cmd::LCDTAP_ATTACH);
  gPump.init(gTap, &gLcd, &sh.queue);

  if (cardUseTfCard()) sdBusRelease();  // TF card to the logic card

  printf("card started: %s bus=%s addr=0x%02x fb=%ux%u\n",
         lcdtap::CONTROLLER_NAMES[static_cast<int>(cfg.controllerFamily)],
         lcdtap::BUS_NAMES[static_cast<int>(cfg.busInterface)], cfg.i2cSlaveAddr,
         cfg.buffWidth, cfg.buffHeight);

  // Release the MCU; mirror the reset into LcdTap.
  core1Call(Core1Cmd::LCDTAP_RESET_ASSERT);
  cardKeysRelease();
  cardResetRelease();
  core1Call(Core1Cmd::LCDTAP_RESET_RELEASE);
  gCardState = CardState::RUNNING;
  return true;
}

// RUNNING -> STOPPED.
static void cardStop() {
  if (gCardState != CardState::RUNNING) return;
  cardResetAssert();
  cardKeysRelease();
  core1Call(Core1Cmd::LCDTAP_DETACH);
  delete gTap;
  gTap = nullptr;
  if (!sdBusOwnedByHost()) sdBusAcquire();
  gCardState = CardState::STOPPED;
  printf("card stopped\n");
}

// Any state -> NONE (card removed or profile rewritten).
static void cardForget() {
  if (gCardState == CardState::RUNNING) cardStop();
  cardIoRelease();
  if (!sdBusOwnedByHost()) sdBusAcquire();
  gAppsDir[0] = '\0';
  gCardState = CardState::NONE;
}

//-----------------------------------------------------------------------------
// Idle screens (outside the menu)
//-----------------------------------------------------------------------------

static void drawIdleScreen() {
  gLcd.clear(COLOR_BLACK);
  drawTextCentered(gLcd, 120, "WildCardBoy pretest", COLOR_WHITE, COLOR_BLACK, 3);
  char line[48];
  switch (gCardState) {
    case CardState::INVALID:
      snprintf(line, sizeof(line), "Profile: %s", profileErrorText(gProfileError));
      drawTextCentered(gLcd, 200, line, COLOR_RED, COLOR_BLACK, 2);
      drawTextCentered(gLcd, 240, "HOME > Profile to write one", COLOR_GRAY, COLOR_BLACK, 1);
      break;
    case CardState::STOPPED:
      snprintf(line, sizeof(line), "%s stopped", gProfile.id);
      drawTextCentered(gLcd, 200, line, COLOR_YELLOW, COLOR_BLACK, 2);
      drawTextCentered(gLcd, 240, "HOME > Start card", COLOR_GRAY, COLOR_BLACK, 1);
      break;
    default:
      drawTextCentered(gLcd, 200, "No Logic Card", COLOR_YELLOW, COLOR_BLACK, 2);
      break;
  }
  drawTextCentered(gLcd, 300, "HOME: system menu", COLOR_GRAY, COLOR_BLACK, 1);
}

//-----------------------------------------------------------------------------
// UI hooks
//-----------------------------------------------------------------------------

static void ispProgressWrite(int pct, void*) { gUi.progress("Write", pct); }
static void ispProgressVerify(int pct, void*) { gUi.progress("Verify", pct); }
static void eepromProgressWrite(int pct, void*) { gUi.progress("Write", pct); }
static void ispUsbProgress(const char* stage, int pct, void*) { gUi.progress(stage, pct); }

static CardState hookCardState(void*) { return gCardState; }
static bool hookTfBusy(void*) { return gCardState == CardState::RUNNING && cardUseTfCard(); }
static const char* hookCardId(void*) {
  return (gCardState == CardState::STOPPED || gCardState == CardState::RUNNING) ? gProfile.id : nullptr;
}
static const char* hookAppsDir(void*) { return gAppsDir[0] ? gAppsDir : nullptr; }
static bool hookStartCard(void*) { return cardStart(); }
static bool hookStopCard(void*) { cardStop(); return true; }

static bool hasExt(const char* path, const char* ext) {
  const char* dot = strrchr(path, '.');
  return dot && strcasecmp(dot, ext) == 0;
}

// ATtiny85 over SPI ISP. Card is stopped (RESET asserted) on entry and exit.
static const char* programAppSpi(const char* path) {
  IspPins pins;
  if (!cardIspPins(&pins)) return "ISP pins not in profile";

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

  // Data pins back to Hi-Z, lines re-armed, card held in reset (stopped).
  ispEnd();
  cardIoConfigure(gProfile);
  cardResetAssert();
  printf("[prog] %s (%llu ms)\n", err ? err : "done",
         static_cast<unsigned long long>((t1 - t0) / 1000));
  return err;
}

static const char* hookProgramApp(const char* path, void*) {
  if (gCardState != CardState::RUNNING && gCardState != CardState::STOPPED) return "No Logic Card";
  const bool uf2 = hasExt(path, ".uf2");
  const IspMode mode = cardIspMode();
  if (uf2 && mode != IspMode::USB) return "Card has no USB ISP";
  if (!uf2 && mode != IspMode::SPI) return uf2 ? "Card has no USB ISP" : "Card has no SPI ISP";

  if (gCardState == CardState::RUNNING) cardStop();  // programming needs the card quiet
  if (uf2) return ispUsbProgram(path, ispUsbProgress, nullptr);
  return programAppSpi(path);
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

  // Forget the card; the main loop re-detects it from scratch.
  cardForget();
  return nullptr;
}

//-----------------------------------------------------------------------------
// Statistics
//-----------------------------------------------------------------------------
static void printStats(uint16_t keys, bool keysOk, uint32_t loopsPerSec) {
  static const char* NAMES[] = {"none", "invalid", "stopped", "running"};
  const Core1Shared& sh = core1Shared();
  printf("[stat] card=%s keys=0x%04x%s loops=%lu core1=%lu",
         NAMES[static_cast<int>(gCardState)], keys, keysOk ? "" : "(err)",
         static_cast<unsigned long>(loopsPerSec), static_cast<unsigned long>(sh.loops));
  if (gTap) {
    const LcdtapInputStats& in = lcdtapInputStats();
    printf(" rxCmd=%lu rxData=%lu unk=%lu(0x%02x) hwrst=%lu drop=%lu ovf=%lu backlog=%lu"
           " | q=%lu groups=%lu qfull=%lu full1=%lu | lines=%lu full=%lu groups=%lu",
           static_cast<unsigned long>(gTap->getRxCmdBytes()),
           static_cast<unsigned long>(gTap->getRxDataBytes()),
           static_cast<unsigned long>(gTap->getUnknownCmdCount()),
           gTap->getLastUnknownCmd(),
           static_cast<unsigned long>(gTap->getHwResetCount()),
           static_cast<unsigned long>(in.dropWords),
           static_cast<unsigned long>(in.hwOverflow),
           static_cast<unsigned long>(in.backlogMax),
           static_cast<unsigned long>(sh.queue.count()),
           static_cast<unsigned long>(in.groups),
           static_cast<unsigned long>(in.queueFull),
           static_cast<unsigned long>(in.fullRepaints),
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
  sysClockInit288();

  // TinyUSB device stack (CDC for stdio) is ours in the dual-role build;
  // pico_stdio_usb expects it to be up before stdio_init_all().
  tusb_rhport_init_t devInit = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO};
  tusb_init(BOARD_TUD_RHPORT, &devInit);
  stdio_init_all();
  printf("\n=== WildCardBoy pretest ===\n");
  printf("clk_sys %lu Hz, clk_peri %lu Hz\n",
         static_cast<unsigned long>(clock_get_hz(clk_sys)),
         static_cast<unsigned long>(clock_get_hz(clk_peri)));

  // TF card mux to the host, SPI0 ready (the card itself is probed when
  // the browser opens).
  sdBusInit();

  // Host LCD.
  Ili9488::Pins lcdPins = {PIN_LCD_D0, PIN_LCD_DC, PIN_LCD_WR,
                           PIN_LCD_RD, PIN_LCD_CS, PIN_LCD_RST};
  const float lcdClkDiv = static_cast<float>(clock_get_hz(clk_sys)) / LCD_PIO_BASE_HZ;
  if (!gLcd.init(pio0, 0, lcdPins, lcdClkDiv)) {
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

  // AUX I2C (keypad + card EEPROM / PCA9555).
  auxI2cInit();

  // Core 1 engine (LcdTap capture / USB host).
  core1Launch();

  // System menu.
  UiHooks hooks;
  hooks.cardState = hookCardState;
  hooks.tfBusy = hookTfBusy;
  hooks.cardId = hookCardId;
  hooks.appsDir = hookAppsDir;
  hooks.programApp = hookProgramApp;
  hooks.validateProfile = hookValidateProfile;
  hooks.writeProfile = hookWriteProfile;
  hooks.startCard = hookStartCard;
  hooks.stopCard = hookStopCard;
  hooks.user = nullptr;
  gUi.init(&gLcd, hooks);

  bool idleShown = false;
  // A card that answers from the very first probe was present at boot and
  // starts automatically; once a NAK has been seen (no card / removed) any
  // later detection is a hot insertion and asks first.
  bool bootCard = true;
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
              cardPrepare(gProfile);
              if (bootCard) {
                if (gUi.isVisible()) gUi.close();
                cardStart();
              } else if (!gUi.isVisible()) {
                gUi.promptStart();
              }
              bootCard = false;
              idleShown = false;
            } else {
              printf("card profile invalid: %s (len field %lu)\n",
                     profileErrorText(gProfileError), static_cast<unsigned long>(cborLen));
              gCardState = CardState::INVALID;
              bootCard = false;
              idleShown = false;
            }
          }
        } else {
          if (probeOkCount > 0) {
            printf("card EEPROM (0x%02x) NAK; ACK streak reset\n", ADDR_CARD_EEPROM);
          }
          probeOkCount = 0;
          bootCard = false;
          if (!idleShown) {
            printf("no card (EEPROM 0x%02x NAK); retrying every %lu ms\n",
                   ADDR_CARD_EEPROM, static_cast<unsigned long>(CARD_PROBE_INTERVAL_MS));
          }
        }
      }
    } else if (gCardState == CardState::INVALID || gCardState == CardState::STOPPED) {
      // Card present but not running: notice removal (NAK) so a new card
      // can be detected from scratch.
      if (now - lastProbeMs >= CARD_IDLE_POLL_MS) {
        lastProbeMs = now;
        if (!cardEepromProbe()) {
          printf("card removed\n");
          cardForget();
          idleShown = false;
        }
      }
    }

    if (!idleShown && gCardState != CardState::RUNNING && !gUi.isVisible()) {
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
          if (gCardState == CardState::RUNNING) cardKeysRelease();
          gUi.open();
        }
      } else if (gUi.isVisible()) {
        gUi.onKeysPressed(edge);  // may close itself, start/stop the card
      } else if (gCardState == CardState::RUNNING) {
        cardKeysSet(keys);
      }
    }

    //--- menu closed this iteration: hand the screen back -----------------
    if (menuWasVisible && !gUi.isVisible()) {
      if (gCardState == CardState::RUNNING) {
        gPump.requestFullRepaint();
      } else {
        drawIdleScreen();
        idleShown = true;
      }
    }
    // Left the running state while the menu was open (stop / programming /
    // profile rewrite): the idle screen is drawn when the menu closes.
    if (stateAtTop == CardState::RUNNING && gCardState != CardState::RUNNING) {
      idleShown = gUi.isVisible();
      lastProbeMs = now;
    }

    //--- card LCD stream -> host LCD (core 1 feeds the queue) -------------
    if (gCardState == CardState::RUNNING && !gUi.isVisible()) {
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
