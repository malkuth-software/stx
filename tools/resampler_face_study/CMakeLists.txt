cmake_minimum_required(VERSION 3.24)
# The image-face resampler study (see face_study.cpp). Standalone: builds
# stx_conv from this repository and the study program, nothing else.
project(resampler_face_study LANGUAGES CXX)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../stx_conv ${CMAKE_CURRENT_BINARY_DIR}/stx_conv)
add_executable(face_study face_study.cpp)
target_link_libraries(face_study PRIVATE stx_conv::stx_conv)
set_target_properties(face_study PROPERTIES CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(face_study PRIVATE -ffp-contract=off)
endif()
