#pragma once

#include <cstddef>
#include <cstdint>

namespace wcb {

// zlib-compatible CRC-32 (poly 0xEDB88320 reflected, init/xorout 0xFFFFFFFF).
uint32_t crc32(const uint8_t* data, size_t len, uint32_t seed = 0);

}  // namespace wcb
