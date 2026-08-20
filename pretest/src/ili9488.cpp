#include "ili9488.hpp"

#include <cstring>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/structs/pio.h"
#include "pico/stdlib.h"

#include "lcd8080.pio.h"

namespace wcb {

//-----------------------------------------------------------------------------
// Tunables
//-----------------------------------------------------------------------------

// MADCTL for landscape orientation.
//   0x28 = MV | BGR              (landscape)
//   0xE8 = MY | MX | MV | BGR    (landscape, rotated 180 degrees)
// Clear bit3 (BGR -> 0x20 / 0xE0) if red and blue come out swapped.
#ifndef WCB_LCD_MADCTL
#define WCB_LCD_MADCTL 0x28
#endif

// Set to 1 if the picture is shown negative (some ILI9488 clones need INVON).
#ifndef WCB_LCD_INVERT
#define WCB_LCD_INVERT 0
#endif

//-----------------------------------------------------------------------------
// ILI9488 commands
//-----------------------------------------------------------------------------
namespace cmd {
static constexpr uint8_t SWRESET = 0x01;
static constexpr uint8_t SLPOUT = 0x11;
static constexpr uint8_t INVOFF = 0x20;
static constexpr uint8_t INVON = 0x21;
static constexpr uint8_t DISPON = 0x29;
static constexpr uint8_t CASET = 0x2A;
static constexpr uint8_t PASET = 0x2B;
static constexpr uint8_t RAMWR = 0x2C;
static constexpr uint8_t MADCTL = 0x36;
static constexpr uint8_t COLMOD = 0x3A;
static constexpr uint8_t IFMODE = 0xB0;
static constexpr uint8_t FRMCTR1 = 0xB1;
static constexpr uint8_t INVTR = 0xB4;
static constexpr uint8_t DISCTRL = 0xB6;
static constexpr uint8_t ETMOD = 0xB7;
static constexpr uint8_t PWCTRL1 = 0xC0;
static constexpr uint8_t PWCTRL2 = 0xC1;
static constexpr uint8_t VMCTRL = 0xC5;
static constexpr uint8_t PGAMCTRL = 0xE0;
static constexpr uint8_t NGAMCTRL = 0xE1;
static constexpr uint8_t ADJCTRL3 = 0xF7;
}  // namespace cmd

//-----------------------------------------------------------------------------
// Low level
//-----------------------------------------------------------------------------

void Ili9488::putByte(uint8_t b) { pio_sm_put_blocking(pio_, sm_, b); }

// Wait until the state machine has actually clocked out everything: the TX
// FIFO being empty is not enough (the OSR may still hold a byte). TXSTALL is
// asserted every cycle the SM sits stalled on an empty FIFO, so clearing it
// and waiting for it to come back proves the SM is idle. By then the last
// WR# rising edge is at least two SM cycles old, satisfying the D/C hold
// time.
void Ili9488::waitSmIdle() {
  const uint32_t bit = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm_);
  pio_->fdebug = bit;  // W1C
  while (!(pio_->fdebug & bit)) tight_loop_contents();
}

void Ili9488::waitIdle() {
  if (dmaCh_ >= 0) dma_channel_wait_for_finish_blocking(dmaCh_);
  waitSmIdle();
}

void Ili9488::writeCommand(uint8_t c) {
  waitIdle();
  gpio_put(pins_.dc, 0);
  putByte(c);
  waitSmIdle();
  gpio_put(pins_.dc, 1);
}

void Ili9488::writeData(uint8_t d) { putByte(d); }

void Ili9488::writeData(const uint8_t* d, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) putByte(d[i]);
}

void Ili9488::startDma(const void* src, uint32_t bytes, bool ringPattern) {
  dma_channel_config c = dma_channel_get_default_config(dmaCh_);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, pio_get_dreq(pio_, sm_, true));
  // ringPattern: wrap the read address every 2 bytes so a single RGB565
  // pattern is repeated for the whole transfer.
  channel_config_set_ring(&c, false, ringPattern ? 1 : 0);
  dma_channel_configure(dmaCh_, &c, &pio_->txf[sm_], src, bytes, true);
}

//-----------------------------------------------------------------------------
// Init
//-----------------------------------------------------------------------------

bool Ili9488::init(PIO pio, uint sm, const Pins& pins, float pioClkDiv) {
  pio_ = pio;
  sm_ = sm;
  pins_ = pins;

  // Static control lines.
  gpio_init(pins.dc);
  gpio_set_dir(pins.dc, GPIO_OUT);
  gpio_put(pins.dc, 1);
  gpio_init(pins.rd);
  gpio_set_dir(pins.rd, GPIO_OUT);
  gpio_put(pins.rd, 1);  // never read
  gpio_init(pins.cs);
  gpio_set_dir(pins.cs, GPIO_OUT);
  gpio_put(pins.cs, 1);
  gpio_init(pins.rst);
  gpio_set_dir(pins.rst, GPIO_OUT);
  gpio_put(pins.rst, 1);

  // PIO program.
  if (!pio_can_add_program(pio, &lcd8080_program)) return false;
  uint off = pio_add_program(pio, &lcd8080_program);

  pio_sm_config c = lcd8080_program_get_default_config(off);
  sm_config_set_out_pins(&c, pins.d0, 8);
  sm_config_set_sideset_pins(&c, pins.wr);
  sm_config_set_out_shift(&c, /*shift_right=*/true, /*autopull=*/true, 8);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
  sm_config_set_clkdiv(&c, pioClkDiv);

  for (uint p = pins.d0; p < pins.d0 + 8; ++p) {
    pio_gpio_init(pio, p);
    gpio_set_slew_rate(p, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(p, GPIO_DRIVE_STRENGTH_8MA);
  }
  pio_gpio_init(pio, pins.wr);
  gpio_set_slew_rate(pins.wr, GPIO_SLEW_RATE_FAST);
  gpio_set_drive_strength(pins.wr, GPIO_DRIVE_STRENGTH_8MA);

  // WR# high and all bus pins as outputs before the SM starts.
  pio_sm_set_pins_with_mask(pio, sm, 1u << pins.wr, 1u << pins.wr);
  pio_sm_set_consecutive_pindirs(pio, sm, pins.d0, 8, true);
  pio_sm_set_consecutive_pindirs(pio, sm, pins.wr, 1, true);
  pio_sm_init(pio, sm, off, &c);
  pio_sm_set_enabled(pio, sm, true);

  dmaCh_ = dma_claim_unused_channel(true);

  // Hardware reset.
  gpio_put(pins.rst, 1);
  sleep_ms(10);
  gpio_put(pins.rst, 0);
  sleep_ms(20);
  gpio_put(pins.rst, 1);
  sleep_ms(120);

  gpio_put(pins.cs, 0);  // single device: keep selected forever

  // Init sequence (8080 8-bit, RGB565).
  static const uint8_t pgam[] = {0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78,
                                 0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F};
  static const uint8_t ngam[] = {0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45,
                                 0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F};

  writeCommand(cmd::PGAMCTRL);
  writeData(pgam, sizeof(pgam));
  writeCommand(cmd::NGAMCTRL);
  writeData(ngam, sizeof(ngam));

  writeCommand(cmd::PWCTRL1);
  writeData(0x17);
  writeData(0x15);
  writeCommand(cmd::PWCTRL2);
  writeData(0x41);
  writeCommand(cmd::VMCTRL);
  writeData(0x00);
  writeData(0x12);
  writeData(0x80);

  writeCommand(cmd::MADCTL);
  writeData(WCB_LCD_MADCTL);
  writeCommand(cmd::COLMOD);
  writeData(0x55);  // 16 bpp
  writeCommand(cmd::IFMODE);
  writeData(0x00);
  writeCommand(cmd::FRMCTR1);
  writeData(0xA0);  // 60 Hz
  writeCommand(cmd::INVTR);
  writeData(0x02);  // 2-dot inversion
  writeCommand(cmd::DISCTRL);
  writeData(0x02);
  writeData(0x02);
  writeData(0x3B);
  writeCommand(cmd::ETMOD);
  writeData(0xC6);
  writeCommand(cmd::ADJCTRL3);
  writeData(0xA9);
  writeData(0x51);
  writeData(0x2C);
  writeData(0x82);
  writeCommand(WCB_LCD_INVERT ? cmd::INVON : cmd::INVOFF);

  writeCommand(cmd::SLPOUT);
  waitIdle();
  sleep_ms(120);
  writeCommand(cmd::DISPON);
  waitIdle();
  sleep_ms(20);

  return true;
}

//-----------------------------------------------------------------------------
// Drawing
//-----------------------------------------------------------------------------

void Ili9488::setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  writeCommand(cmd::CASET);
  writeData(x0 >> 8);
  writeData(x0 & 0xFF);
  writeData(x1 >> 8);
  writeData(x1 & 0xFF);
  writeCommand(cmd::PASET);
  writeData(y0 >> 8);
  writeData(y0 & 0xFF);
  writeData(y1 >> 8);
  writeData(y1 & 0xFF);
  writeCommand(cmd::RAMWR);
}

// Byte-swap n RGB565 pixels into dst (big-endian on the bus: high byte first).
static inline void swapLine(uint8_t* dst, const uint16_t* src, uint32_t n) {
  uint32_t* d32 = reinterpret_cast<uint32_t*>(dst);
  const uint32_t* s32 = reinterpret_cast<const uint32_t*>(src);
  uint32_t words = n >> 1;
  for (uint32_t i = 0; i < words; ++i) {
    uint32_t w = s32[i];
    d32[i] = ((w & 0xFF00FF00u) >> 8) | ((w & 0x00FF00FFu) << 8);
  }
  if (n & 1) {
    uint16_t p = src[n - 1];
    dst[(n - 1) * 2] = p >> 8;
    dst[(n - 1) * 2 + 1] = p & 0xFF;
  }
}

void Ili9488::writeLineAsync(const uint16_t* src, uint32_t n) {
  if (n == 0) return;
  if (n > LINE_PIXELS) n = LINE_PIXELS;
  // lineBuf_[cur_] was last handed to the DMA two calls ago; that transfer
  // completed inside the previous call's wait, so it is free now. Swapping
  // into it overlaps with the DMA of the other buffer.
  uint8_t* buf = lineBuf_[cur_];
  swapLine(buf, src, n);
  dma_channel_wait_for_finish_blocking(dmaCh_);
  startDma(buf, n * 2, false);
  cur_ ^= 1;
}

void Ili9488::writePixels(const uint16_t* src, uint32_t n) {
  while (n > 0) {
    uint32_t chunk = (n > LINE_PIXELS) ? LINE_PIXELS : n;
    writeLineAsync(src, chunk);
    src += chunk;
    n -= chunk;
  }
}

void Ili9488::fillRect(int x, int y, int w, int h, uint16_t color) {
  int x1 = x + w - 1, y1 = y + h - 1;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x1 > LCD_W - 1) x1 = LCD_W - 1;
  if (y1 > LCD_H - 1) y1 = LCD_H - 1;
  if (x > x1 || y > y1) return;

  setWindow(x, y, x1, y1);
  waitIdle();
  fillPattern_[0] = color >> 8;
  fillPattern_[1] = color & 0xFF;
  uint32_t bytes = static_cast<uint32_t>(x1 - x + 1) *
                   static_cast<uint32_t>(y1 - y + 1) * 2u;
  startDma(fillPattern_, bytes, true);
  waitIdle();
}

}  // namespace wcb
