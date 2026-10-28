#!/bin/sh
# Builds and tests one stx_skia profile, then writes its corpora. Run from the stx_skia
# directory. The tree is ${STX_SKIA_BUILD_ROOT:-build}/<profile>.
#   candidate           this tree's library
#   baseline            the reference profile: the library from STX_SKIA_REFERENCE_ROOT, a
#                       checkout of malkuth-software/stx_skia at the pin, and this tree's tests
#   candidate-sanitize  this tree under AddressSanitizer and UBSan; diagnostics go to the
#                       console and the first one stops the run
set -eu
profile=${1:-candidate}
reference_root=${STX_SKIA_REFERENCE_ROOT:-}
case "$profile" in
    baseline) baseline=ON; sanitize=OFF ;;
    candidate) baseline=OFF; sanitize=OFF ;;
    candidate-sanitize) baseline=OFF; sanitize=ON ;;
    *) echo 'Expected baseline, candidate, or candidate-sanitize' >&2; exit 2 ;;
esac
if [ "$baseline" = ON ] && [ -z "$reference_root" ]; then
    echo 'The baseline profile needs STX_SKIA_REFERENCE_ROOT' >&2
    exit 2
fi
tree="${STX_SKIA_BUILD_ROOT:-build}/$profile"
cmake -S . -B "$tree" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
    -DSTX_SKIA_BASELINE="$baseline" -DSTX_SKIA_SANITIZE="$sanitize" \
    -DSTX_SKIA_REFERENCE_ROOT="$reference_root"
cmake --build "$tree" -j "${BUILD_JOBS:-6}"
if [ "$sanitize" = ON ]; then
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        ctest --test-dir "$tree" --output-on-failure
    exit 0
fi
ctest --test-dir "$tree" --output-on-failure
# tests/corpora.txt: each line names a corpus program, then the files it writes.
grep -v '^#' tests/corpora.txt | while read -r program outputs; do
    set --
    for output in $outputs; do
        set -- "$@" "$tree/$output"
    done
    "$tree/$program" "$@"
done
