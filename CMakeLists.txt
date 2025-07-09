# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion RISCV_RPI_2_0_0_5)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(temp_sens C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(dht)

add_executable(temp_sens temp_sens.c)

pico_enable_stdio_uart(temp_sens 1)
pico_enable_stdio_usb(temp_sens 1)

pico_add_extra_outputs(temp_sens)

target_compile_options(temp_sens PRIVATE -Wall)

target_link_libraries(temp_sens dht pico_stdlib hardware_pwm hardware_gpio)
