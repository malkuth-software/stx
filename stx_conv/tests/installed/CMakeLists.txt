cmake_minimum_required(VERSION 3.24)
project(stx_conv_consumer LANGUAGES CXX)
# A consumer of an installed stx_conv prefix (CMAKE_PREFIX_PATH); build it
# with the library's compiler.
find_package(stx_conv CONFIG REQUIRED)
# With STXlib (found through stx_conv's config), the prefix carries STXlib's
# licences and stxlibConfig.cmake names them (stx cmake/Stxlib.cmake):
# STXlib's own LICENSE always, and every listed file in the directory.
if(stxlib_FOUND)
    if(NOT stxlib_LICENSE_DIR OR NOT "LICENSE" IN_LIST stxlib_LICENSE_FILES)
        message(FATAL_ERROR "stxlibConfig.cmake names no licence directory or no LICENSE")
    endif()
    foreach(stxlib_licence IN LISTS stxlib_LICENSE_FILES)
        if(NOT EXISTS "${stxlib_LICENSE_DIR}/${stxlib_licence}")
            message(FATAL_ERROR "the stx prefix has no ${stxlib_LICENSE_DIR}/${stxlib_licence}")
        endif()
    endforeach()
endif()
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE stx_conv::stx_conv stx_conv::stream stx_conv::kernels)
enable_testing()
add_test(NAME installed_consumer COMMAND consumer)
