#!/bin/sh
# Emits assembly of tools/kernels/probes.cpp for arm64 (NEON) and baseline
# x86-64 (SSE2, no AVX or FMA) into the given directory, with the compiler
# version beside it. Needs a Clang that accepts -arch for both targets (the
# macOS SDK). Inspection only: nothing is executed. Read the result with
# audit.py.
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output=${1:?Supply an output directory}
mkdir -p "$output"
cxx=${CXX:-clang++}
"$cxx" --version > "$output/compiler.txt"
for isa in arm64 x86_64; do
    flags=""
    if [ "$isa" = x86_64 ]; then flags="-march=x86-64 -mno-avx -mno-fma"; fi
    "$cxx" -arch "$isa" $flags -std=c++20 -O3 -ffp-contract=off -I "$base/src" -I "$base/include" \
        -S "$base/tools/kernels/probes.cpp" -o "$output/$isa.s"
done
