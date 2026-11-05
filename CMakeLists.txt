cmake_minimum_required(VERSION 3.24)
# The stx libraries in one build: stx_zlib, stx_png, stx_conv, stx_rtf2md,
# stx_vorbis and stx_skia. Each directory is also a complete CMake package on its own
# (configure it alone, or consume its installed config with find_package);
# this root only adds them together so one configure builds, tests and
# installs all of them into one prefix.
project(stx VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STX_BUILD_TESTS "Build every package's standalone tests" ON)
option(STX_WITH_VORBIS "Build stx_vorbis (the audio decoder)" ON)
option(STX_WITH_SKIA "Build stx_skia (the CPU Skia derivative)" ON)
# stx_skia's own switch: only its red/blue swap kernel, stx_skia::swizzle (a
# consumer that draws without Skia). Passed through to the package as is.
option(STX_SKIA_SWIZZLE_ONLY "Build only stx_skia's red/blue swap kernel" OFF)

# The packages' own test switches. Under a parent build a package takes its
# default from the parent; here they all follow STX_BUILD_TESTS.
set(STX_ZLIB_TESTS ${STX_BUILD_TESTS} CACHE BOOL "Build standalone codec tests")
set(STX_PNG_TESTS ${STX_BUILD_TESTS} CACHE BOOL "Build standalone codec tests")
set(STX_CONV_TESTS ${STX_BUILD_TESTS} CACHE BOOL "Build deterministic algorithm tests")
set(STX_RTF2MD_TESTS ${STX_BUILD_TESTS} CACHE BOOL "Build standalone rich-text tests")
set(STX_VORBIS_TESTS ${STX_BUILD_TESTS} CACHE BOOL "Build standalone codec tests")
set(STX_SKIA_TESTS ${STX_BUILD_TESTS} CACHE BOOL "Build the stx_skia contract and corpus tests")
# Under this root stx_skia is not top level, so its install is asked for here.
set(STX_SKIA_INSTALL ON CACHE BOOL "Install the stx_skia package")

if(STX_BUILD_TESTS)
    enable_testing()
endif()

include(GNUInstallDirs)
# stx_auxlib::simd, the process-wide SIMD level stx_conv selects its lane
# variants by (without it only the v128 variant is built). It is installed
# beside the packages, so stx_conv's config finds it.
option(STX_WITH_AUXLIB_SIMD "Build stx_conv's lane selection over stx_auxlib::simd" ON)
if(STX_WITH_AUXLIB_SIMD)
    include(cmake/StxAuxlib.cmake)
endif()

# stx_png finds stx_zlib's target when it already exists, so zlib comes first.
add_subdirectory(stx_zlib)
add_subdirectory(stx_png)
add_subdirectory(stx_conv)
add_subdirectory(stx_rtf2md)
if(STX_WITH_VORBIS)
    add_subdirectory(stx_vorbis)
endif()
if(STX_WITH_SKIA)
    add_subdirectory(stx_skia)
endif()
