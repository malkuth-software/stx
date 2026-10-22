#!/bin/sh
set -eu
export PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin
cmake -S stx_conv -B build/stx-conv-release \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm@22/bin/clang++ \
    -DSTX_CONV_ORACLE="$PWD/build-stx-conv-sources"
cmake --build build/stx-conv-release --parallel 4
ctest --test-dir build/stx-conv-release --output-on-failure
