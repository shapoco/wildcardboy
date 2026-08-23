#include "crc32.hpp"

namespace wcb {

static uint32_t sTable[256];
static bool sTableReady = false;

static void initTable() {
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t c = i;
    for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    sTable[i] = c;
  }
  sTableReady = true;
}

uint32_t crc32(const uint8_t* data, size_t len, uint32_t seed) {
  if (!sTableReady) initTable();
  uint32_t c = seed ^ 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) c = sTable[(c ^ data[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

}  // namespace wcb
