####################################
# Add Libmobile files as a Library
####################################
set(REON_LIBMOBILE libmobile)

file (GLOB_RECURSE LibMobile_SOURCES CONFIGURE_DEPENDS "libmobile/*.c")
file (GLOB_RECURSE LibMobile_HEADERS CONFIGURE_DEPENDS "libmobile/*.h")

set (LibMobile_INCLUDE_DIRS "")
foreach (_headerFile ${LibMobile_HEADERS})
    get_filename_component(_dir ${_headerFile} PATH)
    list (APPEND LibMobile_INCLUDE_DIRS ${_dir})
endforeach()
list (REMOVE_DUPLICATES LibMobile_INCLUDE_DIRS)

add_library(libmobile ${LibMobile_SOURCES})
target_include_directories(libmobile PUBLIC ${REON_LIBMOBILE})


####################################
# Add FreeRTOS files as a Library
####################################
# set(PICO_SDK_FREERTOS_SOURCE FreeRTOS-Kernel)

# add_library(freertos
#     ${PICO_SDK_FREERTOS_SOURCE}/event_groups.c
#     ${PICO_SDK_FREERTOS_SOURCE}/list.c
#     ${PICO_SDK_FREERTOS_SOURCE}/queue.c
#     ${PICO_SDK_FREERTOS_SOURCE}/stream_buffer.c
#     ${PICO_SDK_FREERTOS_SOURCE}/tasks.c
#     ${PICO_SDK_FREERTOS_SOURCE}/timers.c
#     ${PICO_SDK_FREERTOS_SOURCE}/portable/MemMang/heap_3.c
#     ${PICO_SDK_FREERTOS_SOURCE}/portable/ThirdParty/GCC/RP2040/port.c
# )

# target_include_directories(freertos PUBLIC
#     .
#     ${PICO_SDK_FREERTOS_SOURCE}/include
#     ${PICO_SDK_FREERTOS_SOURCE}/portable/ThirdParty/GCC/RP2040
# )