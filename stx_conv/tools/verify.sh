#!/bin/sh
# Release build and the whole standalone suite. Run from the repository root.
# The compiler is CXX when set (for example CXX=clang++-22), else CMake's
# default.
set -eu
cmake -S stx_conv -B build/stx-conv-release -DCMAKE_BUILD_TYPE=Release \
    ${CXX:+-DCMAKE_CXX_COMPILER="$CXX"}
cmake --build build/stx-conv-release --parallel 4
ctest --test-dir build/stx-conv-release --output-on-failure
