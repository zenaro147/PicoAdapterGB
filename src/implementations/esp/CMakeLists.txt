####################################################################
# esp implementation: Pico/Pico 2 + ESP8266EX (ESP-01) running ESP-AT
# v2.3.0.0, talking to the RP2040/RP2350 over UART. Selected via
# -DPICOADAPTER_IMPLEMENTATION=esp. Provides net_hal.h, net/socket_hal.h,
# core/led_hal.h and an optional web/ config UI, exactly like picow does for
# cyw43+lwIP - see src/implementations/esp/README.md for the ESP-AT command
# set actually used and the UART/pinout this backend expects.
####################################################################

if(NOT (PICO_BOARD STREQUAL "pico" OR PICO_BOARD STREQUAL "pico2"))
    message(FATAL_ERROR
        "PICOADAPTER_IMPLEMENTATION=esp requires a plain board with no "
        "onboard Wi-Fi (PICO_BOARD=pico or pico2 - Wi-Fi comes from the "
        "external ESP8266EX). Got PICO_BOARD=${PICO_BOARD}. Use "
        "PICOADAPTER_IMPLEMENTATION=picow for pico_w/pico2_w.")
endif()

# BoardLabel/PinoutLabel come from the root CMakeLists.txt. Unlike picow (whose
# board name already implies Wi-Fi), a plain pico/pico2 says nothing about
# connectivity on its own, so this implementation appends its own suffix -
# PicoAdapterGB_PicoESP_REON, PicoAdapterGB_Pico2ESP_REON, etc.
set(ImplHardwareSuffix "ESP")
set(HardwareLabel "${BoardLabel}${ImplHardwareSuffix}")

set(ProjTargetName "${PROJECT_NAME}_${HardwareLabel}_${PinoutLabel}")

file(GLOB_RECURSE SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)
# Core sources (src/**/*.c) are common to every implementation; exclude
# every implementations/ subtree (this one's own sources are globbed above).
file(GLOB_RECURSE CORE_SOURCES
    "${CMAKE_SOURCE_DIR}/src/*.c"
)
list(FILTER CORE_SOURCES EXCLUDE REGEX "${CMAKE_SOURCE_DIR}/src/implementations/.*")

# Must be set before add_executable(): CMake seeds a target's
# ARCHIVE_OUTPUT_DIRECTORY property from this variable at creation time, not
# retroactively. release/<Pinout>/<Board>/, e.g. release/REON/PicoESP/
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release/${PinoutLabel}/${HardwareLabel})

add_executable(${ProjTargetName}
    ${CORE_SOURCES}
    ${SOURCES}
)

# Adapter pinout PIO program (ADAPTER is validated by the root CMakeLists.txt).
if(ADAPTER STREQUAL "REON")
    pico_generate_pio_header(${ProjTargetName} ${CMAKE_SOURCE_DIR}/src/pio/linkcable.pio)
elseif(ADAPTER STREQUAL "STACKSMASHING")
    add_compile_definitions(STACKSMASHING)
    pico_generate_pio_header(${ProjTargetName} ${CMAKE_SOURCE_DIR}/src/pio/linkcable_sm.pio)
endif()

# This implementation ships a web setup UI (see web/); main.c (core) only
# compiles its web_config_*() calls when this is defined (see src/main.c).
add_compile_definitions(PICOADAPTER_HAS_WEB)
add_compile_definitions(PICO_ADAPTER_HARDWARE=\"${HardwareLabel}\")

target_include_directories(${ProjTargetName} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}
)

# No cyw43/lwIP: connectivity is UART + ESP-AT, so networking here only needs
# the Pico SDK's own UART driver on top of pico_stdlib.
target_link_libraries(${ProjTargetName} libmobile_static pico_stdlib hardware_pio hardware_flash hardware_watchdog hardware_uart)

# disable usb output, enable uart output
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message("USING DEBUG MODE: USB output disabled, UART output enabled")
    pico_enable_stdio_usb(${ProjTargetName} 0)
    pico_enable_stdio_uart(${ProjTargetName} 1)
else()
    pico_enable_stdio_usb(${ProjTargetName} 1)
    pico_enable_stdio_uart(${ProjTargetName} 0)
endif()

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(${ProjTargetName})

message(STATUS "PicoAdapterGB target: ${ProjTargetName}")
