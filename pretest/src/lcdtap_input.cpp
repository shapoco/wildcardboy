#include "lcdtap_input.hpp"

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include <lcdtap/pico2/i2c_slave.hpp>
#include <lcdtap/pico2/spi_slave.hpp>

#include "board_pins.hpp"

namespace wcb {

static constexpr uint32_t I2C_RING_WORDS = 1024;  // 4 KB
static constexpr uint32_t SPI_RING_LOG2 = 15;      // 32 KB
static constexpr uint32_t SPI_RING_BYTES = 1u << SPI_RING_LOG2;
static constexpr uint32_t SPI_RING_WORDS = SPI_RING_BYTES / sizeof(uint32_t);
static constexpr uint32_t MAX_GROUPS_PER_CALL = 64;

// 4-line SPI pins: CS / SCLK are baked into spi_4line_mode0.pio (GPIO1 /
// GPIO2); MOSI and DC must be adjacent (IN_BASE, IN_BASE+1).
static constexpr uint PIN_SPI_RST = 0;
static constexpr uint PIN_SPI_CS = SPI_CS_PIN;      // 1
static constexpr uint PIN_SPI_SCLK = SPI_SCLK_PIN;  // 2
static constexpr uint PIN_SPI_MOSI = 3;
static constexpr uint PIN_SPI_DC = 4;
static_assert(PIN_SPI_CS == 1 && PIN_SPI_SCLK == 2, "spi_4line_mode0.pio pin map");

static uint32_t sI2cRing[I2C_RING_WORDS];
static uint32_t __attribute__((aligned(SPI_RING_BYTES))) sSpiRing[SPI_RING_WORDS];

static lcdtap::pico2::I2cSlaveState sI2c;
static lcdtap::pico2::SpiSlaveState sSpi;
static lcdtap::LcdTap* sTap = nullptr;
static lcdtap::BusType sBus = lcdtap::BusType::I2C;
static bool sAttached = false;
static LineQueue* sQueue = nullptr;
static LcdtapInputStats sStats;
static uint32_t sLastEpoch = 0;
static uint16_t sDirtyRow = 0;

//-----------------------------------------------------------------------------
// GPIO IRQ (core 1): SPI CS rise resyncs the PIO SM; RST mirrors into LcdTap.
//-----------------------------------------------------------------------------
static void __not_in_flash_func(gpioIrqHandler)(uint gpio, uint32_t events) {
  if (!sAttached || sBus != lcdtap::BusType::SPI_4LINE) return;
  if (gpio == PIN_SPI_CS) {
    if (events & GPIO_IRQ_EDGE_RISE) lcdtap::pico2::spiSlaveResetSm(&sSpi);
  } else if (gpio == PIN_SPI_RST && sTap) {
    if (events & GPIO_IRQ_EDGE_FALL) {
      sTap->inputReset(true);
      lcdtap::pico2::spiSlaveResetSm(&sSpi);
    }
    sTap->inputReset(!gpio_get(PIN_SPI_RST));
  }
}

//-----------------------------------------------------------------------------
// Attach / detach
//-----------------------------------------------------------------------------

void lcdtapInputAttach(lcdtap::LcdTap* tap, lcdtap::BusType bus, uint8_t i2cAddr,
                       LineQueue* queue) {
  if (sAttached) lcdtapInputDetach();
  sTap = tap;
  sBus = bus;
  sQueue = queue;
  sLastEpoch = tap->getPresentationEpoch();
  sDirtyRow = 0;
  tap->setDirtyTracking(true);

  if (bus == lcdtap::BusType::I2C) {
    lcdtap::pico2::I2cSlaveConfig cfg;
    cfg.i2c = i2c1;
    cfg.pinSda = PIN_LC_LCD_SDA;
    cfg.pinScl = PIN_LC_LCD_SCL;
    cfg.slaveAddr = i2cAddr;
    sI2c.inst = nullptr;
    sI2c.dropWords = 0;
    sI2c.hwOverflowCount = 0;
    sI2c.backlogMaxWords = 0;
    lcdtap::pico2::i2cSlaveInit(&sI2c, cfg, sI2cRing, I2C_RING_WORDS);
    sI2c.inst = tap;
  } else {
    // RST input with pull-up; the card drives it low during its own reset.
    gpio_init(PIN_SPI_RST);
    gpio_set_dir(PIN_SPI_RST, GPIO_IN);
    gpio_pull_up(PIN_SPI_RST);

    lcdtap::pico2::SpiSlaveConfig cfg = {pio2, 0, PIN_SPI_CS, PIN_SPI_SCLK,
                                         PIN_SPI_MOSI, PIN_SPI_DC, SPI_RING_LOG2};
    sSpi.inst = nullptr;
    sSpi.dropWords = 0;
    sSpi.backlogMaxWords = 0;
    lcdtap::pico2::spiSlaveInit(&sSpi, cfg, sSpiRing, SPI_RING_WORDS);
    sSpi.inst = tap;
    gpio_set_irq_enabled_with_callback(PIN_SPI_CS, GPIO_IRQ_EDGE_RISE, true, gpioIrqHandler);
    gpio_set_irq_enabled(PIN_SPI_RST, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
    tap->inputReset(!gpio_get(PIN_SPI_RST));
  }
  sAttached = true;
}

void lcdtapInputDetach() {
  if (!sAttached) return;
  sAttached = false;
  if (sBus == lcdtap::BusType::I2C) {
    lcdtap::pico2::i2cSlaveDeinit(&sI2c);
    sI2c.inst = nullptr;
  } else {
    gpio_set_irq_enabled(PIN_SPI_RST, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, false);
    lcdtap::pico2::spiSlaveDeinit(&sSpi);
    sSpi.inst = nullptr;
    for (uint pin : {PIN_SPI_RST, PIN_SPI_CS, PIN_SPI_SCLK, PIN_SPI_MOSI, PIN_SPI_DC}) {
      gpio_init(pin);
      gpio_disable_pulls(pin);
    }
  }
  sTap = nullptr;
  sQueue = nullptr;
}

bool lcdtapInputAttached() { return sAttached; }

void lcdtapInputSetReset(bool assert) {
  if (sTap) sTap->inputReset(assert);
}

const LcdtapInputStats& lcdtapInputStats() {
  if (sAttached) {
    if (sBus == lcdtap::BusType::I2C) {
      sStats.dropWords = sI2c.dropWords;
      sStats.hwOverflow = sI2c.hwOverflowCount;
      sStats.backlogMax = sI2c.backlogMaxWords;
    } else {
      sStats.dropWords = sSpi.dropWords;
      sStats.hwOverflow = 0;
      sStats.backlogMax = sSpi.backlogMaxWords;
    }
  }
  return sStats;
}

//-----------------------------------------------------------------------------
// Dirty scan -> line groups (same mapping as lcdtap's displaylink pump)
//-----------------------------------------------------------------------------

static inline uint32_t groupFirstLine(const lcdtap::OutputMapInfo& mi, uint32_t t) {
  if (mi.stepV == 0) return mi.destY;
  return mi.destY + static_cast<uint32_t>(((static_cast<uint64_t>(t) << 16) + mi.stepV - 1u) / mi.stepV);
}

static void publishFull() {
  if (sQueue->push(LineQueue::MSG_FULL_REPAINT)) {
    sStats.fullRepaints = sStats.fullRepaints + 1;
  }
  // Everything pending becomes redundant; the map is cleared so the sweep
  // starts from a clean slate (later writes re-mark their rows).
  memset(sTap->dirtyMap(), 0, sTap->getConfig().buffHeight);
}

static void scanDirty() {
  const uint32_t epoch = sTap->getPresentationEpoch();
  if (epoch != sLastEpoch) {
    sLastEpoch = epoch;
    publishFull();
    return;
  }
  lcdtap::OutputMapInfo mi;
  sTap->getOutputMapInfo(&mi);
  uint8_t* map = sTap->dirtyMap();
  const uint16_t fbH = mi.fbHeight;

  if (mi.rotation & 1u) {
    for (uint32_t r = 0; r < fbH; ++r) {
      if (map[r]) {
        publishFull();
        return;
      }
    }
    return;
  }

  const uint32_t yMax = static_cast<uint32_t>(mi.destY + mi.destH) - 1u;
  uint32_t groups = 0;
  for (uint32_t scanned = 0; scanned < fbH && groups < MAX_GROUPS_PER_CALL; ++scanned) {
    uint32_t r = sDirtyRow;
    sDirtyRow = static_cast<uint16_t>((r + 1u) % fbH);
    if (map[r] == 0) continue;
    map[r] = 0;  // claim before computing; later writes re-mark
    if (r < mi.srcY || r >= static_cast<uint32_t>(mi.srcY + mi.srcH)) continue;
    const uint32_t t = (mi.rotation == 0) ? (r - mi.srcY)
                                          : (static_cast<uint32_t>(mi.srcH) - 1u - (r - mi.srcY));
    uint32_t y0 = groupFirstLine(mi, t);
    uint32_t y1 = groupFirstLine(mi, t + 1u);
    y1 = (y1 > y0) ? y1 - 1u : y0;
    if (y0 > yMax) continue;
    if (y1 > yMax) y1 = yMax;
    if (!sQueue->push(LineQueue::packLines(y0, y1))) {
      sStats.queueFull = sStats.queueFull + 1;
      // Consumer is behind: fold everything into one full repaint. The
      // queue has no room for the marker either, so it is re-attempted
      // on the next call (map rows written meanwhile keep re-marking).
      map[r] |= 1;
      sDirtyRow = static_cast<uint16_t>(r);
      return;
    }
    groups++;
  }
  sStats.groups = sStats.groups + groups;
}

void lcdtapInputProcess(uint32_t nowMs) {
  if (!sAttached) return;
  if (sBus == lcdtap::BusType::I2C) {
    lcdtap::pico2::i2cSlaveProcess(&sI2c);
  } else {
    lcdtap::pico2::spiSlaveProcess(&sSpi);
  }
  sTap->tick(nowMs);
  scanDirty();
}

}  // namespace wcb
