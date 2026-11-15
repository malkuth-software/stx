# STXlib (malkuth-software/stxlib) at the commit stxlib.lock names: its own
# CMake build makes stxlib::state, stxlib::boundary and stxlib::algorithms
# (tests off), and this file installs them beside the stx packages as the
# package `stxlib` (find_package(stxlib CONFIG) gives the three targets).
# stx_conv links stxlib::algorithms for the process's SIMD level
# (include/stxlib/simd.hpp); GUI.Forms links it for the row height index.
#
# Inputs:
# - stxlib.lock: repository and commit, fetched from GitHub;
# - FETCHCONTENT_SOURCE_DIR_STXLIB=<dir>: a local STXlib checkout in its
#   place (lockstep development);
# - STXLIB_STATE_SHARED, passed to STXlib as is; ON, STXlib's default, is
#   what stx builds and installs. stxlib_state holds STXlib's allocator (with
#   mimalloc), the SIMD level and the shadow state, and must be one per
#   process: a program with STXlib in a shared library and in the executable
#   using it (GUI.Forms) would otherwise have two allocators, two mimallocs
#   and two SIMD selections, and a block freed across the modules would be
#   refused or undefined. The stx libraries stay static archives; only
#   libstxlib_state (stxlib_state.dll with its import library on Windows) is
#   shared. OFF is for a consumer that is one module with no shared library
#   of its own linking STXlib.
#
# Outputs: the targets above; the headers at include/stxlib; and the licences
# a program that links STXlib ships, at share/licenses/stxlib: STXlib's
# LICENSE, stx_btree's LICENSE and NOTICE (the row height index's sequence
# tree, src/algorithms/stx_btree/), mimalloc's LICENSE (its allocator
# backend, compiled into stxlib_state) and tzdata's LICENSE (the zone data
# local time is built from).
#
# A lock without a repository and a 40-character commit, or a source without
# the SIMD level, stops the configure.
include(FetchContent)
get_filename_component(_stx_stxlib_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(STRINGS "${_stx_stxlib_root}/stxlib.lock" _stx_stxlib_lock
     REGEX "^(repository|commit) = ")
set(_stx_stxlib_repository "")
set(_stx_stxlib_commit "")
foreach(_stx_stxlib_line IN LISTS _stx_stxlib_lock)
    if(_stx_stxlib_line MATCHES "^repository = \"([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)\"$")
        set(_stx_stxlib_repository "${CMAKE_MATCH_1}")
    elseif(_stx_stxlib_line MATCHES "^commit = \"([0-9a-f]+)\"$")
        set(_stx_stxlib_commit "${CMAKE_MATCH_1}")
    endif()
endforeach()
string(LENGTH "${_stx_stxlib_commit}" _stx_stxlib_commit_length)
if(_stx_stxlib_repository STREQUAL "" OR NOT _stx_stxlib_commit_length EQUAL 40)
    message(FATAL_ERROR "stxlib.lock needs repository = \"owner/name\" and a 40-character commit")
endif()

# STXlib's options, set before its CMakeLists.txt reads them: no tests or
# table generators in a consumer's build.
set(STXLIB_TESTS OFF CACHE BOOL "Build STXlib's tests and proof harnesses")
set(STXLIB_BUILD_TOOLS OFF CACHE BOOL "Build STXlib's table generators")
set(STXLIB_STATE_SHARED ON CACHE BOOL
    "Build stxlib_state as a shared library (required when STXlib is in more than one module)")

FetchContent_Declare(stxlib
    GIT_REPOSITORY https://github.com/${_stx_stxlib_repository}.git
    GIT_TAG ${_stx_stxlib_commit})
FetchContent_MakeAvailable(stxlib)
# Windows finds a DLL beside the program: in a build that runs its own
# programs (stx's tests) the shared state is written where they are, unless
# the consumer chose a runtime directory for every target already.
if(WIN32 AND STXLIB_STATE_SHARED AND NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set_target_properties(stxlib_state PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
set(_stx_stxlib_source "${stxlib_SOURCE_DIR}")
if(NOT EXISTS "${_stx_stxlib_source}/include/stxlib/simd.hpp" OR NOT TARGET stxlib::algorithms)
    message(FATAL_ERROR "the STXlib source at ${_stx_stxlib_source} has no SIMD level "
                        "(include/stxlib/simd.hpp, stxlib::algorithms)")
endif()

# STXlib installs nothing itself; its three libraries go into one export set
# here. The shared state installs as lib/libstxlib_state.dylib (install name
# @rpath/libstxlib_state.dylib; a consumer's rpath or @loader_path finds it),
# lib/libstxlib_state.so (a consumer's RUNPATH or $ORIGIN finds it), or
# bin/stxlib_state.dll with lib/libstxlib_state.dll.a (a program finds it
# beside itself). The object libraries it compiles into them (mimalloc, the avx2_fma
# math variant) are carried as objects and need no export of their own.
# The export names are the ones this file promises (stxlib::state,
# stxlib::boundary, stxlib::algorithms), not the targets' own names.
set_target_properties(stxlib_state PROPERTIES EXPORT_NAME state)
set_target_properties(stxlib_boundary PROPERTIES EXPORT_NAME boundary)
set_target_properties(stxlib_algorithms PROPERTIES EXPORT_NAME algorithms)
install(TARGETS stxlib_state stxlib_boundary stxlib_algorithms EXPORT stxlibTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY "${_stx_stxlib_source}/include/stxlib" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT stxlibTargets NAMESPACE stxlib::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stxlib)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/stxlib-config/stxlibConfig.cmake"
"# STXlib as stx installs it (cmake/Stxlib.cmake): stxlib::state, stxlib::boundary
# and stxlib::algorithms, from stxlib.lock's commit.
include(CMakeFindDependencyMacro)
find_dependency(Threads)
include(\"\${CMAKE_CURRENT_LIST_DIR}/stxlibTargets.cmake\")
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/stxlib-config/stxlibConfig.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stxlib)
install(FILES "${_stx_stxlib_source}/LICENSE"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/stxlib)
install(FILES "${_stx_stxlib_source}/src/algorithms/stx_btree/LICENSE"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/stxlib RENAME stx_btree-LICENSE)
install(FILES "${_stx_stxlib_source}/src/algorithms/stx_btree/NOTICE"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/stxlib RENAME stx_btree-NOTICE)
install(FILES "${_stx_stxlib_source}/third_party/mimalloc/LICENSE"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/stxlib RENAME mimalloc-LICENSE)
install(FILES "${_stx_stxlib_source}/third_party/tzdata/LICENSE"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/stxlib RENAME tzdata-LICENSE)
