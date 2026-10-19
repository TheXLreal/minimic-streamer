cmake_minimum_required(VERSION 3.13)
# Board: one of pico, pico_w, pico2, pico2_w. Default pico2_w. Override: -DPICO_BOARD=pico2
set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(minimic_streamer C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

# The USB audio (UAC1) needs TinyUSB 0.20.0 or newer, but the Pico SDK (up to at least 2.3.1) ships 0.18.0.
if(PICO_TINYUSB_PATH)
    set(MINIMIC_TINYUSB_DIR ${PICO_TINYUSB_PATH})
else()
    set(MINIMIC_TINYUSB_DIR ${PICO_SDK_PATH}/lib/tinyusb)
endif()
file(STRINGS ${MINIMIC_TINYUSB_DIR}/src/tusb_option.h MINIMIC_TUSB_VER REGEX "#define TUSB_VERSION_(MAJOR|MINOR) ")
string(REGEX REPLACE ".*MAJOR +([0-9]+).*MINOR +([0-9]+).*" "\\1.\\2" MINIMIC_TUSB_VER "${MINIMIC_TUSB_VER}")
if(MINIMIC_TUSB_VER VERSION_LESS 0.20)
    message(FATAL_ERROR "TinyUSB ${MINIMIC_TUSB_VER} found in ${MINIMIC_TINYUSB_DIR}, but 0.20.0 or newer is needed "
        "(UAC1 audio). Update it with:\n"
        "  cd ${MINIMIC_TINYUSB_DIR} && git fetch --depth 1 origin tag 0.20.0 && git checkout 0.20.0")
endif()

# the codec lives in ../codec (portable C, shared with the PC tool)
set(CODEC_DIR ${CMAKE_CURRENT_LIST_DIR}/../codec)
add_executable(minimic_streamer
    main.c
    nrf24.c
    radio.c
    usb_audio.c
    cdc.c
    board.c
    usb_descriptors.c
    ${CODEC_DIR}/encoder.c)
target_include_directories(minimic_streamer PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CODEC_DIR})

set(MINIMIC_LIBS
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_watchdog
    tinyusb_device
    pico_usb_reset_interface_headers)

# The "W" boards drive the LED through the wireless chip; the others use a plain GPIO LED (board.c).
if(PICO_BOARD STREQUAL "pico_w" OR PICO_BOARD STREQUAL "pico2_w")
    list(APPEND MINIMIC_LIBS pico_cyw43_arch_none)
    target_compile_definitions(minimic_streamer PRIVATE MINIMIC_HAS_CYW43_LED=1)
else()
    target_compile_definitions(minimic_streamer PRIVATE MINIMIC_HAS_CYW43_LED=0)
endif()

target_link_libraries(minimic_streamer ${MINIMIC_LIBS})
target_compile_options(minimic_streamer PRIVATE -O2 -Wall)
pico_enable_stdio_usb(minimic_streamer 0)
pico_enable_stdio_uart(minimic_streamer 0)
pico_add_extra_outputs(minimic_streamer)
