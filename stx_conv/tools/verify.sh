#!/bin/sh
# Release build and the whole standalone suite, with the pinned test-only
# oracle. Run from the repository root. The compiler is CXX when set (for
# example CXX=clang++-22), else CMake's default; the oracle is fetched and
# verified into build-stx-conv-sources when absent.
set -eu
python3 stx_conv/tools/fetch_oracle.py build-stx-conv-sources
cmake -S stx_conv -B build/stx-conv-release -DCMAKE_BUILD_TYPE=Release \
    ${CXX:+-DCMAKE_CXX_COMPILER="$CXX"} -DSTX_CONV_ORACLE="$PWD/build-stx-conv-sources"
cmake --build build/stx-conv-release --parallel 4
ctest --test-dir build/stx-conv-release --output-on-failure
