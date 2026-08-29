#pragma once

// WildCardBoy host controller pin map (see spec/01_host_interfaces.md, spec/02_wildcardbus.md) and constants shared by
// the pretest firmware.

#include <cstdint>

namespace wcb {

//-----------------------------------------------------------------------------
// Host LCD (ILI9488, 8080 8-bit parallel)
//-----------------------------------------------------------------------------
static constexpr uint PIN_LCD_RST = 15;
static constexpr uint PIN_LCD_D0 = 16;  // D0..D7 = GPIO16..23 (contiguous)
static constexpr uint PIN_LCD_DC = 24;
static constexpr uint PIN_LCD_WR = 25;  // NOTE: also PICO_DEFAULT_LED_PIN
static constexpr uint PIN_LCD_RD = 26;
static constexpr uint PIN_LCD_CS = 27;

// Logical (landscape) size. The panel is 320x480 portrait, rotated via MADCTL.
static constexpr uint16_t LCD_WIDTH = 480;
static constexpr uint16_t LCD_HEIGHT = 320;

//-----------------------------------------------------------------------------
// TF card (SPI0 pin group) and the TF card mux
//-----------------------------------------------------------------------------
static constexpr uint PIN_HTF_MISO = 32;  // SPI0 RX
static constexpr uint PIN_HTF_CS = 33;    // GPIO-controlled
static constexpr uint PIN_HTF_SCK = 34;   // SPI0 SCK
static constexpr uint PIN_HTF_MOSI = 35;  // SPI0 TX
// Low = TF card wired to the logic card, High = to the host controller.
// Pulled up on the board. While Low the HTF_* pins must be Hi-Z.
static constexpr uint PIN_LCTF_ENAX = 38;

//-----------------------------------------------------------------------------
// AUX I2C buses. Both pin pairs map to the I2C0 controller (GPIO % 4 == 0/1),
// so exactly one pair is switched to the I2C function at a time; the idle
// pair sits Hi-Z. This keeps card hot-plug glitches on LCAUX away from the
// host bus (HAUX).
//-----------------------------------------------------------------------------
// HAUX: host-side devices (keypad PCA9555).
static constexpr uint PIN_HAUX_SDA = 36;
static constexpr uint PIN_HAUX_SCL = 37;
// LCAUX: logic-card-side devices (ID EEPROM, card PCA9555 on other cards).
static constexpr uint PIN_LCAUX_SDA = 28;
static constexpr uint PIN_LCAUX_SCL = 29;
static constexpr uint32_t AUX_I2C_BAUD = 400 * 1000;

static constexpr uint8_t ADDR_HOST_KEYPAD = 0x21;  // PCA9555 (host, HAUX)
static constexpr uint8_t ADDR_CARD_KEYPAD = 0x20;  // PCA9555 (card, LCAUX) = LCIO32-47
static constexpr uint8_t ADDR_CARD_EEPROM = 0x50;  // 24LC256 (card, LCAUX)

// Host keypad bits as returned by PCA9555 (P0 = bits 0..7, P1 = bits 8..15).
// Inputs are active-low on the wire; hostKeysRead() returns them already
// inverted (1 = pressed).
static constexpr uint16_t HKEY_L = 1u << 0;
static constexpr uint16_t HKEY_R = 1u << 1;
static constexpr uint16_t HKEY_U = 1u << 2;
static constexpr uint16_t HKEY_D = 1u << 3;
static constexpr uint16_t HKEY_A = 1u << 4;
static constexpr uint16_t HKEY_B = 1u << 5;
static constexpr uint16_t HKEY_X = 1u << 6;
static constexpr uint16_t HKEY_Y = 1u << 7;
static constexpr uint16_t HKEY_STA = 1u << 8;
static constexpr uint16_t HKEY_SEL = 1u << 9;
static constexpr uint16_t HKEY_BL = 1u << 10;
static constexpr uint16_t HKEY_BR = 1u << 11;
static constexpr uint16_t HKEY_HOME = 1u << 12;

//-----------------------------------------------------------------------------
// WildCardBus
//-----------------------------------------------------------------------------
// LCIOn = GPIOn for n = 0..13. Which LCIO does what comes from the card
// profile (card_profile.hpp); only the LCD I2C pair is fixed by the spec.
static constexpr uint PIN_LCIO_BASE = 0;
static constexpr uint PIN_LC_LCD_SDA = 2;  // LCIO2 (I2C1 SDA)
static constexpr uint PIN_LC_LCD_SCL = 3;  // LCIO3 (I2C1 SCL)
// Virtual I/O expander bus (spec/02): the host is an i2c1 *slave* here.
// Mutually exclusive with the I2C LCD capture above (same controller).
static constexpr uint PIN_LCVIO_SDA = 6;  // LCIO6 (I2C1 SDA)
static constexpr uint PIN_LCVIO_SCL = 7;  // LCIO7 (I2C1 SCL)

// Logic card USB (PIO-USB host). DM must be DP + 1.
static constexpr uint PIN_LCUSB_DP = 30;
static constexpr uint PIN_LCUSB_DM = 31;

// PIO block assignment: pio0 = host LCD (8080), pio1 = PIO-USB host,
// pio2 = LcdTap SPI capture.
static constexpr uint PIO_USB_INDEX = 1;

// System clock (sys_clock.hpp). Peripheral dividers are derived from it.
// 312 MHz gives the LcdTap SPI capture more margin against the 62.5 MHz
// SCLK of PicoSystem cards (spi_4line_mode0.pio worst case = clk_sys / 4).
static constexpr uint32_t SYS_CLOCK_HZ = 312'000'000;

// TF card layout (see spec/05_tf_card.md).
static constexpr const char* CARDS_DIR = "/WCB/Cards";

}  // namespace wcb
