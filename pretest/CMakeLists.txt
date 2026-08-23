cmake_minimum_required(VERSION 3.20)  # lcdtap/lib requires 3.20

set(PICO_BOARD weact_studio_rp2350b_core CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(wildcardboy_pretest C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

set(LCDTAP_DIR ${CMAKE_CURRENT_LIST_DIR}/../submodule/lcdtap)

# LcdTap core library (MCU-agnostic). Must come after pico_sdk_init() so it
# can pick up hardware_interp_headers.
add_subdirectory(${LCDTAP_DIR}/lib lcdtap_lib)

# FatFs (read-only config), shared with firmware/.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../firmware/lib/fatfs fatfs)

# QCBOR (card profile decoding). Integer-only build.
set(QCBOR_DISABLE_FLOAT_HW_USE ON CACHE BOOL "" FORCE)
set(USEFULBUF_DISABLE_ALL_FLOAT ON CACHE BOOL "" FORCE)
set(QCBOR_DISABLE_TAGS ON CACHE BOOL "" FORCE)
set(QCBOR_DISABLE_INDEFINITE_LENGTH_STRINGS ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../submodule/QCBOR qcbor)

add_executable(pretest
    src/main.cpp
    src/ili9488.cpp
    src/lcd_pump.cpp
    src/text_draw.cpp
    src/aux_i2c.cpp
    src/card_io.cpp
    src/crc32.cpp
    src/card_profile.cpp
    src/card_eeprom.cpp
    src/sd_spi.cpp
    src/diskio.cpp
    src/isp_tiny85.cpp
    src/app_image.cpp
    src/ui_menu.cpp
    # RP2350 hardware I2C slave -> ring buffer -> LcdTap (reused from the
    # LcdTap pico2 example; self-contained).
    ${LCDTAP_DIR}/example/pico2_common/src/i2c_slave.cpp
)

pico_generate_pio_header(pretest ${CMAKE_CURRENT_LIST_DIR}/src/lcd8080.pio)

target_include_directories(pretest PRIVATE
    include
    ${LCDTAP_DIR}/example/pico2_common/include
)

target_link_libraries(pretest
    pico_stdlib
    lcdtap
    fatfs
    qcbor
    hardware_pio
    hardware_spi
    hardware_dma
    hardware_i2c
    hardware_irq
    hardware_gpio
)

# Wait up to 3 s for a USB CDC host so early boot logs are not lost.
target_compile_definitions(pretest PRIVATE
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=3000
    # FatFs (LFN on stack) needs more than the 2 KB default; 4 KB is the
    # most the core-0 stack region (SCRATCH_Y) can hold.
    PICO_STACK_SIZE=0x1000
)

# Debug output over the native USB port. The board's default UART pins
# (GPIO12/13) collide with LCIO12/LCIO13 (ATtiny85 RESET), so UART stdio
# must stay disabled.
pico_enable_stdio_usb(pretest 1)
pico_enable_stdio_uart(pretest 0)

pico_add_extra_outputs(pretest)
