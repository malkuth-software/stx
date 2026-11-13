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
# - STXLIB_STATE_SHARED, passed to STXlib as is. stx sets it OFF unless the
#   consumer sets it: the stx packages are static archives, and STXlib's
#   state is then one static archive too. A program that links STXlib into
#   more than one module of its own (a shared library and the executable
#   using it) needs ON, which STXlib's own build documents.
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
set(STXLIB_STATE_SHARED OFF CACHE BOOL
    "Build stxlib_state as a shared library (required when STXlib is in more than one module)")

FetchContent_Declare(stxlib
    GIT_REPOSITORY https://github.com/${_stx_stxlib_repository}.git
    GIT_TAG ${_stx_stxlib_commit})
FetchContent_MakeAvailable(stxlib)
set(_stx_stxlib_source "${stxlib_SOURCE_DIR}")
if(NOT EXISTS "${_stx_stxlib_source}/include/stxlib/simd.hpp" OR NOT TARGET stxlib::algorithms)
    message(FATAL_ERROR "the STXlib source at ${_stx_stxlib_source} has no SIMD level "
                        "(include/stxlib/simd.hpp, stxlib::algorithms)")
endif()

# STXlib installs nothing itself; its three libraries go into one export set
# here. The object libraries it compiles into them (mimalloc, the avx2_fma
# math variant) are carried as objects and need no export of their own.
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
