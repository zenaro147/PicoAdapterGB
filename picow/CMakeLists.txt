add_executable(${PROJECT_NAME}_PicoW 
    	        src/main.c 
                src/config_menu.c
                src/gblink.c 
                src/flash_eeprom.c 
                src/picow_socket.c 
                src/socket_impl.c)
target_include_directories(${PROJECT_NAME}_PicoW PRIVATE /.. ${CMAKE_CURRENT_LIST_DIR})
 
# Pull in our pico_stdlib which pulls in commonly used features
target_link_libraries(${PROJECT_NAME}_PicoW libmobile pico_stdlib pico_multicore hardware_spi hardware_flash pico_cyw43_arch_lwip_poll)

# disable usb output, enable uart output
pico_enable_stdio_usb(${PROJECT_NAME}_PicoW 1)
pico_enable_stdio_uart(${PROJECT_NAME}_PicoW 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(${PROJECT_NAME}_PicoW)
