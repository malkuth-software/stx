cmake_minimum_required(VERSION 3.24)
project(stx_bfft_experiment LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)

# This target deliberately cannot replace/install the shipping stx_vorbis.
# Keep the alternate factorization opt-in until its cross-platform release
# gates pass. All candidate storage now uses the codec's bounded resource.
set(BFFT_SOURCE "" CACHE PATH "External BFFT checkout at the reviewed revision")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Git REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VORBIS REQUIRED IMPORTED_TARGET vorbisfile vorbisenc vorbis ogg)
set(bfft_revision "0f75ca79fbdc9176af729fc67afed59dca436ff1")
execute_process(COMMAND ${GIT_EXECUTABLE} -C "${BFFT_SOURCE}" rev-parse HEAD
  OUTPUT_VARIABLE actual_revision OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
if(NOT actual_revision STREQUAL bfft_revision)
  message(FATAL_ERROR "BFFT revision differs from the reviewed experiment pin")
endif()
execute_process(COMMAND ${GIT_EXECUTABLE} -C "${BFFT_SOURCE}" diff --exit-code HEAD --
  src/bodft.cpp src/bodft_prepared.cpp src/detail/bodft_storage.hpp
  src/detail/bodft_kernel.hpp src/detail/bruun_simd_backend.hpp
  include/bfft/bodft.h include/bfft/bfft.h tests/bodft_storage.cpp COMMAND_ERROR_IS_FATAL ANY)

get_filename_component(codec_source "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(projection "${CMAKE_CURRENT_BINARY_DIR}/projection")
execute_process(COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/project_decoder.py"
  --source "${codec_source}" --output "${projection}" COMMAND_ERROR_IS_FATAL ANY)
# Reconfigure if either projection anchor changes, instead of compiling stale
# copied private headers when the shipping decoder evolves.
file(GLOB codec_private CONFIGURE_DEPENDS "${codec_source}/src/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${codec_private}
  "${CMAKE_CURRENT_SOURCE_DIR}/project_decoder.py")

add_library(bodft_external STATIC "${BFFT_SOURCE}/src/bodft.cpp" "${BFFT_SOURCE}/src/bodft_prepared.cpp")
target_include_directories(bodft_external PUBLIC "${BFFT_SOURCE}/include")
target_compile_features(bodft_external PUBLIC cxx_std_17)

add_library(stx_bfft_experimental STATIC
  "${projection}/src/types.cpp" "${projection}/src/bit_reader.cpp"
  "${projection}/src/ogg.cpp" "${projection}/src/setup.cpp"
  "${projection}/src/synthesis.cpp" "${projection}/src/packet.cpp"
  "${projection}/src/decoder.cpp" "${projection}/src/source.cpp")
target_compile_features(stx_bfft_experimental PUBLIC cxx_std_20)
target_include_directories(stx_bfft_experimental PUBLIC "${codec_source}/include"
  PRIVATE "${codec_source}/bench")
target_link_libraries(stx_bfft_experimental PRIVATE bodft_external)

add_executable(stx_bench "${codec_source}/bench/decode.cpp")
target_link_libraries(stx_bench PRIVATE stx_bfft_experimental)
set_target_properties(stx_bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bench")
add_executable(stx_bench_libvorbis "${codec_source}/bench/decode.cpp")
target_compile_features(stx_bench_libvorbis PRIVATE cxx_std_20)
target_compile_definitions(stx_bench_libvorbis PRIVATE STX_BENCH_LIBVORBIS)
target_link_libraries(stx_bench_libvorbis PRIVATE PkgConfig::VORBIS)
set_target_properties(stx_bench_libvorbis PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bench")
add_executable(stx_vorbis_decode "${codec_source}/tools/decode.cpp")
target_link_libraries(stx_vorbis_decode PRIVATE stx_bfft_experimental)
add_executable(stx_vorbis_reference "${codec_source}/tools/reference.cpp")
target_compile_features(stx_vorbis_reference PRIVATE cxx_std_20)
target_link_libraries(stx_vorbis_reference PRIVATE PkgConfig::VORBIS)
add_executable(stx_vorbis_streaming "${codec_source}/tests/streaming.cpp")
target_link_libraries(stx_vorbis_streaming PRIVATE stx_bfft_experimental)
add_executable(stx_bfft_kernel "${codec_source}/bench/bfft_imdct.cpp")
target_include_directories(stx_bfft_kernel PRIVATE "${projection}/src" "${codec_source}/bench")
target_link_libraries(stx_bfft_kernel PRIVATE stx_bfft_experimental bodft_external)
add_executable(stx_bfft_hostile "${codec_source}/tests/hostile.cpp")
target_include_directories(stx_bfft_hostile PRIVATE "${projection}/src" "${codec_source}/bench")
target_link_libraries(stx_bfft_hostile PRIVATE stx_bfft_experimental bodft_external)
add_executable(stx_bfft_provisioning provisioning.cpp)
target_include_directories(stx_bfft_provisioning PRIVATE "${projection}/src" "${codec_source}/bench")
target_link_libraries(stx_bfft_provisioning PRIVATE stx_bfft_experimental bodft_external)
add_executable(bodft_storage "${BFFT_SOURCE}/tests/bodft_storage.cpp")
target_link_libraries(bodft_storage PRIVATE bodft_external Threads::Threads)

option(STX_BFFT_SANITIZERS "Instrument the experimental targets and BODFT" OFF)
foreach(target bodft_external stx_bfft_experimental stx_bench stx_bench_libvorbis
    stx_vorbis_decode stx_vorbis_reference stx_vorbis_streaming stx_bfft_kernel stx_bfft_hostile
    stx_bfft_provisioning bodft_storage)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE -ffp-contract=off)
    if(NOT target STREQUAL "bodft_external")
      target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
    if(STX_BFFT_SANITIZERS)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
  elseif(MSVC)
    target_compile_options(${target} PRIVATE /fp:strict)
  endif()
endforeach()
enable_testing()
add_test(NAME bodft_storage COMMAND bodft_storage)
add_test(NAME bfft_kernel COMMAND stx_bfft_kernel)
add_test(NAME bfft_hostile COMMAND stx_bfft_hostile "${codec_source}/tests/corpus/floor1-residue2.ogg")
add_test(NAME bfft_provisioning COMMAND stx_bfft_provisioning
  "${codec_source}/tests/corpus/floor0-residue0.ogg" "${codec_source}/tests/corpus/floor1-residue2.ogg")
add_test(NAME bfft_streaming COMMAND stx_vorbis_streaming
  "${codec_source}/tests/corpus/floor0-residue0.ogg"
  "${codec_source}/tests/corpus/floor1-residue2.ogg"
  "${codec_source}/tests/corpus/leading-trim.ogg")
