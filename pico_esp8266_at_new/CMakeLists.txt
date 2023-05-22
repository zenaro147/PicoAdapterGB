add_executable(${PROJECT_NAME}_PicoESP01
                                         src/esp_at.c
                                         src/config_menu.c 
                                         src/gblink.c 
                                         src/flash_eeprom.c 
                                         src/pio/linkcable.c)

# Add PIO code
pico_generate_pio_header(${PROJECT_NAME}_PicoESP01 ${CMAKE_CURRENT_LIST_DIR}/src/pio/linkcable.pio)
 
# Pull in our pico_stdlib which pulls in commonly used features
target_link_libraries(${PROJECT_NAME}_PicoESP01 libmobile_static pico_stdlib hardware_pio hardware_uart hardware_flash)

# disable usb output, enable uart output
pico_enable_stdio_usb(${PROJECT_NAME}_PicoESP01 0)
pico_enable_stdio_uart(${PROJECT_NAME}_PicoESP01 1)

# create map/bin/hex file etc.
pico_add_extra_outputs(${PROJECT_NAME}_PicoESP01)
