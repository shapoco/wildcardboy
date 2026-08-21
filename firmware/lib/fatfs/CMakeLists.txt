# FatFs R0.16 (ChaN) as a static library. ffconf.h in source/ is configured
# for this project (read-only, LFN, exFAT); see README.md.
add_library(fatfs STATIC
    source/ff.c
    source/ffunicode.c
)
target_include_directories(fatfs PUBLIC source)
