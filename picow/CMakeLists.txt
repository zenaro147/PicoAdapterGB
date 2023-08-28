add_executable(${PROJECT_NAME}_PicoW 
    	        src/main.c 
                src/config_menu.c
                src/flash_eeprom.c 
                src/picow_socket.c 
                src/socket_impl.c
                src/pio/linkcable.c)

# Add PIO code
pico_generate_pio_header(${PROJECT_NAME}_PicoW ${CMAKE_CURRENT_LIST_DIR}/src/pio/linkcable.pio)
pico_generate_pio_header(${PROJECT_NAME}_PicoW ${CMAKE_CURRENT_LIST_DIR}/src/pio/linkcable_sm.pio)

target_include_directories(${PROJECT_NAME}_PicoW PRIVATE /.. ${CMAKE_CURRENT_LIST_DIR})
 
# Pull in our pico_stdlib which pulls in commonly used features
# target_link_libraries(${PROJECT_NAME}_PicoW libmobile pico_stdlib pico_multicore hardware_spi hardware_flash pico_cyw43_arch_lwip_poll)
target_link_libraries(${PROJECT_NAME}_PicoW libmobile_static pico_stdlib hardware_pio hardware_flash pico_cyw43_arch_lwip_poll)

# disable usb output, enable uart output
pico_enable_stdio_usb(${PROJECT_NAME}_PicoW 1)
pico_enable_stdio_uart(${PROJECT_NAME}_PicoW 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(${PROJECT_NAME}_PicoW)
