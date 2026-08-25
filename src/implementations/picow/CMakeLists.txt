####################################################################
# picow implementation: cyw43 + lwIP Wi-Fi backend for Pico W/Pico 2 W.
# Selected via -DPICOADAPTER_IMPLEMENTATION=picow (the default). Provides
# net_hal.h, net/socket_hal.h, core/led_hal.h and an optional web/ config UI.
####################################################################

if(NOT PICO_CYW43_SUPPORTED OR NOT TARGET pico_cyw43_arch)
    message(FATAL_ERROR
        "PICOADAPTER_IMPLEMENTATION=picow requires a board with cyw43 Wi-Fi "
        "support (PICO_BOARD=pico_w or pico2_w). Got PICO_BOARD=${PICO_BOARD}.")
endif()

# BoardLabel/PinoutLabel come from the root CMakeLists.txt (board and pinout
# are dimensions independent of which implementation is selected). This
# implementation only knows its own suffix: since pico_w/pico2_w already
# imply "has Wi-Fi", picow contributes no extra suffix to the hardware name
# (contrast a hypothetical esp/4g implementation on a plain pico/pico2 board,
# which would need to append its own label there instead).
set(ImplHardwareSuffix "")
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
# retroactively. release/<Pinout>/<Board>/, e.g. release/REON/PicoW/
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

# This implementation ships a web setup UI; main.c (core) only compiles its
# web_config_*() calls when this is defined (see src/main.c).
add_compile_definitions(PICOADAPTER_HAS_WEB)
add_compile_definitions(PICO_ADAPTER_HARDWARE=\"${HardwareLabel}\")

target_include_directories(${ProjTargetName} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}
)

# Pull in our pico_stdlib which pulls in commonly used features
target_link_libraries(${ProjTargetName} libmobile_static pico_stdlib pico_multicore hardware_pio hardware_flash hardware_watchdog pico_cyw43_arch_lwip_poll)

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
