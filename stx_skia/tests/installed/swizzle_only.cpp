// Links only the swizzle library, with no Skia include directory and no Skia code: a host
// can use the red/blue swap on its own. Built in the build tree and against the installed
// package.

#include "stx_skia/swizzle.hpp"

#if __has_include("include/core/SkTypes.h")
#error The swizzle library must not put Skia's headers on the include path.
#endif

#include <array>
#include <cstddef>
#include <optional>
#include <span>

int main() {
    std::array<std::byte, 8> pixels{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                    std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    std::array<std::byte, 8> swapped{};
    if (stx_skia::swap_red_blue(swapped, pixels) != stx_skia::SwizzleResult::ok) return 1;
    if (swapped[0] != std::byte{3} || swapped[2] != std::byte{1} ||
        swapped[4] != std::byte{7} || swapped[6] != std::byte{5}) return 2;
    // An Android-style stride in pixels converts to bytes before a rows call.
    const std::optional<stx_skia::RowBytes> stride =
            stx_skia::row_bytes_for(stx_skia::PixelCount(1));
    if (!stride.has_value()) return 3;
    const stx_skia::SwizzleResult rows = stx_skia::swap_red_blue_rows(
            swapped, *stride, swapped, *stride, stx_skia::PixelCount(1),
            stx_skia::RowCount(2));
    if (rows != stx_skia::SwizzleResult::ok || swapped != pixels) return 4;
    return 0;
}
