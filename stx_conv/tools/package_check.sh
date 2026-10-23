#!/bin/sh
# Install the release build, move the prefix, and build and run a consumer
# against the moved copy. Run from the repository root after verify.sh.
# Use a new record name each time; no existing result is overwritten.
set -eu
record="${1:?supply a new output-directory name}"
mkdir "build/$record"
cmake --install build/stx-conv-release --prefix "$PWD/build/$record/original"
mv "build/$record/original" "build/$record/relocated"
cmake -S stx_conv/tests/installed -B "build/$record/consumer" -DCMAKE_BUILD_TYPE=Release \
    ${CXX:+-DCMAKE_CXX_COMPILER="$CXX"} -DCMAKE_PREFIX_PATH="$PWD/build/$record/relocated"
cmake --build "build/$record/consumer" --parallel 4
ctest --test-dir "build/$record/consumer" --output-on-failure
