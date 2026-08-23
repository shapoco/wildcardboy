#pragma once

// SD/TF card over SPI0 (HTF_* pins), read-only. Block device for FatFs.

#include <cstdint>

namespace wcb {

enum class SdCardType : uint8_t { NONE, SD_V1, SD_V2_SC, SD_V2_HC };

// Drive LCTF_ENAX high (TF card to the host) and set up SPI0. Call once
// at boot; does not touch the card.
void sdBusInit();

// Hand the TF card to the logic card: HTF_* pins Hi-Z, LCTF_ENAX low.
void sdBusRelease();
// Take the TF card back: LCTF_ENAX high, SPI0 re-enabled (card re-detected
// on the next sdInit()).
void sdBusAcquire();
bool sdBusOwnedByHost();

// (Re)initialize the card. Returns false if no card answers. Safe to call
// again after a card swap.
bool sdInit();

bool sdIsReady();
void sdMarkNotReady();  // after an I/O error: forces sdInit() next time
SdCardType sdCardType();
const char* sdCardTypeName();

// Read `count` 512-byte blocks starting at block `lba`.
bool sdReadBlocks(uint32_t lba, uint8_t* buf, uint32_t count);

}  // namespace wcb
