# stx_auxlib's process-wide SIMD level (include/stx_auxlib/simd.hpp) as the
# target stx_auxlib::simd: src/simd.cpp and the environment service it reads
# STX_SIMD_LEVEL through (environment, its host backend, and the threads,
# clocks and unicode units that service uses), nothing else of stx_auxlib
# (no math, no other hosted services, no allocator).
#
# The source is the commit stx_auxlib.lock names, fetched from GitHub; a
# local checkout replaces it with -DFETCHCONTENT_SOURCE_DIR_GUI_FORMS_STX_AUXLIB=<dir>
# (lockstep development). stx_auxlib's own CMake build is not run.
include(FetchContent)
get_filename_component(_gui_stx_auxlib_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(STRINGS "${_gui_stx_auxlib_root}/stx_auxlib.lock" _gui_stx_auxlib_lock
     REGEX "^(repository|commit) = ")
set(_gui_stx_auxlib_repository "")
set(_gui_stx_auxlib_commit "")
foreach(_gui_stx_auxlib_line IN LISTS _gui_stx_auxlib_lock)
    if(_gui_stx_auxlib_line MATCHES "^repository = \"([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)\"$")
        set(_gui_stx_auxlib_repository "${CMAKE_MATCH_1}")
    elseif(_gui_stx_auxlib_line MATCHES "^commit = \"([0-9a-f]+)\"$")
        set(_gui_stx_auxlib_commit "${CMAKE_MATCH_1}")
    endif()
endforeach()
string(LENGTH "${_gui_stx_auxlib_commit}" _gui_stx_auxlib_commit_length)
if(_gui_stx_auxlib_repository STREQUAL "" OR NOT _gui_stx_auxlib_commit_length EQUAL 40)
    message(FATAL_ERROR "stx_auxlib.lock needs repository = \"owner/name\" and a 40-character commit")
endif()
FetchContent_Declare(gui_forms_stx_auxlib
    GIT_REPOSITORY https://github.com/${_gui_stx_auxlib_repository}.git
    GIT_TAG ${_gui_stx_auxlib_commit}
    GIT_CONFIG core.autocrlf=false
    SOURCE_SUBDIR gui-forms-no-upstream-build)
FetchContent_MakeAvailable(gui_forms_stx_auxlib)
set(_gui_stx_auxlib_source "${gui_forms_stx_auxlib_SOURCE_DIR}")
if(NOT EXISTS "${_gui_stx_auxlib_source}/include/stx_auxlib/simd.hpp" OR
   NOT EXISTS "${_gui_stx_auxlib_source}/src/simd.cpp")
    message(FATAL_ERROR "the stx_auxlib source at ${_gui_stx_auxlib_source} has no SIMD level (simd.hpp, simd.cpp)")
endif()

if(WIN32)
    set(_gui_stx_auxlib_host windows)
else()
    set(_gui_stx_auxlib_host posix)
endif()
add_library(stx_auxlib_simd STATIC
    "${_gui_stx_auxlib_source}/src/simd.cpp"
    "${_gui_stx_auxlib_source}/src/unicode.cpp"
    "${_gui_stx_auxlib_source}/src/hosted/clocks.cpp"
    "${_gui_stx_auxlib_source}/src/hosted/environment.cpp"
    "${_gui_stx_auxlib_source}/src/hosted/environment_${_gui_stx_auxlib_host}.cpp"
    "${_gui_stx_auxlib_source}/src/hosted/threads.cpp")
add_library(stx_auxlib::simd ALIAS stx_auxlib_simd)
target_compile_features(stx_auxlib_simd PUBLIC cxx_std_20)
set_target_properties(stx_auxlib_simd PROPERTIES
    EXPORT_NAME simd CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
# Only simd.hpp is installed; the build reads it from the fetched tree.
target_include_directories(stx_auxlib_simd PUBLIC
    $<BUILD_INTERFACE:${_gui_stx_auxlib_source}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(stx_auxlib_simd PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
install(TARGETS stx_auxlib_simd EXPORT stx_auxlib_simdTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES "${_gui_stx_auxlib_source}/include/stx_auxlib/simd.hpp"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/stx_auxlib)
install(EXPORT stx_auxlib_simdTargets NAMESPACE stx_auxlib::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_auxlib_simd
        FILE stx_auxlib_simdConfig.cmake)
install(FILES "${_gui_stx_auxlib_source}/LICENSE"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/stx_auxlib_simd)
