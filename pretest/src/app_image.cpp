#include "app_image.hpp"

#include <cstring>

#include "ff.h"

namespace wcb {

const char* loadResultName(LoadResult r) {
  switch (r) {
    case LoadResult::OK: return "OK";
    case LoadResult::NOT_FOUND: return "File not found";
    case LoadResult::TOO_LARGE: return "File too large";
    case LoadResult::BAD_HEX: return "Bad HEX file";
    case LoadResult::IO_ERROR: return "TF card read error";
    case LoadResult::EMPTY: return "Empty file";
  }
  return "?";
}

static bool hasHexExt(const char* path) {
  const char* dot = strrchr(path, '.');
  if (!dot) return false;
  return strcasecmp(dot, ".hex") == 0;
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

//-----------------------------------------------------------------------------
// Intel HEX, parsed from a chunked byte stream
//-----------------------------------------------------------------------------

struct HexParser {
  uint8_t* img;
  uint32_t cap;
  uint32_t maxEnd = 0;   // last used byte + 1
  uint32_t base = 0;     // from type 02/04 records (must stay 0)
  bool eof = false;

  char line[600];
  uint32_t lineLen = 0;
  bool inRecord = false;  // after ':'

  // Returns OK while parsing continues; other values abort.
  LoadResult feed(char c) {
    if (c == ':') {
      inRecord = true;
      lineLen = 0;
      return LoadResult::OK;
    }
    if (!inRecord) return LoadResult::OK;  // skip junk between records
    if (c == '\r' || c == '\n') {
      inRecord = false;
      return endRecord();
    }
    if (lineLen >= sizeof(line)) return LoadResult::BAD_HEX;
    line[lineLen++] = c;
    return LoadResult::OK;
  }

  LoadResult endRecord() {
    if (lineLen < 10 || (lineLen & 1)) return LoadResult::BAD_HEX;
    static uint8_t bytes[300];  // keep the stack small
    uint32_t n = lineLen / 2;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < n; ++i) {
      int h = hexVal(line[2 * i]), l = hexVal(line[2 * i + 1]);
      if (h < 0 || l < 0) return LoadResult::BAD_HEX;
      bytes[i] = static_cast<uint8_t>((h << 4) | l);
      sum = static_cast<uint8_t>(sum + bytes[i]);
    }
    if (sum != 0) return LoadResult::BAD_HEX;  // checksum
    uint8_t count = bytes[0];
    uint32_t addr = (static_cast<uint32_t>(bytes[1]) << 8) | bytes[2];
    uint8_t type = bytes[3];
    if (n != 5u + count) return LoadResult::BAD_HEX;
    const uint8_t* data = &bytes[4];

    switch (type) {
      case 0x00: {
        uint32_t start = base + addr;
        if (start + count > cap) return LoadResult::TOO_LARGE;
        memcpy(img + start, data, count);
        if (start + count > maxEnd) maxEnd = start + count;
        return LoadResult::OK;
      }
      case 0x01: eof = true; return LoadResult::OK;
      case 0x02:  // extended segment address
        if (count != 2) return LoadResult::BAD_HEX;
        base = ((static_cast<uint32_t>(data[0]) << 8) | data[1]) << 4;
        return base ? LoadResult::TOO_LARGE : LoadResult::OK;
      case 0x04:  // extended linear address
        if (count != 2) return LoadResult::BAD_HEX;
        base = ((static_cast<uint32_t>(data[0]) << 8) | data[1]) << 16;
        return base ? LoadResult::TOO_LARGE : LoadResult::OK;
      case 0x03:
      case 0x05: return LoadResult::OK;  // start address records: ignore
      default: return LoadResult::BAD_HEX;
    }
  }
};

//-----------------------------------------------------------------------------

static uint8_t chunk[512];

LoadResult ihexLoad(const char* path, uint8_t* img, uint32_t cap,
                    uint32_t* len) {
  memset(img, 0xFF, cap);
  *len = 0;
  FIL f;
  FRESULT fr = f_open(&f, path, FA_READ);
  if (fr == FR_NO_FILE || fr == FR_NO_PATH) return LoadResult::NOT_FOUND;
  if (fr != FR_OK) return LoadResult::IO_ERROR;

  LoadResult result = LoadResult::OK;
  {
    static HexParser hp;
    hp = HexParser{};
    hp.img = img;
    hp.cap = cap;
    while (!hp.eof) {
      UINT rd = 0;
      if (f_read(&f, chunk, sizeof(chunk), &rd) != FR_OK) {
        result = LoadResult::IO_ERROR;
        break;
      }
      if (rd == 0) break;
      for (UINT i = 0; i < rd && result == LoadResult::OK && !hp.eof; ++i) {
        result = hp.feed(static_cast<char>(chunk[i]));
      }
      if (result != LoadResult::OK) break;
    }
    if (result == LoadResult::OK && hp.inRecord) {
      result = hp.endRecord();  // last record without a trailing newline
    }
    if (result == LoadResult::OK) {
      if (hp.maxEnd == 0) result = LoadResult::EMPTY;
      *len = hp.maxEnd;
    }
  }
  f_close(&f);
  return result;
}

LoadResult appImageLoad(const char* path, uint8_t* img, uint32_t cap,
                        uint32_t* len) {
  if (hasHexExt(path)) return ihexLoad(path, img, cap, len);

  memset(img, 0xFF, cap);
  *len = 0;
  FIL f;
  FRESULT fr = f_open(&f, path, FA_READ);
  if (fr == FR_NO_FILE || fr == FR_NO_PATH) return LoadResult::NOT_FOUND;
  if (fr != FR_OK) return LoadResult::IO_ERROR;

  LoadResult result = LoadResult::OK;
  {
    FSIZE_t size = f_size(&f);
    if (size > cap) {
      result = LoadResult::TOO_LARGE;
    } else if (size == 0) {
      result = LoadResult::EMPTY;
    } else {
      UINT rd = 0;
      if (f_read(&f, img, static_cast<UINT>(size), &rd) != FR_OK ||
          rd != size) {
        result = LoadResult::IO_ERROR;
      } else {
        *len = static_cast<uint32_t>(size);
      }
    }
  }

  f_close(&f);
  return result;
}

}  // namespace wcb
