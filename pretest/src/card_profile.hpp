#pragma once

// Card profile (spec/03_card_profile.md): the CBOR object stored in the
// logic card's EEPROM, decoded into a fixed-size struct, plus the frame
// validation (length / CRC32) around it. MCU-independent (host-testable).

#include <cstdint>

#include <lcdtap/config.hpp>

namespace wcb {

static constexpr uint32_t PROFILE_CBOR_MAX = 4096;
static constexpr uint32_t PROFILE_FRAME_MAX = 4 + PROFILE_CBOR_MAX + 4;
// LCIO numbering: 0..13 are GPIOs on the bus, 32..47 are the card-side
// PCA9555 ports (P0_0..P1_7, via LCAUX I2C), 64..79 are the virtual I/O
// expander ports (port A/B: MCP23017 GPA0..GPB7 / PCA9555 P0_0..P1_7,
// host-emulated on LCIO6/7). Others are unused.
static constexpr int NUM_LCIO = 80;
static constexpr int LCIO_GPIO_COUNT = 14;
static constexpr int LCIO_PCA_FIRST = 32;
static constexpr int LCIO_PCA_COUNT = 16;
static constexpr int LCIO_VIRT_FIRST = 64;
static constexpr int LCIO_VIRT_COUNT = 16;

static constexpr bool lcioIsGpio(int i) { return i >= 0 && i < LCIO_GPIO_COUNT; }
static constexpr bool lcioIsPca(int i) { return i >= LCIO_PCA_FIRST && i < LCIO_PCA_FIRST + LCIO_PCA_COUNT; }
static constexpr bool lcioIsVirt(int i) { return i >= LCIO_VIRT_FIRST && i < LCIO_VIRT_FIRST + LCIO_VIRT_COUNT; }
static constexpr bool lcioIsValid(int i) { return lcioIsGpio(i) || lcioIsPca(i) || lcioIsVirt(i); }
static constexpr int NUM_BUTTONS = 12;
static constexpr int ID_MAX = 16;
static constexpr int NAME_MAX = 64;

// Port function numbers.
namespace func {
static constexpr uint8_t UNUSED = 0;
static constexpr uint8_t LCD = 1;
static constexpr uint8_t TF = 2;
static constexpr uint8_t BTN_FIRST = 16;  // 16 + button number (0..11)
static constexpr uint8_t BTN_LAST = 27;
static constexpr uint8_t RESET = 32;
static constexpr uint8_t BOOTSEL = 33;
static constexpr uint8_t ISP_CS = 34;
static constexpr uint8_t ISP_SCK = 35;
static constexpr uint8_t ISP_MOSI = 36;
static constexpr uint8_t ISP_MISO = 37;
static constexpr uint8_t ISP_UART_TX = 38;
static constexpr uint8_t ISP_UART_RX = 39;
static constexpr uint8_t I2C_SLAVE = 48;  // virtual I/O expander bus (LCIO6/7 only)
}  // namespace func

// Port mode bits.
namespace mode {
static constexpr uint8_t INPUT = 1;
static constexpr uint8_t OUTPUT = 2;
static constexpr uint8_t OPEN_DRAIN = 4;
static constexpr uint8_t PULL_UP = 8;
static constexpr uint8_t PULL_DOWN = 16;
static constexpr uint8_t NEGATIVE = 32;
static constexpr uint8_t DIR_MASK = INPUT | OUTPUT | OPEN_DRAIN;
}  // namespace mode

namespace isp_method {
static constexpr uint8_t UNUSED = 0;
static constexpr uint8_t SPI = 1;
static constexpr uint8_t UART_ESP = 2;  // Espressif serial bootloader
static constexpr uint8_t USB_MSC = 16;
}  // namespace isp_method

// AVR devices programmable over the SPI ISP (isp.mcu).
struct AvrDevice {
  const char* id;        // MCU ID in the profile
  const char* name;
  uint8_t signature[3];
  uint32_t flashSize;    // bytes
  uint32_t pageBytes;    // flash page size
};

// nullptr when `id` is not a known AVR device. An empty id resolves to
// ATtiny85 (default for isp.method = 1, backward compatible).
const AvrDevice* avrDeviceById(const char* id);

struct PortCfg {
  uint8_t f = 0;
  uint8_t m = 0;
};

struct CardProfile {
  char id[ID_MAX + 1];
  char name[NAME_MAX + 1];
  PortCfg lcio[NUM_LCIO];
  bool useTfCard;  // card owns the TF card while running (LCTF_ENAX low)
  bool useVirtIoExp;       // host emulates an I/O expander on LCIO6/7 (i2c1 slave)
  char virtIoExpChip[17];  // virtIoExp.chip ("mcp23017" / "pca9555"); empty = member absent
  uint8_t virtIoExpAddr;   // virtIoExp.addr (I2C slave address)
  char lcdtapPreset[32];
  lcdtap::ConfigPreset lcdtapPresetId;
  struct CfgOverride {
    lcdtap::Configs key;
    int16_t value;
  } lcdtapCfg[static_cast<int>(lcdtap::Configs::NUM_CONFIGS)];
  uint8_t lcdtapCfgCount;
  uint8_t ispMethod;
  char ispMcu[17];  // MCU ID ("attiny85", "atmega32u4", ...); may be empty
  PortCfg isp[NUM_LCIO];
  uint8_t keymap[NUM_BUTTONS];  // host button -> card button, 0xFF = unmapped
};

enum class ProfileError : uint8_t {
  OK,
  EMPTY,             // erased EEPROM (length 0xFFFFFFFF) or length 0
  BAD_LENGTH,        // length field inconsistent with the data available
  TOO_LARGE,         // CBOR longer than PROFILE_CBOR_MAX
  CRC_MISMATCH,
  CBOR_ERROR,        // not decodable as the expected structure
  BAD_FORMAT,        // "format" != "WCBCARD"
  BAD_ID,            // id empty or too long
  BAD_NAME,          // name too long
  BAD_PORT,          // LCIO index out of range / duplicated / unknown function
  BAD_PORT_MODE,     // inconsistent mode bits (e.g. open-drain with positive logic)
  UNKNOWN_PRESET,    // lcdtap.preset not a LcdTap preset name
  UNSUPPORTED_LCD_BUS,  // effective LcdTap bus is not I2C / 4-line SPI (pretest limitation)
  MISSING_PORT,      // a required port (e.g. RESET/BOOTSEL for USB ISP) is absent
  BAD_KEYMAP,
  BAD_VIRT_IO_EXP,   // virtIoExp missing / unsupported chip / bad addr / non-SPI LCD
};

const char* profileErrorText(ProfileError e);

// Validate and decode a complete frame [len BE32][CBOR][CRC32 BE32].
// frameLen is the number of valid bytes in `frame` (may exceed the frame).
// On success *cborLen is the CBOR length; on EMPTY/TOO_LARGE/BAD_LENGTH it
// carries the raw length field when available.
ProfileError profileParseFrame(const uint8_t* frame, uint32_t frameLen,
                               CardProfile* out, uint32_t* cborLen);

// Frame length implied by the header (4 + len + 4), or 0 if not sane.
uint32_t profileFrameLength(const uint8_t* header4);

// LcdTap configuration = preset + overrides (dvi size / scaling are left
// for the host to set afterwards).
void profileBuildLcdTapConfig(const CardProfile& p, lcdtap::LcdTapConfig* cfg);

// Helpers for the bus configuration.
int profileFindPort(const PortCfg* ports, uint8_t function);  // LCIO index or -1
// Search the isp table first, then lcio (for RESET / BOOTSEL).
int profileFindIspOrLcioPort(const CardProfile& p, uint8_t function);

}  // namespace wcb
