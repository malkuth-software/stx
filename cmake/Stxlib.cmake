# STXlib (malkuth-software/stxlib) at the commit stxlib.lock names: its own
# CMake build makes stxlib::state, stxlib::boundary and stxlib::algorithms
# (tests off), and this file installs them beside the stx packages as the
# package `stxlib` (find_package(stxlib CONFIG) gives the three targets).
# stx_conv links stxlib::algorithms for the process's SIMD level
# (include/stxlib/simd.hpp); GUI.Forms links it for the row height index and
# for the glyph face its text rasterizers draw with (stxlib::glyphs,
# include/stxlib/glyph_face.hpp, compiled into stxlib::algorithms).
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
# a program that links STXlib ships, at share/licenses/stxlib under stx's
# <component>-LICENSE naming:
# - LICENSE, STXlib's own;
# - stx_btree-LICENSE and stx_btree-NOTICE (the row height index's sequence
#   tree, src/algorithms/stx_btree/);
# - tzdata-LICENSE (the zone data local time is built from);
# - stx_minmallocpp-NOTICE (STXlib's allocator, carrying mimalloc's licence),
#   in every configuration, Emscripten included;
# - unicode-LICENSE (Unicode License v3, for the case-folding tables generated
#   from CaseFolding.txt in stxlib_algorithms), in every configuration;
# - glyphs-NOTICE (stxlib::glyphs' font reader and hinter: the notices of
#   ttf-parser, Skrifa/read-fonts and the FreeType Project's design it
#   converts), in every configuration;
# - markdown-NOTICE (stxlib::markdown, carrying MD4C's licence) and
#   whatwg-entities-LICENSE (its named character references, from the HTML
#   Standard's list), in every configuration;
# - mimalloc-LICENSE only when STXLIB_ALLOCATOR is mimalloc (the vendored
#   mimalloc is compiled into stxlib_state).
# stxlibConfig.cmake exports them: stxlib_LICENSE_DIR is that directory and
# stxlib_LICENSE_FILES the names installed there.
#
# STXlib from ed91414 installs these files itself, under the same names, and
# its own install runs in stx's (FetchContent_MakeAvailable): stx then adds
# no rule of its own, so no file is installed twice. An older STXlib installs
# none, and stx installs those it has; the two notices it predates are
# skipped. stx no longer installs mimalloc's LICENSE unconditionally: STXlib
# drops the vendored mimalloc, and the rule would fail on a missing file.
#
# A lock without a repository and a 40-character commit, or a source without
# the SIMD level or the glyph face, stops the configure.
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
# The glyph face (STXlib 23a0025, layout 13): GUI.Forms' text rasterizers
# draw through it, so an older STXlib is refused here rather than at
# GUI.Forms' compile.
if(NOT EXISTS "${_stx_stxlib_source}/include/stxlib/glyph_face.hpp")
    message(FATAL_ERROR "the STXlib source at ${_stx_stxlib_source} has no glyph face "
                        "(include/stxlib/glyph_face.hpp, STXlib 23a0025 or later)")
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
# STXlib's licence files: source path and installed name, in pairs. The
# required ones stop the configure when missing; the notices an older STXlib
# predates (stx_minmallocpp-NOTICE, unicode-LICENSE, glyphs-NOTICE,
# markdown-NOTICE, whatwg-entities-LICENSE) are installed when present;
# mimalloc's only with the mimalloc backend.
set(_stx_stxlib_licence_directory ${CMAKE_INSTALL_DATADIR}/licenses/stxlib)
set(_stx_stxlib_required_licences
    "LICENSE" "LICENSE"
    "src/algorithms/stx_btree/LICENSE" "stx_btree-LICENSE"
    "src/algorithms/stx_btree/NOTICE" "stx_btree-NOTICE"
    "third_party/tzdata/LICENSE" "tzdata-LICENSE")
set(_stx_stxlib_present_licences
    "src/state/stx_minmallocpp/NOTICE" "stx_minmallocpp-NOTICE"
    # The Unicode License v3 for the case-folding tables (CaseFolding.txt);
    # STXlib da36497 installs it as unicode-LICENSE.
    "third_party/unicode-18.0.0/LICENSE" "unicode-LICENSE"
    # stxlib::glyphs (the glyph face), stxlib::markdown and its named
    # character references; STXlib 23a0025 installs all three.
    "src/algorithms/glyphs/NOTICE" "glyphs-NOTICE"
    "src/algorithms/markdown/NOTICE" "markdown-NOTICE"
    "third_party/whatwg-entities/LICENSE" "whatwg-entities-LICENSE")
if(STXLIB_ALLOCATOR STREQUAL "mimalloc")
    list(APPEND _stx_stxlib_present_licences "third_party/mimalloc/LICENSE" "mimalloc-LICENSE")
endif()
# Whether this STXlib installs its licences itself (ed91414 and later): its
# CMakeLists.txt names stx_minmallocpp-NOTICE as an installed file.
file(READ "${_stx_stxlib_source}/CMakeLists.txt" _stx_stxlib_lists)
string(FIND "${_stx_stxlib_lists}" "RENAME stx_minmallocpp-NOTICE" _stx_stxlib_own_licences)
set(stxlib_LICENSE_FILES "")
foreach(_stx_stxlib_set IN ITEMS required present)
    set(_stx_stxlib_pairs ${_stx_stxlib_${_stx_stxlib_set}_licences})
    list(LENGTH _stx_stxlib_pairs _stx_stxlib_pair_count)
    math(EXPR _stx_stxlib_last "${_stx_stxlib_pair_count} - 2")
    foreach(_stx_stxlib_index RANGE 0 ${_stx_stxlib_last} 2)
        math(EXPR _stx_stxlib_name_index "${_stx_stxlib_index} + 1")
        list(GET _stx_stxlib_pairs ${_stx_stxlib_index} _stx_stxlib_from)
        list(GET _stx_stxlib_pairs ${_stx_stxlib_name_index} _stx_stxlib_name)
        if(NOT EXISTS "${_stx_stxlib_source}/${_stx_stxlib_from}")
            if(_stx_stxlib_set STREQUAL "required")
                message(FATAL_ERROR "the STXlib source at ${_stx_stxlib_source} has no ${_stx_stxlib_from}")
            endif()
            continue()
        endif()
        if(_stx_stxlib_own_licences EQUAL -1)
            install(FILES "${_stx_stxlib_source}/${_stx_stxlib_from}"
                    DESTINATION ${_stx_stxlib_licence_directory} RENAME ${_stx_stxlib_name})
        elseif(NOT _stx_stxlib_name STREQUAL "LICENSE")
            # STXlib installs it: export the name only when its rules do (a
            # file in its source is not necessarily one it installs).
            string(FIND "${_stx_stxlib_lists}" "RENAME ${_stx_stxlib_name}" _stx_stxlib_installed)
            if(_stx_stxlib_installed EQUAL -1)
                continue()
            endif()
        endif()
        list(APPEND stxlib_LICENSE_FILES "${_stx_stxlib_name}")
    endforeach()
endforeach()
unset(_stx_stxlib_lists)
# The licence directory as the installed config finds it, relative to
# lib/cmake/stxlib (an absolute data directory is used as it is).
if(IS_ABSOLUTE "${CMAKE_INSTALL_DATADIR}")
    set(_stx_stxlib_licence_reference "${CMAKE_INSTALL_DATADIR}/licenses/stxlib")
else()
    file(RELATIVE_PATH _stx_stxlib_licence_relative "/prefix/${CMAKE_INSTALL_LIBDIR}/cmake/stxlib"
         "/prefix/${_stx_stxlib_licence_directory}")
    set(_stx_stxlib_licence_reference "\${CMAKE_CURRENT_LIST_DIR}/${_stx_stxlib_licence_relative}")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/stxlib-config/stxlibConfig.cmake"
"# STXlib as stx installs it (cmake/Stxlib.cmake): stxlib::state, stxlib::boundary
# and stxlib::algorithms, from stxlib.lock's commit.
include(CMakeFindDependencyMacro)
find_dependency(Threads)
include(\"\${CMAKE_CURRENT_LIST_DIR}/stxlibTargets.cmake\")
# The licences a program that links STXlib ships: their directory and the
# file names installed there (cmake/Stxlib.cmake in stx).
get_filename_component(stxlib_LICENSE_DIR \"${_stx_stxlib_licence_reference}\" ABSOLUTE)
set(stxlib_LICENSE_FILES \"${stxlib_LICENSE_FILES}\")
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/stxlib-config/stxlibConfig.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stxlib)
