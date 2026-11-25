cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(stx_raster3d VERSION 1.0.4 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(STX_RASTER3D_TESTS_DEFAULT ON)
else()
    set(STX_RASTER3D_TESTS_DEFAULT ${GUI_FORMS_BUILD_TESTS})
endif()
option(STX_RASTER3D_TESTS "Build the renderer's deterministic tests" ${STX_RASTER3D_TESTS_DEFAULT})

# stx_raster3d's only dependency: CONV surfaces for the adaptive mesher and
# exact picking. Under the stx root (or GUI.Forms) the sibling target exists;
# a standalone build finds an installed stx_conv.
if(NOT TARGET stx_conv::stx_conv)
    find_package(stx_conv 1.0 CONFIG REQUIRED)
endif()

add_library(stx_raster3d STATIC src/memory.cpp src/camera.cpp src/mesh.cpp src/raster.cpp src/picking.cpp)
add_library(stx_raster3d::stx_raster3d ALIAS stx_raster3d)
target_compile_features(stx_raster3d PUBLIC cxx_std_20)
set_target_properties(stx_raster3d PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_include_directories(stx_raster3d PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(stx_raster3d PUBLIC stx_conv::stx_conv)
# No contraction into fused multiply-add: pictures are the same bytes on
# every architecture the compiler would otherwise fuse differently on.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(stx_raster3d PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -ffp-contract=off)
elseif(MSVC)
    target_compile_options(stx_raster3d PRIVATE /W4 /fp:precise)
endif()

if(STX_RASTER3D_TESTS)
    enable_testing()
    add_executable(stx_raster3d_tests tests/raster3d.cpp)
    target_link_libraries(stx_raster3d_tests PRIVATE stx_raster3d::stx_raster3d)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(stx_raster3d_tests PRIVATE -Wall -Wextra -Wshadow -ffp-contract=off)
    endif()
    add_test(NAME stx_raster3d_tests COMMAND stx_raster3d_tests)
endif()

install(TARGETS stx_raster3d EXPORT stx_raster3dTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT stx_raster3dTargets NAMESPACE stx_raster3d::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_raster3d)
configure_package_config_file(cmake/stx_raster3dConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/stx_raster3dConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_raster3d)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/stx_raster3dConfigVersion.cmake
    VERSION 1.0.4 COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stx_raster3dConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/stx_raster3dConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_raster3d)
install(FILES LICENSE PROVENANCE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/stx_raster3d)
