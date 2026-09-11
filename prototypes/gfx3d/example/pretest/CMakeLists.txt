cmake_minimum_required(VERSION 3.13)

# gfx3d performance check on the pretest host shell (prototype board).
# Reuses the pretest board support (pin map, clock, keypad I2C, LCD PIO)
# directly from ../../../../pretest; the logic card interface is not used.

set(PICO_BOARD weact_studio_rp2350b_core CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(gfx3d_pretest C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

# pico_enable_stdio_usb() silently does nothing without TinyUSB.
if(NOT TARGET tinyusb_device)
    message(WARNING "TinyUSB is not available in the Pico SDK: the USB CDC stats output is "
        "disabled (the on-screen stats still work). Run 'git submodule update --init lib/tinyusb' "
        "in ${PICO_SDK_PATH} to enable it.")
endif()

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../..)
set(PRETEST_DIR ${REPO_ROOT}/pretest)
set(GFX3D_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
set(LCDTAP_DIR ${REPO_ROOT}/submodule/lcdtap)

option(GFX3D_PERSPECTIVE_CORRECT_UV "Perspective-correct texture coordinates" OFF)

add_executable(gfx3d_pretest
    src/main.cpp
    src/lcd_ili9488.cpp
    src/overlay.cpp
    ${GFX3D_DIR}/example/common/scene.cpp
    ${GFX3D_DIR}/lib/src/gfx3d.cpp
    # pretest board support (used unmodified)
    ${PRETEST_DIR}/src/aux_i2c.cpp
    ${PRETEST_DIR}/src/sys_clock.cpp
)

pico_generate_pio_header(gfx3d_pretest ${PRETEST_DIR}/src/lcd8080.pio)

target_include_directories(gfx3d_pretest PRIVATE
    src
    ${GFX3D_DIR}/lib/include
    ${GFX3D_DIR}/example/common
    ${PRETEST_DIR}/include
    ${PRETEST_DIR}/src
    # Header-only use of the LcdTap 8x16 font (the library is not linked).
    ${LCDTAP_DIR}/lib/include
)

target_link_libraries(gfx3d_pretest
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_i2c
    hardware_gpio
    hardware_pll
    hardware_vreg
    hardware_clocks
)

target_compile_definitions(gfx3d_pretest PRIVATE
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=1000
    PICO_STACK_SIZE=0x1000
)
if(GFX3D_PERSPECTIVE_CORRECT_UV)
    target_compile_definitions(gfx3d_pretest PRIVATE GFX3D_PERSPECTIVE_CORRECT_UV=1)
endif()

# Run everything from SRAM so XIP cache misses do not skew the timings.
pico_set_binary_type(gfx3d_pretest copy_to_ram)

# Debug output over the native USB port. UART stdio stays off: the board's
# default UART pins (GPIO12/13) are logic card I/O.
pico_enable_stdio_usb(gfx3d_pretest 1)
pico_enable_stdio_uart(gfx3d_pretest 0)

pico_add_extra_outputs(gfx3d_pretest)
