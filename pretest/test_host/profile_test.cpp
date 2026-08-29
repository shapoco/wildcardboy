// Host-side test of card_profile: parses cards/TinyJoypad/profile.hex and checks
// the error paths. Build/run with test_host/build.sh.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "card_profile.hpp"
#include "crc32.hpp"

using namespace wcb;

static std::vector<uint8_t> loadHex(const char* path) {
  std::ifstream f(path);
  assert(f && "cannot open hex");
  std::vector<uint8_t> img;
  std::string line;
  uint32_t base = 0;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] != ':') continue;
    auto hx = [&](size_t i) { return static_cast<uint8_t>(std::stoul(line.substr(i, 2), nullptr, 16)); };
    uint8_t n = hx(1), type = hx(7);
    uint32_t addr = (hx(3) << 8) | hx(5);
    if (type == 4) { base = ((hx(9) << 8) | hx(11)) << 16; continue; }
    if (type != 0) continue;
    for (uint8_t i = 0; i < n; ++i) {
      uint32_t a = base + addr + i;
      if (img.size() <= a) img.resize(a + 1, 0xFF);
      img[a] = hx(9 + 2 * i);
    }
  }
  return img;
}

static void patchCrc(std::vector<uint8_t>& frame) {
  uint32_t n = (frame[0] << 24) | (frame[1] << 16) | (frame[2] << 8) | frame[3];
  uint32_t c = crc32(frame.data(), 4 + n);
  frame[4 + n] = c >> 24; frame[5 + n] = c >> 16; frame[6 + n] = c >> 8; frame[7 + n] = c;
}

int main(int argc, char** argv) {
  const char* hexPath = argc > 1 ? argv[1] : "../../cards/TinyJoypad/profile.hex";
  std::vector<uint8_t> frame = loadHex(hexPath);
  printf("frame: %zu bytes\n", frame.size());

  CardProfile p;
  uint32_t n = 0;
  ProfileError e = profileParseFrame(frame.data(), frame.size(), &p, &n);
  printf("parse: %s (cbor %u bytes)\n", profileErrorText(e), n);
  assert(e == ProfileError::OK);
  assert(strcmp(p.id, "TinyJoypad") == 0);
  assert(strcmp(p.name, "Tinyjoypad") == 0);
  assert(p.lcdtapPresetId == lcdtap::ConfigPreset::TINYJOYPAD);
  assert(p.lcdtapCfgCount == 0);
  assert(p.lcio[2].f == func::LCD && p.lcio[2].m == 9);
  assert(p.lcio[5].f == 16 && p.lcio[5].m == 36);
  assert(p.lcio[13].f == func::RESET && p.lcio[13].m == 36);
  assert(p.lcio[0].f == 0 && p.lcio[0].m == 0);
  assert(p.ispMethod == 1);
  assert(strcmp(p.ispMcu, "attiny85") == 0);
  {
    const AvrDevice* d = avrDeviceById(p.ispMcu);
    assert(d && strcmp(d->id, "attiny85") == 0 && d->flashSize == 8192 && d->pageBytes == 64);
    d = avrDeviceById("atmega32u4");
    assert(d && d->flashSize == 32768 && d->pageBytes == 128 && d->signature[1] == 0x95);
    assert(avrDeviceById("z80") == nullptr);
  }
  assert(p.isp[2].f == func::ISP_MOSI && p.isp[2].m == 2);
  assert(p.isp[9].f == func::ISP_MISO && p.isp[9].m == 1);
  assert(p.keymap[0] == 0 && p.keymap[5] == 4 && p.keymap[7] == 4 && p.keymap[8] == 0xFF);
  assert(profileFindPort(p.lcio, func::RESET) == 13);
  assert(profileFindPort(p.isp, func::ISP_SCK) == 3);
  assert(profileFindPort(p.lcio, func::TF) == -1);
  assert(!p.useTfCard);
  assert(p.vsyncHz == VSYNC_HZ_DEFAULT && p.vsyncPulseUs == VSYNC_PULSE_US_DEFAULT);
  assert(profileFindPort(p.lcio, func::VSYNC) == -1);
  assert(lcioIsValid(0) && lcioIsValid(13) && !lcioIsValid(14) && !lcioIsValid(31) &&
         lcioIsValid(32) && lcioIsValid(47) && !lcioIsValid(48));
  assert(!lcioIsValid(63) && lcioIsValid(64) && lcioIsValid(79) && !lcioIsValid(80));
  assert(lcioIsPca(40) && !lcioIsGpio(40) && lcioIsGpio(5));
  assert(lcioIsVirt(64) && lcioIsVirt(79) && !lcioIsVirt(48) && !lcioIsVirt(80));

  lcdtap::LcdTapConfig cfg;
  profileBuildLcdTapConfig(p, &cfg);
  assert(cfg.busInterface == lcdtap::BusType::I2C);
  assert(cfg.controllerFamily == lcdtap::ControllerFamily::SSD1306);
  assert(cfg.i2cSlaveAddr == 0x3C);

  // Erased EEPROM
  std::vector<uint8_t> blank(16, 0xFF);
  assert(profileParseFrame(blank.data(), blank.size(), &p, &n) == ProfileError::EMPTY);

  // CRC corruption
  std::vector<uint8_t> bad = frame;
  bad[10] ^= 1;
  assert(profileParseFrame(bad.data(), bad.size(), &p, &n) == ProfileError::CRC_MISMATCH);

  // Truncated frame
  assert(profileParseFrame(frame.data(), frame.size() - 5, &p, &n) == ProfileError::BAD_LENGTH);

  // Too large
  std::vector<uint8_t> big = frame;
  big[0] = 0; big[1] = 0; big[2] = 0x10; big[3] = 0x01;  // 4097
  assert(profileParseFrame(big.data(), big.size(), &p, &n) == ProfileError::TOO_LARGE);

  // Open-drain with positive logic: LCIO5 m=36 -> m=4. Find the bytes: the
  // port map {"i":5,"f":16,"m":36} encodes m as 0x18 0x24; patch 0x24 -> 0x04.
  std::vector<uint8_t> od = frame;
  {
    const uint8_t pat[] = {0x61, 'm', 0x18, 0x24};
    bool done = false;
    for (size_t i = 4; i + 4 <= od.size() && !done; ++i) {
      if (memcmp(&od[i], pat, 4) == 0) { od[i + 3] = 0x04; done = true; }
    }
    assert(done);
    patchCrc(od);
    assert(profileParseFrame(od.data(), od.size(), &p, &n) == ProfileError::BAD_PORT_MODE);
  }

  // Effective-config path honours overrides.
  CardProfile q = p;
  q.lcdtapCfgCount = 1;
  q.lcdtapCfg[0] = {lcdtap::Configs::BUS_INTERFACE, static_cast<int16_t>(lcdtap::BusType::PARALLEL)};
  profileBuildLcdTapConfig(q, &cfg);
  assert(cfg.busInterface == lcdtap::BusType::PARALLEL);

  // A PP-style frame passed on the command line (optional second argument):
  // must parse, use the TF card and have USB ISP with RESET + BOOTSEL.
  if (argc > 2) {
    std::vector<uint8_t> pp = loadHex(argv[2]);
    CardProfile r;
    ProfileError pe = profileParseFrame(pp.data(), pp.size(), &r, &n);
    printf("pp parse: %s (cbor %u bytes) id=%s useTfCard=%d isp=%u\n", profileErrorText(pe), n, r.id,
           r.useTfCard, r.ispMethod);
    assert(pe == ProfileError::OK);
    assert(r.useTfCard);
    assert(r.ispMethod == isp_method::USB_MSC);
    assert(profileFindIspOrLcioPort(r, func::RESET) == 13);
    assert(profileFindIspOrLcioPort(r, func::BOOTSEL) == 12);
    // Keys on the card PCA9555 are open-drain active-low in the current profiles.
    assert(r.lcio[32].f == 16 && (r.lcio[32].m & mode::DIR_MASK) == mode::OPEN_DRAIN);
    profileBuildLcdTapConfig(r, &cfg);
    assert(cfg.busInterface == lcdtap::BusType::SPI_4LINE);
  }

  // An ESPboy-style frame (optional third argument): UART ISP + virtual
  // I/O expander (spec/03 additions).
  if (argc > 3) {
    std::vector<uint8_t> eb = loadHex(argv[3]);
    CardProfile r;
    ProfileError ee = profileParseFrame(eb.data(), eb.size(), &r, &n);
    printf("espboy parse: %s (cbor %u bytes) id=%s vio=%d isp=%u\n", profileErrorText(ee), n, r.id,
           r.useVirtIoExp, r.ispMethod);
    assert(ee == ProfileError::OK);
    assert(strcmp(r.id, "ESPboy") == 0);
    assert(r.useVirtIoExp && !r.useTfCard);
    assert(strcmp(r.virtIoExpChip, "mcp23017") == 0 && r.virtIoExpAddr == 32);
    assert(r.lcio[6].f == func::I2C_SLAVE && r.lcio[7].f == func::I2C_SLAVE);
    assert(r.lcio[64].f == 16 && r.lcio[64].m == 36);  // GPA0 = LEFT
    assert(r.lcio[65].f == 18 && r.lcio[67].f == 17);  // GPA1 = UP, GPA3 = RIGHT
    assert(r.lcio[70].f == 26 && r.lcio[71].f == 27);  // GPA6/7 = L/R bumper
    assert(r.lcio[72].f == 0 && r.lcio[72].m == 0);    // GPB0 unassigned
    assert(r.ispMethod == isp_method::UART_ESP);
    assert(strcmp(r.ispMcu, "esp8266") == 0);
    assert(r.isp[10].f == func::ISP_UART_TX && r.isp[10].m == 2);
    assert(r.isp[11].f == func::ISP_UART_RX && r.isp[11].m == 9);
    assert(profileFindIspOrLcioPort(r, func::RESET) == 13);
    assert(profileFindIspOrLcioPort(r, func::BOOTSEL) == 12);
    assert(r.keymap[10] == 10 && r.keymap[6] == 0xFF);
    assert(r.lcdtapPresetId == lcdtap::ConfigPreset::ESPBOY);
    profileBuildLcdTapConfig(r, &cfg);
    assert(cfg.busInterface == lcdtap::BusType::SPI_4LINE);
    assert(cfg.buffWidth == 136 && cfg.buffHeight == 136);
    assert(cfg.trimWidth == 128 && cfg.trimHeight == 128);

    // useVirtIoExp true (0xF5) -> false (0xF4): virtual ports without the
    // flag must be rejected.
    std::vector<uint8_t> novio = eb;
    {
      const uint8_t pat[] = {0x6C, 'u', 's', 'e', 'V', 'i', 'r', 't', 'I', 'o', 'E', 'x', 'p', 0xF5};
      bool done = false;
      for (size_t i = 4; i + sizeof(pat) <= novio.size() && !done; ++i) {
        if (memcmp(&novio[i], pat, sizeof(pat)) == 0) { novio[i + sizeof(pat) - 1] = 0xF4; done = true; }
      }
      assert(done);
      patchCrc(novio);
      assert(profileParseFrame(novio.data(), novio.size(), &r, &n) == ProfileError::BAD_PORT);
    }

    // isp port f:38 (0x18 0x26) -> f:0: the UART TX requirement must trip.
    std::vector<uint8_t> notx = eb;
    {
      const uint8_t pat[] = {0x61, 'f', 0x18, 0x26};
      bool done = false;
      for (size_t i = 4; i + sizeof(pat) <= notx.size() && !done; ++i) {
        if (memcmp(&notx[i], pat, sizeof(pat)) == 0) { notx[i + 3] = 0x00; done = true; }
      }
      assert(done);
      patchCrc(notx);
      assert(profileParseFrame(notx.data(), notx.size(), &r, &n) == ProfileError::MISSING_PORT);
    }
  }

  // A Xiamocon-style frame (optional fourth argument): PCA9555 virtual
  // expander + TF card + USB ISP, display reset on a virtual port.
  if (argc > 4) {
    std::vector<uint8_t> xm = loadHex(argv[4]);
    CardProfile r;
    ProfileError xe = profileParseFrame(xm.data(), xm.size(), &r, &n);
    printf("xiamocon parse: %s (cbor %u bytes) id=%s vio=%d tf=%d isp=%u\n", profileErrorText(xe),
           n, r.id, r.useVirtIoExp, r.useTfCard, r.ispMethod);
    assert(xe == ProfileError::OK);
    assert(strcmp(r.id, "XiamoconRP") == 0);
    assert(r.useVirtIoExp && r.useTfCard);
    assert(strcmp(r.virtIoExpChip, "pca9555") == 0 && r.virtIoExpAddr == 34);
    assert(r.lcio[6].f == func::I2C_SLAVE && r.lcio[7].f == func::I2C_SLAVE);
    assert(r.lcio[69].f == func::LCD && r.lcio[69].m == (mode::INPUT | mode::NEGATIVE));  // display reset (P0_5)
    assert(r.lcio[71].f == 26 && r.lcio[71].m == 36);  // function switch = L bumper (P0_7)
    assert(r.lcio[72].f == 21 && r.lcio[73].f == 20);  // P1_0 = B, P1_1 = A
    assert(r.lcio[74].f == 22 && r.lcio[79].f == 17);  // P1_2 = X, P1_7 = RIGHT
    assert(r.keymap[10] == 10 && r.keymap[11] == 10);  // both bumpers -> function switch
    assert(r.ispMethod == isp_method::USB_MSC);
    assert(strcmp(r.ispMcu, "rp2350") == 0);
    assert(profileFindIspOrLcioPort(r, func::RESET) == 13);
    assert(profileFindIspOrLcioPort(r, func::BOOTSEL) == 12);
    assert(r.lcdtapPresetId == lcdtap::ConfigPreset::XIAMOCON);
    profileBuildLcdTapConfig(r, &cfg);
    assert(cfg.busInterface == lcdtap::BusType::SPI_4LINE);
    assert(cfg.buffWidth == 240 && cfg.buffHeight == 240);

    // INPUT on a virtual key port is rejected (INPUT is display-reset only):
    // patch {i:72,f:21,m:36} -> m:33 (INPUT | NEGATIVE).
    std::vector<uint8_t> vin = xm;
    {
      const uint8_t pat[] = {0x61, 'i', 0x18, 0x48, 0x61, 'f', 0x15, 0x61, 'm', 0x18, 0x24};
      bool done = false;
      for (size_t i = 4; i + sizeof(pat) <= vin.size() && !done; ++i) {
        if (memcmp(&vin[i], pat, sizeof(pat)) == 0) { vin[i + 10] = 0x21; done = true; }
      }
      assert(done);
      patchCrc(vin);
      assert(profileParseFrame(vin.data(), vin.size(), &r, &n) == ProfileError::BAD_PORT_MODE);
    }

    // Unknown expander chip is a hard error: patch "pca9555" -> "pca9556".
    std::vector<uint8_t> chip = xm;
    {
      const uint8_t pat[] = {0x67, 'p', 'c', 'a', '9', '5', '5', '5'};
      bool done = false;
      for (size_t i = 4; i + sizeof(pat) <= chip.size() && !done; ++i) {
        if (memcmp(&chip[i], pat, sizeof(pat)) == 0) { chip[i + 7] = '6'; done = true; }
      }
      assert(done);
      patchCrc(chip);
      assert(profileParseFrame(chip.data(), chip.size(), &r, &n) == ProfileError::BAD_VIRT_IO_EXP);
    }
  }

  // A PicoSystem-style frame (optional fifth argument): 8-bit parallel LCD
  // (on-card deserializer), push-pull buttons, no vsync member.
  if (argc > 5) {
    std::vector<uint8_t> ps = loadHex(argv[5]);
    CardProfile r;
    ProfileError se = profileParseFrame(ps.data(), ps.size(), &r, &n);
    printf("picosystem parse: %s (cbor %u bytes) id=%s isp=%u\n",
           profileErrorText(se), n, r.id, r.ispMethod);
    assert(se == ProfileError::OK);
    assert(strcmp(r.id, "PicoSystem") == 0);
    assert(!r.useTfCard && !r.useVirtIoExp);
    for (int i = 0; i <= 11; ++i) assert(r.lcio[i].f == func::LCD);
    assert(r.lcio[0].m == 41 && r.lcio[2].m == 1 && r.lcio[11].m == 1);
    // No vsync member: defaults stay, no VSYNC port.
    assert(r.vsyncHz == VSYNC_HZ_DEFAULT && r.vsyncPulseUs == VSYNC_PULSE_US_DEFAULT);
    assert(profileFindPort(r.lcio, func::VSYNC) == -1);
    // Buttons: push-pull + negative (the SDK never enables its pull-ups).
    assert(r.lcio[32].f == 16 && r.lcio[32].m == 34 && r.lcio[39].m == 34);
    assert(r.ispMethod == isp_method::USB_MSC);
    assert(strcmp(r.ispMcu, "rp2040") == 0);
    assert(profileFindIspOrLcioPort(r, func::RESET) == 13);
    assert(profileFindIspOrLcioPort(r, func::BOOTSEL) == 12);
    assert(r.lcdtapPresetId == lcdtap::ConfigPreset::PICOSYSTEM);
    profileBuildLcdTapConfig(r, &cfg);
    assert(cfg.busInterface == lcdtap::BusType::PARALLEL);
    assert(cfg.buffWidth == 240 && cfg.buffHeight == 240);
  }

  printf("ok\n");
  return 0;
}
