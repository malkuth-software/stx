cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(stx_rtf2md VERSION 0.1.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(STX_RTF2MD_TESTS_DEFAULT ON)
else()
    set(STX_RTF2MD_TESTS_DEFAULT ${GUI_FORMS_BUILD_TESTS})
endif()
option(STX_RTF2MD_TESTS "Build standalone rich-text tests" ${STX_RTF2MD_TESTS_DEFAULT})

add_library(stx_rtf2md STATIC
    src/document.cpp src/limits.cpp src/text_scan.cpp
    src/markdown_inline.cpp src/markdown_reader.cpp src/markdown_writer.cpp
    src/rtf_reader.cpp src/rtf_writer.cpp)
add_library(stx_rtf2md::stx_rtf2md ALIAS stx_rtf2md)
target_compile_features(stx_rtf2md PUBLIC cxx_std_20)
set_target_properties(stx_rtf2md PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(stx_rtf2md PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(stx_rtf2md PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
elseif(MSVC)
    target_compile_options(stx_rtf2md PRIVATE /W4)
endif()

if(STX_RTF2MD_TESTS)
    enable_testing()
    add_executable(stx_rtf2md_document_tests tests/document.cpp)
    target_link_libraries(stx_rtf2md_document_tests PRIVATE stx_rtf2md::stx_rtf2md)
    add_test(NAME stx_rtf2md_document_tests COMMAND stx_rtf2md_document_tests)
    add_executable(stx_rtf2md_markdown_tests tests/markdown.cpp)
    target_link_libraries(stx_rtf2md_markdown_tests PRIVATE stx_rtf2md::stx_rtf2md)
    add_test(NAME stx_rtf2md_markdown_tests COMMAND stx_rtf2md_markdown_tests)
    # The markdown contract fuzzer's fixed and generated inputs, in the normal suite.
    add_executable(stx_rtf2md_markdown_fuzz_tests fuzz/markdown_round_trip_fuzz.cpp)
    target_link_libraries(stx_rtf2md_markdown_fuzz_tests PRIVATE stx_rtf2md::stx_rtf2md)
    target_compile_definitions(stx_rtf2md_markdown_fuzz_tests PRIVATE STX_RTF2MD_FUZZ_STANDALONE=1)
    add_test(NAME stx_rtf2md_markdown_fuzz_tests COMMAND stx_rtf2md_markdown_fuzz_tests)
    add_executable(stx_rtf2md_rtf_tests tests/rtf.cpp)
    target_link_libraries(stx_rtf2md_rtf_tests PRIVATE stx_rtf2md::stx_rtf2md)
    add_test(NAME stx_rtf2md_rtf_tests COMMAND stx_rtf2md_rtf_tests)
    # The first-party corpus: conversions to reviewed markdown and the
    # malformed sweep.
    add_executable(stx_rtf2md_rtf_corpus_tests tests/rtf_corpus.cpp)
    target_link_libraries(stx_rtf2md_rtf_corpus_tests PRIVATE stx_rtf2md::stx_rtf2md)
    add_test(NAME stx_rtf2md_rtf_corpus_tests
        COMMAND stx_rtf2md_rtf_corpus_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus)
    add_executable(stx_rtf2md_rtf_fuzz_tests fuzz/rtf_read_fuzz.cpp)
    target_link_libraries(stx_rtf2md_rtf_fuzz_tests PRIVATE stx_rtf2md::stx_rtf2md)
    target_compile_definitions(stx_rtf2md_rtf_fuzz_tests PRIVATE STX_RTF2MD_FUZZ_STANDALONE=1)
    add_test(NAME stx_rtf2md_rtf_fuzz_tests COMMAND stx_rtf2md_rtf_fuzz_tests)
endif()

install(TARGETS stx_rtf2md EXPORT stx_rtf2mdTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT stx_rtf2mdTargets NAMESPACE stx_rtf2md::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_rtf2md)
configure_package_config_file(cmake/stx_rtf2mdConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/stx_rtf2mdConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_rtf2md)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/stx_rtf2mdConfigVersion.cmake
    VERSION 0.1.0 COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stx_rtf2mdConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/stx_rtf2mdConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stx_rtf2md)
install(FILES LICENSE PROVENANCE.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/stx_rtf2md)
