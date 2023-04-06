cmake_minimum_required(VERSION 3.12)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_BOARD pico_w)

include(pico_sdk_import.cmake)
project(PicoAdapterGB C CXX ASM)
pico_sdk_init()

file (GLOB_RECURSE LibMobile_SOURCES CONFIGURE_DEPENDS "src/libmobile/*.c")
file (GLOB_RECURSE LibMobile_HEADERS CONFIGURE_DEPENDS "src/libmobile/*.h")

set (LibMobile_INCLUDE_DIRS "")
foreach (_headerFile ${LibMobile_HEADERS})
    get_filename_component(_dir ${_headerFile} PATH)
    list (APPEND LibMobile_INCLUDE_DIRS ${_dir})
endforeach()
list (REMOVE_DUPLICATES LibMobile_INCLUDE_DIRS)

add_executable(${PROJECT_NAME} src/main.c src/socket.c src/socket_impl.c ${LibMobile_SOURCES} )
target_include_directories(${PROJECT_NAME} PRIVATE ${LibMobile_INCLUDE_DIRS} ${CMAKE_CURRENT_LIST_DIR})
 
target_link_libraries(${PROJECT_NAME} pico_stdlib pico_multicore hardware_spi hardware_flash pico_cyw43_arch_lwip_threadsafe_background)

# enable usb output, disable uart output
#pico_enable_stdio_usb(${PROJECT_NAME} 1)
#pico_enable_stdio_uart(${PROJECT_NAME} 0)

# disable usb output, enable uart output
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 1)

# create map/bin/hex file etc.
pico_add_extra_outputs(${PROJECT_NAME})
