cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(stx_png VERSION 0.1.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(STX_PNG_TESTS_DEFAULT ON)
else()
    set(STX_PNG_TESTS_DEFAULT ${GUI_FORMS_BUILD_TESTS})
endif()
option(STX_PNG_TESTS "Build standalone codec tests" ${STX_PNG_TESTS_DEFAULT})

# stx_png's only dependency. Inside GUI.Forms the sibling package's target
# already exists; a standalone build finds an installed stx_zlib.
if(NOT TARGET stx_zlib::stx_zlib)
    find_package(stx_zlib 0.1 CONFIG REQUIRED)
endif()

add_library(stx_png STATIC src/decoder.cpp src/encoder.cpp src/pixels.cpp)
add_library(stx_png::stx_png ALIAS stx_png)
target_compile_features(stx_png PUBLIC cxx_std_20)
set_target_properties(stx_png PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(stx_png PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(stx_png PUBLIC stx_zlib::stx_zlib)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(stx_png PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
elseif(MSVC)
    target_compile_options(stx_png PRIVATE /W4)
endif()

if(STX_PNG_TESTS)
    enable_testing()
    add_executable(stx_png_core_tests tests/core.cpp)
    target_link_libraries(stx_png_core_tests PRIVATE stx_png::stx_png)
    add_test(NAME stx_png_core_tests COMMAND stx_png_core_tests)
endif()

install(TARGETS stx_png EXPORT stx_pngTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT stx_pngTargets NAMESPACE stx_png::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_png)
configure_package_config_file(cmake/stx_pngConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/stx_pngConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_png)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/stx_pngConfigVersion.cmake
    VERSION 0.1.0 COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stx_pngConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/stx_pngConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_png)
install(FILES LICENSE PROVENANCE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/stx_png)
