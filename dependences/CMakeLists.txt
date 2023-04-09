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

target_include_directories(libmobile PUBLIC . ${REON_LIBMOBILE})