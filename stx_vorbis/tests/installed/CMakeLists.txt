cmake_minimum_required(VERSION 3.24)
project(stx_installed_consumer LANGUAGES CXX)
enable_testing()
find_package(stx_vorbis CONFIG REQUIRED)
add_executable(installed_streaming ../streaming.cpp)
target_link_libraries(installed_streaming PRIVATE stx_vorbis::stx_vorbis)
add_test(NAME installed_streaming COMMAND installed_streaming
  ${CMAKE_CURRENT_SOURCE_DIR}/../corpus/floor0-residue0.ogg
  ${CMAKE_CURRENT_SOURCE_DIR}/../corpus/floor1-residue2.ogg)
# A consumer can separately link the original BFFT API. The installed codec
# carries its own private symbols and requires no BFFT headers or target.
add_library(consumer_bodft STATIC ../../third_party/bfft/src/bodft_prepared.cpp)
target_include_directories(consumer_bodft PUBLIC ../../third_party/bfft/include)
target_compile_features(consumer_bodft PUBLIC cxx_std_17)
add_executable(installed_coexist coexist.cpp)
target_link_libraries(installed_coexist PRIVATE stx_vorbis::stx_vorbis consumer_bodft)
add_test(NAME installed_coexist COMMAND installed_coexist
  ${CMAKE_CURRENT_SOURCE_DIR}/../corpus/floor1-residue2.ogg)
