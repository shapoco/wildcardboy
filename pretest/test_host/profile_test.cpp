// Host-side test of card_profile: parses cards/TJP/profile.hex and checks
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
  const char* hexPath = argc > 1 ? argv[1] : "../../cards/TJP/profile.hex";
  std::vector<uint8_t> frame = loadHex(hexPath);
  printf("frame: %zu bytes\n", frame.size());

  CardProfile p;
  uint32_t n = 0;
  ProfileError e = profileParseFrame(frame.data(), frame.size(), &p, &n);
  printf("parse: %s (cbor %u bytes)\n", profileErrorText(e), n);
  assert(e == ProfileError::OK);
  assert(strcmp(p.id, "TJP") == 0);
  assert(strcmp(p.name, "Tinyjoypad") == 0);
  assert(p.lcdtapPresetId == lcdtap::ConfigPreset::TINYJOYPAD);
  assert(p.lcdtapCfgCount == 0);
  assert(p.lcio[2].f == func::LCD && p.lcio[2].m == 9);
  assert(p.lcio[5].f == 16 && p.lcio[5].m == 36);
  assert(p.lcio[13].f == func::RESET && p.lcio[13].m == 36);
  assert(p.lcio[0].f == 0 && p.lcio[0].m == 0);
  assert(p.ispMethod == 1);
  assert(p.ispMcu[0] == '\0');  // TJP profile predates isp.mcu
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
  assert(lcioIsValid(0) && lcioIsValid(13) && !lcioIsValid(14) && !lcioIsValid(31) &&
         lcioIsValid(32) && lcioIsValid(47) && !lcioIsValid(48));
  assert(lcioIsPca(40) && !lcioIsGpio(40) && lcioIsGpio(5));

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
    assert(r.lcio[32].f == 16 && (r.lcio[32].m & mode::DIR_MASK) == mode::OUTPUT);
    profileBuildLcdTapConfig(r, &cfg);
    assert(cfg.busInterface == lcdtap::BusType::SPI_4LINE);
  }

  printf("ok\n");
  return 0;
}
