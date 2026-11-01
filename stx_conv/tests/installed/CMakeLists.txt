cmake_minimum_required(VERSION 3.24)
project(stx_conv_consumer LANGUAGES CXX)
# A consumer of an installed stx_conv prefix (CMAKE_PREFIX_PATH); build it
# with the library's compiler.
find_package(stx_conv CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE stx_conv::stx_conv stx_conv::stream stx_conv::kernels)
enable_testing()
add_test(NAME installed_consumer COMMAND consumer)
