cmake_minimum_required(VERSION 3.24)
project(stx_zlib_installed_consumer LANGUAGES CXX)

# This fixture has no source-tree include path or library fallback.
find_package(stx_zlib 0.1 CONFIG REQUIRED)
add_executable(stx_zlib_installed_consumer main.cpp)
target_link_libraries(stx_zlib_installed_consumer PRIVATE stx_zlib::stx_zlib)
enable_testing()
add_test(NAME stx_zlib_installed_consumer COMMAND stx_zlib_installed_consumer)
