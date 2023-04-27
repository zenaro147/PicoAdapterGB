add_executable(${PROJECT_NAME}_PicoESP01 src/main.c)
 
# Pull in our pico_stdlib which pulls in commonly used features
target_link_libraries(${PROJECT_NAME}_PicoESP01 libmobile pico_stdlib pico_multicore hardware_spi hardware_uart hardware_flash)

# disable usb output, enable uart output
pico_enable_stdio_usb(${PROJECT_NAME}_PicoESP01 1)
pico_enable_stdio_uart(${PROJECT_NAME}_PicoESP01 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(${PROJECT_NAME}_PicoESP01)
