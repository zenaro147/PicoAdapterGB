# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.12)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)


if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(PICO_ADAPTER_SOFTWARE "Debug" CACHE STRING "Firmware version shown by the adapter")
else()
    set(PICO_ADAPTER_SOFTWARE "NoVersion" CACHE STRING "Firmware version shown by the adapter")
endif()
add_compile_definitions(PICO_ADAPTER_SOFTWARE=\"${PICO_ADAPTER_SOFTWARE}\")

#set(LIBMOBILE_ENABLE_NO32BIT ON)

####################################################################
# Three independent build dimensions - do not collapse into one variable:
#   PICO_BOARD                the RP2040/RP2350 chip/board (Pico SDK's own variable)
#   PICOADAPTER_IMPLEMENTATION  which connectivity/hardware backend under src/implementations/
#   ADAPTER                   the Game Boy link-cable pinout (REON / STACKSMASHING)
####################################################################

set(ADAPTER "REON" CACHE STRING "The adapter pinout to build for, options are: 'REON', 'STACKSMASHING'.")
set_property(CACHE ADAPTER PROPERTY STRINGS "REON;STACKSMASHING")

set(PICOADAPTER_IMPLEMENTATION "picow" CACHE STRING "Connectivity/hardware backend under src/implementations/, options are: 'picow', 'esp'.")
set_property(CACHE PICOADAPTER_IMPLEMENTATION PROPERTY STRINGS "picow;esp")

# PICO_BOARD has no safe default across implementations (picow needs pico_w/
# pico2_w, esp needs pico/pico2), so an unrecognized value fails the configure
# step instead of silently falling back to some other board's firmware.
if(PICO_BOARD STREQUAL "pico_w")
    set(PICO_PLATFORM "rp2040")
elseif(PICO_BOARD STREQUAL "pico2_w")
    set(PICO_PLATFORM "rp2350")
elseif(PICO_BOARD STREQUAL "pico")
    set(PICO_PLATFORM "rp2040")
elseif(PICO_BOARD STREQUAL "pico2")
    set(PICO_PLATFORM "rp2350")
else()
    message(FATAL_ERROR
        "Unknown PICO_BOARD '${PICO_BOARD}'. Valid values: pico_w, pico2_w "
        "(PICOADAPTER_IMPLEMENTATION=picow), pico, pico2 "
        "(PICOADAPTER_IMPLEMENTATION=esp).")
endif()

message("PICO_BOARD version is ${PICO_BOARD}")

if(NOT (ADAPTER MATCHES "REON|STACKSMASHING"))
    message(WARNING "Unknown adapter '${ADAPTER}', using 'REON' instead")
    set(ADAPTER "REON")
endif()

if(NOT (PICOADAPTER_IMPLEMENTATION MATCHES "^picow$|^esp$"))
    message(FATAL_ERROR
        "Unknown PICOADAPTER_IMPLEMENTATION '${PICOADAPTER_IMPLEMENTATION}'. "
        "Valid values: picow, esp.")
endif()

message("Adapter pinout is ${ADAPTER}")
message("PicoAdapterGB implementation is ${PICOADAPTER_IMPLEMENTATION}")

# Friendly name components shared by every implementation's CMakeLists.txt,
# used both for PICO_ADAPTER_HARDWARE (see globals.h) and for artifact/target
# names (e.g. PicoAdapterGB_PicoW_REON). Adding a new PICO_BOARD value here
# only ever needs a new branch in this one mapping.
if(PICO_BOARD STREQUAL "pico_w")
    set(BoardLabel "PicoW")
elseif(PICO_BOARD STREQUAL "pico2_w")
    set(BoardLabel "Pico2W")
elseif(PICO_BOARD STREQUAL "pico")
    set(BoardLabel "Pico")
elseif(PICO_BOARD STREQUAL "pico2")
    set(BoardLabel "Pico2")
else()
    message(FATAL_ERROR "No BoardLabel mapping for PICO_BOARD '${PICO_BOARD}'.")
endif()

if(ADAPTER STREQUAL "STACKSMASHING")
    set(PinoutLabel "SmBoard")
else()
    set(PinoutLabel "REON")
endif()

include(pico_sdk_import.cmake)
project(PicoAdapterGB C CXX ASM)
pico_sdk_init()

add_subdirectory(dependences)

# Each implementation's own CMakeLists.txt owns its dependencies (e.g. picow
# needs cyw43+lwIP) and validates board compatibility itself; the root
# project never assumes a specific implementation's requirements are met.
if(PICOADAPTER_IMPLEMENTATION STREQUAL "picow")
    add_subdirectory(src/implementations/picow)
elseif(PICOADAPTER_IMPLEMENTATION STREQUAL "esp")
    add_subdirectory(src/implementations/esp)
endif()

message(STATUS "====================================================")
message(STATUS " PicoAdapterGB configuration")
message(STATUS "   Board:          ${PICO_BOARD}")
message(STATUS "   Implementation: ${PICOADAPTER_IMPLEMENTATION}")
message(STATUS "   Pinout:         ${ADAPTER}")
message(STATUS "====================================================")
