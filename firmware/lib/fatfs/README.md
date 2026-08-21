# FatFs R0.16

Generic FAT/exFAT filesystem module by ChaN (http://elm-chan.org/fsw/ff/).
Source: `ff16.zip` from elm-chan.org. Only `source/` (without the sample
`diskio.c` / `ffsystem.c`) and `LICENSE.txt` are included.

The low-level disk I/O (`disk_initialize`, `disk_read`, ..., `get_fattime`)
is provided by the application (see `pretest/src/diskio.cpp`).

## Local changes to `source/ffconf.h`

| option | value | note |
|---|---|---|
| `FF_FS_READONLY` | 1 | read only (no f_write / f_mkdir ...) |
| `FF_CODE_PAGE` | 437 | |
| `FF_USE_LFN` | 2 | long file names, buffer on stack |
| `FF_FS_EXFAT` | 1 | SDXC cards |
| `FF_FS_NORTC` | 1 | no RTC (timestamps unused in read-only mode) |
| `FF_NORTC_YEAR` | 2026 | |

Everything else is at the upstream default.
