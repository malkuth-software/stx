cmake_minimum_required(VERSION 3.24)
project(stx_png_installed_consumer LANGUAGES CXX)

# This fixture has no source-tree include path or library fallback; the
# package config finds stx_zlib by itself.
find_package(stx_png 0.1 CONFIG REQUIRED)
add_executable(stx_png_installed_consumer main.cpp)
target_link_libraries(stx_png_installed_consumer PRIVATE stx_png::stx_png)
enable_testing()
add_test(NAME stx_png_installed_consumer COMMAND stx_png_installed_consumer)
