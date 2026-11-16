cmake_minimum_required(VERSION 3.24)
project(stx_map_installed_consumer LANGUAGES CXX)

# This fixture has no source-tree include path or library fallback.
find_package(stx_map 0.1 CONFIG REQUIRED)
add_executable(stx_map_installed_consumer main.cpp)
target_link_libraries(stx_map_installed_consumer PRIVATE stx_map::stx_map)
enable_testing()
add_test(NAME stx_map_installed_consumer COMMAND stx_map_installed_consumer)
