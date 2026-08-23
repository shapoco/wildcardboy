#include "card_profile.hpp"

#include <cstdio>
#include <cstring>

#include "qcbor/qcbor_decode.h"
#include "qcbor/qcbor_spiffy_decode.h"

#include "crc32.hpp"

namespace wcb {

const char* profileErrorText(ProfileError e) {
  switch (e) {
    case ProfileError::OK: return "OK";
    case ProfileError::EMPTY: return "empty";
    case ProfileError::BAD_LENGTH: return "bad length";
    case ProfileError::TOO_LARGE: return "too large";
    case ProfileError::CRC_MISMATCH: return "CRC mismatch";
    case ProfileError::CBOR_ERROR: return "CBOR error";
    case ProfileError::BAD_FORMAT: return "bad format";
    case ProfileError::BAD_ID: return "bad id";
    case ProfileError::BAD_NAME: return "bad name";
    case ProfileError::BAD_PORT: return "bad port";
    case ProfileError::BAD_PORT_MODE: return "bad port mode";
    case ProfileError::UNKNOWN_PRESET: return "unknown preset";
    case ProfileError::UNSUPPORTED_LCD_BUS: return "unsupported LCD bus";
    case ProfileError::MISSING_PORT: return "missing port";
    case ProfileError::BAD_KEYMAP: return "bad keymap";
  }
  return "?";
}

static uint32_t be32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

uint32_t profileFrameLength(const uint8_t* header4) {
  uint32_t n = be32(header4);
  if (n == 0 || n > PROFILE_CBOR_MAX) return 0;
  return 4 + n + 4;
}

//-----------------------------------------------------------------------------
// CBOR decoding (QCBOR spiffy decode)
//-----------------------------------------------------------------------------

// Copy a text string into a NUL-terminated buffer; false if it does not fit.
static bool copyText(UsefulBufC s, char* dst, size_t cap) {
  if (s.len > cap) return false;
  memcpy(dst, s.ptr, s.len);
  dst[s.len] = '\0';
  return true;
}

// Resets a "label not found" error and reports whether the label existed.
static bool optional(QCBORDecodeContext* ctx) {
  QCBORError e = QCBORDecode_GetError(ctx);
  if (e == QCBOR_SUCCESS) return true;
  if (e == QCBOR_ERR_LABEL_NOT_FOUND) {
    QCBORDecode_GetAndResetError(ctx);
    return false;
  }
  return false;  // real error stays latched; caller checks later
}

static int64_t getIntOr(QCBORDecodeContext* ctx, const char* label, int64_t dflt) {
  int64_t v = dflt;
  QCBORDecode_GetInt64InMapSZ(ctx, label, &v);
  if (!optional(ctx)) v = dflt;
  return v;
}

// Decode an array of {i,f,m} maps (already positioned inside the parent
// map) into ports[]. Returns a ProfileError for range problems.
static ProfileError decodePorts(QCBORDecodeContext* ctx, const char* label,
                                PortCfg* ports, const char* what) {
  QCBORDecode_EnterArrayFromMapSZ(ctx, label);
  if (!optional(ctx)) return ProfileError::OK;  // no array: all unused
  bool seen[NUM_LCIO] = {};
  for (;;) {
    QCBORItem item;
    QCBORDecode_EnterMap(ctx, &item);
    QCBORError e = QCBORDecode_GetError(ctx);
    if (e == QCBOR_ERR_NO_MORE_ITEMS) {
      QCBORDecode_GetAndResetError(ctx);
      break;
    }
    if (e != QCBOR_SUCCESS) return ProfileError::CBOR_ERROR;
    int64_t i = getIntOr(ctx, "i", -1);
    int64_t f = getIntOr(ctx, "f", 0);
    int64_t m = getIntOr(ctx, "m", 0);
    QCBORDecode_ExitMap(ctx);
    if (QCBORDecode_GetError(ctx) != QCBOR_SUCCESS) return ProfileError::CBOR_ERROR;
    if (i < 0 || i >= NUM_LCIO || !lcioIsValid(static_cast<int>(i))) {
      printf("[profile] %s: LCIO%lld is not a valid port number\n", what, static_cast<long long>(i));
      return ProfileError::BAD_PORT;
    }
    if (seen[i]) {
      printf("[profile] %s: LCIO%lld listed twice\n", what, static_cast<long long>(i));
      return ProfileError::BAD_PORT;
    }
    seen[i] = true;
    if (f < 0 || f > 255 || m < 0 || m > 63) return ProfileError::BAD_PORT;
    ports[i].f = static_cast<uint8_t>(f);
    ports[i].m = static_cast<uint8_t>(m);
  }
  QCBORDecode_ExitArray(ctx);
  return QCBORDecode_GetError(ctx) == QCBOR_SUCCESS ? ProfileError::OK : ProfileError::CBOR_ERROR;
}

static bool knownFunction(uint8_t f) {
  return f == func::UNUSED || f == func::LCD || f == func::TF ||
         (f >= func::BTN_FIRST && f <= func::BTN_LAST) ||
         (f >= func::RESET && f <= func::ISP_MISO);
}

static ProfileError checkPorts(const PortCfg* ports, const char* what, bool ispTable) {
  for (int i = 0; i < NUM_LCIO; ++i) {
    const PortCfg& p = ports[i];
    if (p.f == 0 && p.m == 0) continue;
    if (!knownFunction(p.f)) {
      printf("[profile] %s: LCIO%d: unknown function %u\n", what, i, p.f);
      return ProfileError::BAD_PORT;
    }
    if (ispTable && p.f != 0 && p.f < func::RESET) {
      printf("[profile] %s: LCIO%d: function %u is not an ISP function\n", what, i, p.f);
      return ProfileError::BAD_PORT;
    }
    uint8_t dir = p.m & mode::DIR_MASK;
    if (dir != 0 && dir != mode::INPUT && dir != mode::OUTPUT && dir != mode::OPEN_DRAIN) {
      printf("[profile] %s: LCIO%d: mode %u mixes directions\n", what, i, p.m);
      return ProfileError::BAD_PORT_MODE;
    }
    if ((p.m & mode::PULL_UP) && (p.m & mode::PULL_DOWN)) {
      printf("[profile] %s: LCIO%d: both pulls set\n", what, i);
      return ProfileError::BAD_PORT_MODE;
    }
    if (dir == mode::OPEN_DRAIN && !(p.m & mode::NEGATIVE)) {
      printf("[profile] %s: LCIO%d: open-drain output needs negative logic\n", what, i);
      return ProfileError::BAD_PORT_MODE;
    }
    if (lcioIsPca(i)) {
      // PCA9555 ports: push-pull or open-drain (direction switching) outputs;
      // pulls are fixed by the chip.
      if (dir != mode::OUTPUT && dir != mode::OPEN_DRAIN) {
        printf("[profile] %s: LCIO%d: PCA9555 ports must be OUTPUT or OPEN_DRAIN\n", what, i);
        return ProfileError::BAD_PORT_MODE;
      }
      if (p.f >= func::RESET) {
        printf("[profile] %s: LCIO%d: RESET/BOOTSEL/ISP cannot be on a PCA9555 port\n", what, i);
        return ProfileError::BAD_PORT;
      }
    }
    if (p.f == func::TF) printf("[profile] warning: %s: LCIO%d: TF card I/F is not supported by pretest\n", what, i);
    if (p.f == func::BOOTSEL) printf("[profile] warning: %s: LCIO%d: BOOTSEL is not used by pretest\n", what, i);
    if (p.f == func::LCD && i != 2 && i != 3) {
      printf("[profile] warning: %s: LCIO%d: LCD I/F outside the I2C pair (LCIO2/3)\n", what, i);
    }
  }
  return ProfileError::OK;
}

static ProfileError decodeCbor(const uint8_t* cbor, uint32_t len, CardProfile* out) {
  memset(out, 0, sizeof(*out));
  memset(out->keymap, 0xFF, sizeof(out->keymap));
  out->lcdtapPresetId = lcdtap::ConfigPreset::NUM_PRESETS;

  QCBORDecodeContext ctx;
  UsefulBufC in = {cbor, len};
  QCBORDecode_Init(&ctx, in, QCBOR_DECODE_MODE_NORMAL);
  QCBORDecode_EnterMap(&ctx, nullptr);
  if (QCBORDecode_GetError(&ctx) != QCBOR_SUCCESS) return ProfileError::CBOR_ERROR;

  UsefulBufC s;
  QCBORDecode_GetTextStringInMapSZ(&ctx, "format", &s);
  if (QCBORDecode_GetError(&ctx) != QCBOR_SUCCESS) return ProfileError::BAD_FORMAT;
  if (s.len != 7 || memcmp(s.ptr, "WCBCARD", 7) != 0) return ProfileError::BAD_FORMAT;

  QCBORDecode_GetTextStringInMapSZ(&ctx, "id", &s);
  if (QCBORDecode_GetError(&ctx) != QCBOR_SUCCESS) return ProfileError::BAD_ID;
  if (s.len == 0 || !copyText(s, out->id, ID_MAX)) return ProfileError::BAD_ID;

  QCBORDecode_GetTextStringInMapSZ(&ctx, "name", &s);
  if (optional(&ctx)) {
    if (!copyText(s, out->name, NAME_MAX)) return ProfileError::BAD_NAME;
  } else if (QCBORDecode_GetError(&ctx) != QCBOR_SUCCESS) {
    return ProfileError::CBOR_ERROR;
  }

  // lcio.ports / lcio.useTfCard
  QCBORDecode_EnterMapFromMapSZ(&ctx, "lcio");
  if (optional(&ctx)) {
    ProfileError e = decodePorts(&ctx, "ports", out->lcio, "lcio");
    if (e != ProfileError::OK) return e;
    bool useTf = false;
    QCBORDecode_GetBoolInMapSZ(&ctx, "useTfCard", &useTf);
    if (optional(&ctx)) out->useTfCard = useTf;
    else if (QCBORDecode_GetError(&ctx) != QCBOR_SUCCESS) return ProfileError::CBOR_ERROR;
    QCBORDecode_ExitMap(&ctx);
  }

  // lcdtap.preset / lcdtap.cfg
  QCBORDecode_EnterMapFromMapSZ(&ctx, "lcdtap");
  if (!optional(&ctx)) return ProfileError::UNKNOWN_PRESET;
  QCBORDecode_GetTextStringInMapSZ(&ctx, "preset", &s);
  if (QCBORDecode_GetError(&ctx) != QCBOR_SUCCESS) return ProfileError::UNKNOWN_PRESET;
  if (!copyText(s, out->lcdtapPreset, sizeof(out->lcdtapPreset) - 1)) return ProfileError::UNKNOWN_PRESET;
  QCBORDecode_EnterMapFromMapSZ(&ctx, "cfg");
  if (optional(&ctx)) {
    for (;;) {
      QCBORItem item;
      QCBORDecode_VGetNext(&ctx, &item);
      QCBORError e = QCBORDecode_GetError(&ctx);
      if (e == QCBOR_ERR_NO_MORE_ITEMS) {
        QCBORDecode_GetAndResetError(&ctx);
        break;
      }
      if (e != QCBOR_SUCCESS) return ProfileError::CBOR_ERROR;
      if (item.uLabelType != QCBOR_TYPE_TEXT_STRING || item.uDataType != QCBOR_TYPE_INT64) {
        return ProfileError::CBOR_ERROR;
      }
      char key[32];
      if (!copyText(item.label.string, key, sizeof(key) - 1)) return ProfileError::CBOR_ERROR;
      lcdtap::Configs id = lcdtap::findConfigByKey(key);
      if (id == lcdtap::Configs::NUM_CONFIGS) {
        printf("[profile] warning: lcdtap.cfg: unknown key \"%s\" ignored\n", key);
        continue;
      }
      if (out->lcdtapCfgCount < static_cast<int>(lcdtap::Configs::NUM_CONFIGS)) {
        auto& o = out->lcdtapCfg[out->lcdtapCfgCount++];
        o.key = id;
        int64_t v = item.val.int64;
        o.value = static_cast<int16_t>(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
      }
    }
    QCBORDecode_ExitMap(&ctx);
  }
  QCBORDecode_ExitMap(&ctx);  // lcdtap

  // isp
  QCBORDecode_EnterMapFromMapSZ(&ctx, "isp");
  if (optional(&ctx)) {
    int64_t method = getIntOr(&ctx, "method", 0);
    if (method < 0 || method > 255) return ProfileError::CBOR_ERROR;
    out->ispMethod = static_cast<uint8_t>(method);
    ProfileError e = decodePorts(&ctx, "ports", out->isp, "isp");
    if (e != ProfileError::OK) return e;
    QCBORDecode_ExitMap(&ctx);
  }

  // keymap.map
  QCBORDecode_EnterMapFromMapSZ(&ctx, "keymap");
  if (optional(&ctx)) {
    QCBORDecode_EnterArrayFromMapSZ(&ctx, "map");
    if (optional(&ctx)) {
      for (;;) {
        QCBORItem item;
        QCBORDecode_EnterMap(&ctx, &item);
        QCBORError e = QCBORDecode_GetError(&ctx);
        if (e == QCBOR_ERR_NO_MORE_ITEMS) {
          QCBORDecode_GetAndResetError(&ctx);
          break;
        }
        if (e != QCBOR_SUCCESS) return ProfileError::CBOR_ERROR;
        int64_t src = getIntOr(&ctx, "s", -1);
        int64_t dst = getIntOr(&ctx, "d", -1);
        QCBORDecode_ExitMap(&ctx);
        if (src < 0 || src >= NUM_BUTTONS || dst < 0 || dst >= NUM_BUTTONS) return ProfileError::BAD_KEYMAP;
        if (out->keymap[src] != 0xFF) return ProfileError::BAD_KEYMAP;  // duplicate source
        out->keymap[src] = static_cast<uint8_t>(dst);
      }
      QCBORDecode_ExitArray(&ctx);
    }
    QCBORDecode_ExitMap(&ctx);
  }

  QCBORDecode_ExitMap(&ctx);  // root
  if (QCBORDecode_Finish(&ctx) != QCBOR_SUCCESS) return ProfileError::CBOR_ERROR;
  return ProfileError::OK;
}

//-----------------------------------------------------------------------------
// Semantic checks
//-----------------------------------------------------------------------------

static ProfileError checkProfile(CardProfile* p) {
  ProfileError e = checkPorts(p->lcio, "lcio", false);
  if (e != ProfileError::OK) return e;
  e = checkPorts(p->isp, "isp", true);
  if (e != ProfileError::OK) return e;

  // Preset name -> id (exact match against CONFIG_PRESET_NAMES).
  for (int i = 0; i < static_cast<int>(lcdtap::ConfigPreset::NUM_PRESETS); ++i) {
    if (strcmp(lcdtap::CONFIG_PRESET_NAMES[i], p->lcdtapPreset) == 0) {
      p->lcdtapPresetId = static_cast<lcdtap::ConfigPreset>(i);
      break;
    }
  }
  if (p->lcdtapPresetId == lcdtap::ConfigPreset::NUM_PRESETS) {
    printf("[profile] unknown LcdTap preset \"%s\"\n", p->lcdtapPreset);
    return ProfileError::UNKNOWN_PRESET;
  }

  lcdtap::LcdTapConfig cfg;
  profileBuildLcdTapConfig(*p, &cfg);
  if (cfg.busInterface != lcdtap::BusType::I2C &&
      cfg.busInterface != lcdtap::BusType::SPI_4LINE) {
    printf("[profile] LcdTap bus %s is not supported by pretest (I2C / 4-line SPI only)\n",
           lcdtap::BUS_NAMES[static_cast<int>(cfg.busInterface)]);
    return ProfileError::UNSUPPORTED_LCD_BUS;
  }

  if (p->ispMethod == isp_method::USB_MSC) {
    if (profileFindIspOrLcioPort(*p, func::RESET) < 0 ||
        profileFindIspOrLcioPort(*p, func::BOOTSEL) < 0) {
      printf("[profile] USB ISP needs RESET and BOOTSEL ports\n");
      return ProfileError::MISSING_PORT;
    }
  } else if (p->ispMethod == isp_method::SPI) {
    if (profileFindPort(p->isp, func::ISP_MOSI) < 0 || profileFindPort(p->isp, func::ISP_SCK) < 0 ||
        profileFindPort(p->isp, func::ISP_MISO) < 0 || profileFindIspOrLcioPort(*p, func::RESET) < 0) {
      printf("[profile] SPI ISP needs MOSI/SCK/MISO and RESET ports\n");
      return ProfileError::MISSING_PORT;
    }
  } else if (p->ispMethod != isp_method::UNUSED) {
    printf("[profile] warning: unknown ISP method %u\n", p->ispMethod);
  }
  return ProfileError::OK;
}

//-----------------------------------------------------------------------------
// Public API
//-----------------------------------------------------------------------------

ProfileError profileParseFrame(const uint8_t* frame, uint32_t frameLen,
                               CardProfile* out, uint32_t* cborLen) {
  if (cborLen) *cborLen = 0;
  if (frameLen < 8) return ProfileError::BAD_LENGTH;
  uint32_t n = be32(frame);
  if (cborLen) *cborLen = n;
  if (n == 0xFFFFFFFFu || n == 0) return ProfileError::EMPTY;
  if (n > PROFILE_CBOR_MAX) return ProfileError::TOO_LARGE;
  if (4 + n + 4 > frameLen) return ProfileError::BAD_LENGTH;
  uint32_t stored = be32(frame + 4 + n);
  uint32_t calc = crc32(frame, 4 + n);
  if (stored != calc) {
    printf("[profile] CRC mismatch: stored %08lx computed %08lx\n",
           static_cast<unsigned long>(stored), static_cast<unsigned long>(calc));
    return ProfileError::CRC_MISMATCH;
  }
  ProfileError e = decodeCbor(frame + 4, n, out);
  if (e != ProfileError::OK) return e;
  return checkProfile(out);
}

void profileBuildLcdTapConfig(const CardProfile& p, lcdtap::LcdTapConfig* cfg) {
  lcdtap::ConfigPreset preset = p.lcdtapPresetId;
  if (preset == lcdtap::ConfigPreset::NUM_PRESETS) preset = lcdtap::ConfigPreset::SSD1306;
  lcdtap::getPresetConfig(preset, cfg);
  for (int i = 0; i < p.lcdtapCfgCount; ++i) {
    lcdtap::setConfigValueById(cfg, p.lcdtapCfg[i].key, p.lcdtapCfg[i].value);
  }
  lcdtap::normalizeConfig(cfg);
}

int profileFindPort(const PortCfg* ports, uint8_t function) {
  for (int i = 0; i < NUM_LCIO; ++i) {
    if (ports[i].f == function && (ports[i].m & mode::DIR_MASK) != 0) return i;
  }
  return -1;
}

int profileFindIspOrLcioPort(const CardProfile& p, uint8_t function) {
  int i = profileFindPort(p.isp, function);
  if (i < 0) i = profileFindPort(p.lcio, function);
  return i;
}

}  // namespace wcb
