#!/bin/sh
# Builds three Release candidate trees that differ only in how SkUNREACHABLE (the Release
# failure path of the collection bounds helpers) is defined, then times them.
#   trap        current definition (__builtin_trap)
#   unreachable __builtin_unreachable(): no check; the cost floor
#   abort       SK_ABORT with a message
# Output: build/bounds-<variant>/ and build/bounds-measurements.json.
set -eu
for variant in trap unreachable abort; do
    directory="build/bounds-$variant"
    mkdir -p "$directory"
    case "$variant" in
        trap) printf '/* current SkUNREACHABLE */\n' > "$directory/variant.h" ;;
        unreachable) printf '#define SkUNREACHABLE __builtin_unreachable()\n' > "$directory/variant.h" ;;
        abort) printf '#define SkUNREACHABLE SK_ABORT("unreachable")\n' > "$directory/variant.h" ;;
    esac
    cmake -S . -B "$directory" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
        -DSTX_SKIA_BASELINE=OFF -DSTX_SKIA_SANITIZE=OFF \
        "-DCMAKE_CXX_FLAGS=-include $(pwd)/$directory/variant.h" > "$directory/configure.log"
    cmake --build "$directory" -j "${BUILD_JOBS:-6}" > "$directory/build.log"
done
python3 tools/measure_bounds.py
