#pragma once

// WildCardBoy host controller pin map (see SPEC.md) and constants shared by
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
// WildCardBus, TJP card assignment
//-----------------------------------------------------------------------------
// LCIOn = GPIOn for n = 0..13.
static constexpr uint PIN_LC_LCD_SDA = 2;  // LCIO2  -> ATtiny85 PB0
static constexpr uint PIN_LC_LCD_SCL = 3;  // LCIO3  -> ATtiny85 PB2
static constexpr uint PIN_LC_KEY_L = 5;    // LCIO5  (open-drain, active-low)
static constexpr uint PIN_LC_KEY_R = 6;    // LCIO6
static constexpr uint PIN_LC_KEY_U = 7;    // LCIO7
static constexpr uint PIN_LC_KEY_D = 8;    // LCIO8
static constexpr uint PIN_LC_KEY_A = 9;    // LCIO9
static constexpr uint PIN_LC_RESET = 13;   // LCIO13 -> ATtiny85 RESET (o.d.)

// ATtiny85 ISP lines (shared with the SSD1306 I2C pair and the A key).
static constexpr uint PIN_LC_ISP_MOSI = 2;  // LCIO2 -> PB0
static constexpr uint PIN_LC_ISP_SCK = 3;   // LCIO3 -> PB2
static constexpr uint PIN_LC_ISP_MISO = 9;  // LCIO9 -> PB1

// SSD1306 I2C slave address the ATtiny85 talks to.
static constexpr uint8_t ADDR_LC_LCD = 0x3C;

// TF card layout (see SPEC.md).
static constexpr const char* CARD_ID = "TJP";
static constexpr const char* APPS_DIR = "/WCB/Cards/TJP/Apps";
static constexpr uint32_t TINY85_FLASH_SIZE = 8192;

// Card key bits (subset of the host bits, same positions).
static constexpr uint16_t CKEY_MASK = HKEY_L | HKEY_R | HKEY_U | HKEY_D | HKEY_A;

}  // namespace wcb
