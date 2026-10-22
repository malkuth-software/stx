#!/bin/sh
set -eu
export PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:/usr/bin:/bin
# Use distinct prefixes on reruns; no existing result is overwritten or removed.
record="${1:?supply a new output-directory name}"
mkdir "build/$record"
cmake --install build/stx-conv-release --prefix "$PWD/build/$record/original"
mv "build/$record/original" "build/$record/relocated"
cmake -S stx_conv/tests/installed -B "build/$record/consumer" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm@22/bin/clang++ \
    -DCMAKE_PREFIX_PATH="$PWD/build/$record/relocated"
cmake --build "build/$record/consumer" --parallel 4
ctest --test-dir "build/$record/consumer" --output-on-failure
