#!/bin/sh
# The full record: release suite, relocated installed consumer, a clean
# instrumented (ASan and UBSan) build of the same suite, and the profile
# kernel's assembly for inspection. Run from the repository root with a new
# record name; outputs land in build/<record>/. On macOS an x86-64 SSE2
# assembly is also produced (inspection only, not execution).
set -eu
record="${1:?supply a new output-directory name}"
mkdir "build/$record"
sh stx_conv/tools/verify.sh > "build/$record/release.txt" 2>&1
cat "build/$record/release.txt"
sh stx_conv/tools/package_check.sh "$record-installed" > "build/$record/installed.txt" 2>&1
cat "build/$record/installed.txt"
cmake -S stx_conv -B build/stx-conv-sanitize -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    ${CXX:+-DCMAKE_CXX_COMPILER="$CXX"} -DSTX_CONV_ORACLE="$PWD/build-stx-conv-sources" -DSTX_CONV_SANITIZE=ON
cmake --build build/stx-conv-sanitize --clean-first --parallel 4
python3 stx_conv/tools/sanitizer_check.py build/stx-conv-sanitize "build/$record/integrity"
compiler="${CXX:-clang++}"
"$compiler" -std=c++20 -O3 -ffp-contract=off -Istx_conv/include -S stx_conv/src/profile.cpp \
    -o "build/$record/profile-native.s"
if command -v xcrun > /dev/null 2>&1; then
    "$compiler" -std=c++20 -O3 -ffp-contract=off -Istx_conv/include \
        -target x86_64-apple-macos14 -msse2 -mno-avx -isysroot "$(xcrun --show-sdk-path)" \
        -S stx_conv/src/profile.cpp -o "build/$record/profile-sse2.s"
fi
