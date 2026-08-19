cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD weact_studio_rp2350b_core CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(wildcardboy C CXX ASM)

set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(wildcardboy_fw
    src/main.cpp
)

target_include_directories(wildcardboy_fw PRIVATE include)

target_link_libraries(wildcardboy_fw
    pico_stdlib
)

pico_enable_stdio_uart(wildcardboy_fw 1)
pico_add_extra_outputs(wildcardboy_fw)
