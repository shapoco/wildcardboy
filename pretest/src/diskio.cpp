// FatFs low-level disk I/O glue for the TF card (read-only).

#include <cstdint>

#include "ff.h"
#include "diskio.h"  // needs the ff.h types

#include "sd_spi.hpp"

extern "C" {

DSTATUS disk_status(BYTE pdrv) {
  if (pdrv != 0) return STA_NOINIT;
  return wcb::sdIsReady() ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
  if (pdrv != 0) return STA_NOINIT;
  return wcb::sdInit() ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
  if (pdrv != 0) return RES_PARERR;
  if (!wcb::sdIsReady()) return RES_NOTRDY;
  return wcb::sdReadBlocks(static_cast<uint32_t>(sector), buff, count)
             ? RES_OK
             : RES_ERROR;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE, const BYTE*, LBA_t, UINT) { return RES_WRPRT; }
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
  if (pdrv != 0) return RES_PARERR;
  switch (cmd) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_SIZE:
      *static_cast<WORD*>(buff) = 512;
      return RES_OK;
    case GET_BLOCK_SIZE:
      *static_cast<DWORD*>(buff) = 1;
      return RES_OK;
    default: return RES_PARERR;
  }
}

}  // extern "C"
