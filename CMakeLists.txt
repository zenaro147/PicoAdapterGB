cmake_minimum_required(VERSION 3.12)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

#set(LIBMOBILE_ENABLE_NO32BIT ON)
set(ADAPTER "REON" CACHE STRING "The adapter pinout to build for, options are: 'REON', 'STACKSMASHING'.")
set_property(CACHE ADAPTER PROPERTY STRINGS "REON;STACKSMASHING")

if(NOT (ADAPTER MATCHES "REON|STACKSMASHING"))
    message(WARNING "Unknown adapter '${ADAPTER}', using 'REON' instead")
    set(ADAPTER "REON")
elseif(ADAPTER STREQUAL "REON")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release/REON)
elseif(ADAPTER STREQUAL "STACKSMASHING")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release/SmBoard)
endif()

message("Adapter pinout is ${ADAPTER}")

set(PICO_BOARD pico_w)

include(pico_sdk_import.cmake)
project(PicoAdapterGB C CXX ASM)
pico_sdk_init()
 
add_subdirectory(dependences)
#add_subdirectory(pico_esp8266_at)
if (PICO_CYW43_SUPPORTED)
    if (TARGET pico_cyw43_arch)
        add_subdirectory(picow)
    endif()
endif()
