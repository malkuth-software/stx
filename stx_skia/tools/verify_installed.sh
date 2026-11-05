#!/bin/sh
set -eu
root=$(pwd)
if [ -e build/install-original ] || [ -e build/install-relocated ]; then
    echo 'Use a fresh build directory for installed verification' >&2
    exit 2
fi
cmake --install build/candidate --prefix "$root/build/install-original"
mv build/install-original build/install-relocated
cmake -S tests/installed -B build/installed-consumer -G Ninja \
    -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_PREFIX_PATH="$root/build/install-relocated"
cmake --build build/installed-consumer
build/installed-consumer/consumer
build/installed-consumer/swizzle_only
