cmake_minimum_required(VERSION 3.12)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include(pico_sdk_import.cmake)
project(blink)
pico_sdk_init()
#add_executable(blink src/gbsniffer.c)
add_executable(blink src/gbsniffer2.c)
#add_executable(blink src/gbsniffer3.c)
#add_executable(blink src/wifi8266_AT.c)
 
# Pull in our pico_stdlib which pulls in commonly used features
target_link_libraries(blink pico_stdlib)
target_link_libraries(blink pico_multicore)
target_link_libraries(blink hardware_spi)
target_link_libraries(blink hardware_uart)

# enable usb output, disable uart output
#pico_enable_stdio_usb(blink 1)
#pico_enable_stdio_uart(blink 0)

# disable usb output, enable uart output
pico_enable_stdio_usb(blink 0)
pico_enable_stdio_uart(blink 1)

# create map/bin/hex file etc.
pico_add_extra_outputs(blink)
