#pragma once

// Loads an application image for the card MCU from the TF card.
//   *.hex / *.HEX : Intel HEX (record types 00/01; 02/04 accepted only for
//                   segment/linear base 0)
//   anything else : raw binary
// The buffer is pre-filled with 0xFF; *len = last used byte + 1.

#include <cstdint>

namespace wcb {

enum class LoadResult : uint8_t {
  OK,
  NOT_FOUND,
  TOO_LARGE,
  BAD_HEX,
  IO_ERROR,
  EMPTY,
};

LoadResult appImageLoad(const char* path, uint8_t* img, uint32_t cap,
                        uint32_t* len);

// Intel HEX file -> image (0xFF filled). *len = last used byte + 1.
LoadResult ihexLoad(const char* path, uint8_t* img, uint32_t cap,
                    uint32_t* len);

const char* loadResultName(LoadResult r);

}  // namespace wcb
