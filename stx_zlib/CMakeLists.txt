cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(stx_zlib VERSION 0.1.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(STX_ZLIB_TESTS_DEFAULT ON)
else()
    set(STX_ZLIB_TESTS_DEFAULT ${GUI_FORMS_BUILD_TESTS})
endif()
option(STX_ZLIB_TESTS "Build standalone codec tests" ${STX_ZLIB_TESTS_DEFAULT})

add_library(stx_zlib STATIC src/checksum.cpp src/inflate.cpp src/deflate.cpp)
add_library(stx_zlib::stx_zlib ALIAS stx_zlib)
target_compile_features(stx_zlib PUBLIC cxx_std_20)
set_target_properties(stx_zlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(stx_zlib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(stx_zlib PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
elseif(MSVC)
    target_compile_options(stx_zlib PRIVATE /W4)
endif()

if(STX_ZLIB_TESTS)
    enable_testing()
    add_executable(stx_zlib_core_tests tests/core.cpp)
    target_link_libraries(stx_zlib_core_tests PRIVATE stx_zlib::stx_zlib)
    add_test(NAME stx_zlib_core_tests COMMAND stx_zlib_core_tests)
    add_executable(stx_zlib_deflate_tests tests/deflate.cpp)
    target_link_libraries(stx_zlib_deflate_tests PRIVATE stx_zlib::stx_zlib)
    add_test(NAME stx_zlib_deflate_tests COMMAND stx_zlib_deflate_tests)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME stx_zlib_deflate_oracle COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/oracle.py $<TARGET_FILE:stx_zlib_deflate_tests>)
    endif()
endif()

install(TARGETS stx_zlib EXPORT stx_zlibTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT stx_zlibTargets NAMESPACE stx_zlib::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_zlib)
configure_package_config_file(cmake/stx_zlibConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/stx_zlibConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_zlib)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/stx_zlibConfigVersion.cmake
    VERSION 0.1.0 COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stx_zlibConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/stx_zlibConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_zlib)
install(FILES LICENSE LICENSE-puff.txt LICENSE-zlib.txt PROVENANCE.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/stx_zlib)
