#include "isp_usb.hpp"

#include <cstdio>
#include <cstring>

#include "ff.h"
#include "pico/stdlib.h"

#include "card_io.hpp"
#include "core1.hpp"
#include "usb_host.hpp"

namespace wcb {

static constexpr uint32_t UF2_BLOCK = 512;
static constexpr uint32_t CHUNK_BLOCKS = 8;  // 4 KB per write10
static constexpr uint32_t UF2_MAGIC0 = 0x0A324655u;
static constexpr uint32_t UF2_MAGIC1 = 0x9E5D5157u;
static constexpr uint32_t UF2_MAGIC_END = 0x0AB16F30u;
// First LBA used on the bootrom's virtual disk. Any sector written with a
// valid UF2 block is consumed regardless of position; the data area is used
// so nothing collides with the fake FAT/root directory.
static constexpr uint32_t UF2_LBA_BASE = 0x100;
static constexpr uint32_t MOUNT_TIMEOUT_MS = 5000;
static constexpr uint32_t WRITE_TIMEOUT_MS = 5000;
static constexpr uint32_t REBOOT_TIMEOUT_MS = 10000;

static uint8_t sChunk[UF2_BLOCK * CHUNK_BLOCKS];

static inline uint32_t rd32(const uint8_t* p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static bool isUf2Block(const uint8_t* b) {
  return rd32(b) == UF2_MAGIC0 && rd32(b + 4) == UF2_MAGIC1 && rd32(b + 508) == UF2_MAGIC_END;
}

static void finish() {
  cardBootselRelease();
  cardResetAssert();
  core1Call(Core1Cmd::USB_STOP);
}

const char* ispUsbProgram(const char* path, IspUsbProgressFn progress, void* user) {
  if (!cardHasReset() || !cardHasBootsel()) return "Card has no RESET/BOOTSEL";

  FIL f;
  if (f_open(&f, path, FA_READ) != FR_OK) return "Cannot open file";
  const uint32_t size = static_cast<uint32_t>(f_size(&f));
  if (size == 0 || (size % UF2_BLOCK) != 0) {
    f_close(&f);
    return "Not a UF2 file";
  }
  const uint32_t totalBlocks = size / UF2_BLOCK;

  // 1. Into BOOTSEL: BOOTSEL low while RESET is released.
  progress("BOOTSEL", 0, user);
  cardResetAssert();
  cardBootselAssert();
  sleep_ms(10);
  cardResetRelease();
  sleep_ms(200);
  cardBootselRelease();

  // 2. USB host on core 1, wait for the MSC device.
  core1Call(Core1Cmd::USB_START);
  if (usbHostInitFailed()) {
    f_close(&f);
    finish();
    return "USB host init failed";
  }
  printf("[isp-usb] waiting for the MSC device\n");
  absolute_time_t deadline = make_timeout_time_ms(MOUNT_TIMEOUT_MS);
  bool attachedSeen = false;
  while (!usbHostMscMounted() && !time_reached(deadline)) {
    sleep_ms(20);
    if (!attachedSeen && usbHostDeviceAttached()) {
      attachedSeen = true;
      printf("[isp-usb] device attached\n");
    }
    uint32_t left = static_cast<uint32_t>(absolute_time_diff_us(get_absolute_time(), deadline) / 1000);
    progress("BOOTSEL", static_cast<int>(100 - left * 100 / MOUNT_TIMEOUT_MS), user);
  }
  if (!usbHostMscMounted()) {
    f_close(&f);
    finish();
    return usbHostDeviceAttached() ? "USB device is not MSC" : "MCU not in BOOTSEL mode";
  }
  printf("[isp-usb] MSC mounted: %lu blocks x %lu B\n",
         static_cast<unsigned long>(usbHostMscBlockCount()),
         static_cast<unsigned long>(usbHostMscBlockSize()));
  if (usbHostMscBlockSize() != UF2_BLOCK) {
    f_close(&f);
    finish();
    return "Unexpected USB block size";
  }

  // 3. Stream the UF2 blocks.
  progress("Write", 0, user);
  uint32_t blk = 0;
  const char* err = nullptr;
  while (blk < totalBlocks) {
    uint32_t n = totalBlocks - blk;
    if (n > CHUNK_BLOCKS) n = CHUNK_BLOCKS;
    UINT rd = 0;
    if (f_read(&f, sChunk, n * UF2_BLOCK, &rd) != FR_OK || rd != n * UF2_BLOCK) {
      err = "TF card read error";
      break;
    }
    for (uint32_t i = 0; i < n; ++i) {
      if (!isUf2Block(sChunk + i * UF2_BLOCK)) {
        err = "Not a UF2 file";
        break;
      }
    }
    if (err) break;
    if (!usbHostWriteBlocks(UF2_LBA_BASE + blk, sChunk, n, WRITE_TIMEOUT_MS)) {
      // The device may detach right after the last blocks (reboot); that
      // is only an error if blocks are still outstanding.
      if (!usbHostMscMounted() && blk + n >= totalBlocks) break;
      err = "USB write failed";
      break;
    }
    blk += n;
    progress("Write", static_cast<int>(blk * 100 / totalBlocks), user);
  }
  f_close(&f);
  if (err) {
    printf("[isp-usb] %s at block %lu/%lu\n", err, static_cast<unsigned long>(blk),
           static_cast<unsigned long>(totalBlocks));
    finish();
    return err;
  }

  // 4. The bootrom reboots once every block has been written; wait for the
  //    detach, which also means the flash programming has completed.
  progress("Reboot", 0, user);
  deadline = make_timeout_time_ms(REBOOT_TIMEOUT_MS);
  while (usbHostMscMounted() && !time_reached(deadline)) {
    sleep_ms(20);
    uint32_t left = static_cast<uint32_t>(absolute_time_diff_us(get_absolute_time(), deadline) / 1000);
    progress("Reboot", static_cast<int>(100 - left * 100 / REBOOT_TIMEOUT_MS), user);
  }
  if (usbHostMscMounted()) {
    printf("[isp-usb] warning: device did not detach after the UF2 transfer\n");
  } else {
    printf("[isp-usb] device detached (flash write complete)\n");
  }
  progress("Reboot", 100, user);

  // 5. Back to the stopped state.
  finish();
  printf("[isp-usb] %lu blocks written\n", static_cast<unsigned long>(totalBlocks));
  return nullptr;
}

}  // namespace wcb
