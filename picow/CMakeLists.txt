if(PICO_BOARD STREQUAL "pico_w")
    set(ProjBoardType "_PicoW")
elseif(PICO_BOARD STREQUAL "pico2_w")
    set(ProjBoardType "_Pico2W")
else()
    set(ProjBoardType "")
endif()

if(ADAPTER STREQUAL "STACKSMASHING")
    set(ProjOutputType "_SmBoard")
else()
    set(ProjOutputType "")
endif()

add_executable(${PROJECT_NAME}${ProjBoardType}${ProjOutputType}
    	        src/main.c 
                src/config_menu.c
                src/flash_eeprom.c 
                src/picow_socket.c 
                src/socket_impl.c
                src/sync.c
                src/pio/linkcable.c)

# Add PIO code
if(ADAPTER STREQUAL "REON")
    pico_generate_pio_header(${PROJECT_NAME}${ProjBoardType}${ProjOutputType} ${CMAKE_CURRENT_LIST_DIR}/src/pio/linkcable.pio)
elseif(ADAPTER STREQUAL "STACKSMASHING")
    add_compile_definitions(STACKSMASHING)
    pico_generate_pio_header(${PROJECT_NAME}${ProjBoardType}${ProjOutputType} ${CMAKE_CURRENT_LIST_DIR}/src/pio/linkcable_sm.pio)
endif()

target_include_directories(${PROJECT_NAME}${ProjBoardType}${ProjOutputType} PRIVATE /.. ${CMAKE_CURRENT_LIST_DIR})
 
# Pull in our pico_stdlib which pulls in commonly used features
target_link_libraries(${PROJECT_NAME}${ProjBoardType}${ProjOutputType} libmobile_static pico_stdlib hardware_pio hardware_flash pico_cyw43_arch_lwip_poll)

# disable usb output, enable uart output
pico_enable_stdio_usb(${PROJECT_NAME}${ProjBoardType}${ProjOutputType} 0)
pico_enable_stdio_uart(${PROJECT_NAME}${ProjBoardType}${ProjOutputType} 1)

# create map/bin/hex file etc.
pico_add_extra_outputs(${PROJECT_NAME}${ProjBoardType}${ProjOutputType})
