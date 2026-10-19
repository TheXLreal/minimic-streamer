cmake_minimum_required(VERSION 3.20)
project(minimic_nrf5340 C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(NRFX ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nrfx)
set(TUSB ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinyusb)
set(CMSIS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmsis/CMSIS/Core/Include)
foreach(dep ${NRFX}/mdk/nrf.h ${TUSB}/src/tusb.c ${CMSIS}/core_cm33.h)
  if(NOT EXISTS ${dep})
    message(FATAL_ERROR "Missing ${dep}. Run tools/build-nrf5340.ps1 first.")
  endif()
endforeach()
function(core_target name core)
  add_executable(${name}
    ${NRFX}/mdk/gcc_startup_nrf5340_${core}.S
    ${NRFX}/mdk/system_nrf5340_${core}.c
    nrf5340/platform.c)
  set_target_properties(${name} PROPERTIES SUFFIX ".elf")
  target_include_directories(${name} PRIVATE nrf5340 ${NRFX} ${NRFX}/mdk
    ${NRFX}/drivers/include ${CMSIS})
  target_compile_definitions(${name} PRIVATE __STARTUP_CLEAR_BSS __START=main
    __STACK_SIZE=16384 __HEAP_SIZE=0 NRF5340_XXAA)
  target_compile_options(${name} PRIVATE -mthumb -O2 -g3 -ffunction-sections
    -fdata-sections -fno-strict-aliasing -Wall -Wextra -Werror=implicit-function-declaration)
  target_link_options(${name} PRIVATE -mthumb -nostartfiles --specs=nano.specs
    --specs=nosys.specs -Wl,--gc-sections -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${name}.map
    -L${NRFX}/mdk -T${CMAKE_CURRENT_SOURCE_DIR}/nrf5340/${core}.ld)
  target_link_libraries(${name} PRIVATE m)
  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${name}> ${name}.hex)
endfunction()
core_target(minimic_app application)
target_compile_definitions(minimic_app PRIVATE NRF5340_XXAA_APPLICATION NRF_APPLICATION)
target_compile_options(minimic_app PRIVATE -mcpu=cortex-m33 -mfloat-abi=hard -mfpu=fpv5-sp-d16)
target_link_options(minimic_app PRIVATE -mcpu=cortex-m33 -mfloat-abi=hard -mfpu=fpv5-sp-d16)
target_include_directories(minimic_app PRIVATE ${TUSB}/src firmware codec)
target_sources(minimic_app PRIVATE nrf5340/app.c nrf5340/audio.c nrf5340/usb_descriptors.c
  codec/encoder.c ${TUSB}/src/tusb.c ${TUSB}/src/common/tusb_fifo.c
  ${TUSB}/src/device/usbd.c ${TUSB}/src/device/usbd_control.c
  ${TUSB}/src/class/audio/audio_device.c ${TUSB}/src/class/cdc/cdc_device.c
  ${TUSB}/src/portable/nordic/nrf5x/dcd_nrf5x.c)
core_target(minimic_net network)
target_compile_definitions(minimic_net PRIVATE NRF5340_XXAA_NETWORK)
target_compile_options(minimic_net PRIVATE -mcpu=cortex-m33+nodsp -mfloat-abi=soft)
target_link_options(minimic_net PRIVATE -mcpu=cortex-m33+nodsp -mfloat-abi=soft)
target_sources(minimic_net PRIVATE nrf5340/net.c nrf5340/wire.c)
