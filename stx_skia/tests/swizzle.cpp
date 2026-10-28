// Contract tests for stx_skia's red/blue swap. The kernel must match Skia's SkSwapRB
// (SkOpts::RGBA_to_BGRA) and an independent byte reference for every tail length, misaligned
// start, and in-place and disjoint call. Swapping twice must restore the input. Every
// validation failure must leave the destination untouched. "--benchmark" prints timings
// instead (see tools/measure_swizzle.py).

#include "differential_corpus.h"

#include "include/core/SkGraphics.h"
#include "include/core/SkSwizzle.h"
#include "src/core/SkSwizzlePriv.h"
#include "stx_skia/swizzle.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using stx_skia::PixelCount;
using stx_skia::RowBytes;
using stx_skia::RowCount;
using stx_skia::SwizzleResult;

// Units do not convert into each other or from bare integers, braced or not.
static_assert(!std::is_convertible_v<PixelCount, RowBytes>);
static_assert(!std::is_convertible_v<RowBytes, PixelCount>);
static_assert(!std::is_convertible_v<RowCount, PixelCount>);
static_assert(!std::is_convertible_v<std::size_t, RowBytes>);
static_assert(!std::is_convertible_v<std::size_t, PixelCount>);
static_assert(!std::is_convertible_v<std::size_t, RowCount>);

constexpr std::size_t kMaxSize = std::numeric_limits<std::size_t>::max();
constexpr std::byte kSentinel{0xA5};

std::vector<std::byte> random_bytes(corpus::Sequence& sequence, std::size_t count) {
    std::vector<std::byte> bytes(count);
    for (std::byte& value : bytes) value = static_cast<std::byte>(sequence.next() >> 24);
    return bytes;
}

// The byte reference: swap bytes 0 and 2 of each pixel.
void reference_swap(std::span<std::byte> dst, std::span<const std::byte> src) {
    for (std::size_t offset = 0; offset < src.size(); offset += 4) {
        const std::byte red = src[offset];
        const std::byte green = src[offset + 1];
        const std::byte blue = src[offset + 2];
        const std::byte alpha = src[offset + 3];
        dst[offset] = blue;
        dst[offset + 1] = green;
        dst[offset + 2] = red;
        dst[offset + 3] = alpha;
    }
}

// Skia's routine takes 4-byte-aligned words; copy through aligned storage.
std::vector<std::byte> skia_swap(std::span<const std::byte> src) {
    const std::size_t count = src.size() / 4;
    std::vector<std::uint32_t> words(count);
    if (count != 0) std::memcpy(words.data(), src.data(), src.size());
    std::vector<std::uint32_t> swapped(count);
    SkSwapRB(swapped.data(), words.data(), static_cast<int>(count));
    std::vector<std::byte> result(src.size());
    if (count != 0) std::memcpy(result.data(), swapped.data(), src.size());
    return result;
}

bool same(std::span<const std::byte> a, std::span<const std::byte> b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
}

bool untouched(std::span<const std::byte> bytes) {
    for (const std::byte value : bytes) {
        if (value != kSentinel) return false;
    }
    return true;
}

const std::array<std::size_t, 5> kLongCounts{255, 256, 1000, 1027, 4099};

std::vector<std::size_t> pixel_counts() {
    std::vector<std::size_t> counts{};
    for (std::size_t count = 0; count <= 64; ++count) counts.push_back(count);
    for (const std::size_t count : kLongCounts) counts.push_back(count);
    return counts;
}

// Every count and both misalignments, disjoint and in place, against Skia and the reference,
// and swapped twice back to the input.
bool span_cases() {
    corpus::Sequence sequence{};
    for (const std::size_t count : pixel_counts()) {
        const std::size_t bytes = count * 4;
        const std::vector<std::byte> input = random_bytes(sequence, bytes);
        std::vector<std::byte> expected(bytes);
        reference_swap(expected, input);
        if (!same(skia_swap(input), expected)) {
            std::printf("swizzle: Skia and the reference differ at %zu pixels\n", count);
            return false;
        }
        for (std::size_t srcOffset = 0; srcOffset < 4; ++srcOffset) {
            for (std::size_t dstOffset = 0; dstOffset < 4; ++dstOffset) {
                std::vector<std::byte> srcStorage(bytes + 4, kSentinel);
                std::vector<std::byte> dstStorage(bytes + 8, kSentinel);
                if (bytes != 0) std::memcpy(srcStorage.data() + srcOffset, input.data(), bytes);
                const std::span<const std::byte> src(srcStorage.data() + srcOffset, bytes);
                const std::span<std::byte> dst(dstStorage.data() + dstOffset, bytes);
                if (stx_skia::swap_red_blue(dst, src) != SwizzleResult::ok) return false;
                if (!same(dst, expected)) {
                    std::printf("swizzle: disjoint mismatch at %zu pixels, offsets %zu/%zu\n",
                                count, srcOffset, dstOffset);
                    return false;
                }
                const std::span<const std::byte> before(dstStorage.data(), dstOffset);
                const std::span<const std::byte> after(dstStorage.data() + dstOffset + bytes,
                                                       8 - dstOffset);
                if (!untouched(before) || !untouched(after)) return false;
                // Back again, into the source buffer: the input returns.
                const std::span<std::byte> back(srcStorage.data() + srcOffset, bytes);
                if (stx_skia::swap_red_blue(back, dst) != SwizzleResult::ok) return false;
                if (!same(back, input)) return false;
            }
            // In place, twice.
            std::vector<std::byte> storage(bytes + 4, kSentinel);
            if (bytes != 0) std::memcpy(storage.data() + srcOffset, input.data(), bytes);
            const std::span<std::byte> pixels(storage.data() + srcOffset, bytes);
            if (stx_skia::swap_red_blue(pixels, pixels) != SwizzleResult::ok) return false;
            if (!same(pixels, expected)) {
                std::printf("swizzle: in-place mismatch at %zu pixels, offset %zu\n", count,
                            srcOffset);
                return false;
            }
            if (stx_skia::swap_red_blue(pixels, pixels) != SwizzleResult::ok) return false;
            if (!same(pixels, input)) return false;
        }
    }
    return true;
}

struct Layout {
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t srcStride = 0;
    std::size_t dstStride = 0;
};

// Strided rectangles: each row matches the reference, padding between and after rows keeps
// its sentinel, and swapping back restores the source rows.
bool row_cases() {
    const std::array<Layout, 8> layouts{
        Layout{1, 1, 4, 4},     Layout{3, 5, 16, 12},   Layout{17, 3, 70, 68},
        Layout{16, 4, 64, 64},  Layout{33, 7, 140, 133}, Layout{64, 2, 256, 300},
        Layout{5, 1, 0, 0},     Layout{300, 20, 4096, 1203}};
    corpus::Sequence sequence{};
    for (const Layout layout : layouts) {
        const std::size_t rowBytes = layout.width * 4;
        const std::size_t srcExtent = layout.srcStride * (layout.height - 1) + rowBytes;
        const std::size_t dstExtent = layout.dstStride * (layout.height - 1) + rowBytes;
        const std::vector<std::byte> source = random_bytes(sequence, srcExtent + 3);
        for (std::size_t offset = 0; offset < 4; ++offset) {
            std::vector<std::byte> srcStorage = source;
            std::vector<std::byte> dstStorage(dstExtent + 8, kSentinel);
            const std::span<const std::byte> src(srcStorage.data(), srcExtent);
            const std::span<std::byte> dst(dstStorage.data() + offset, dstExtent);
            const SwizzleResult result = stx_skia::swap_red_blue_rows(
                    dst, RowBytes(layout.dstStride), src, RowBytes(layout.srcStride),
                    PixelCount(layout.width), RowCount(layout.height));
            if (result != SwizzleResult::ok) return false;
            for (std::size_t row = 0; row < layout.height; ++row) {
                std::vector<std::byte> expected(rowBytes);
                reference_swap(expected, src.subspan(row * layout.srcStride, rowBytes));
                if (!same(dst.subspan(row * layout.dstStride, rowBytes), expected)) {
                    std::printf("swizzle: row mismatch, width %zu row %zu\n", layout.width, row);
                    return false;
                }
                if (row + 1 < layout.height) {
                    const std::size_t gapStart = row * layout.dstStride + rowBytes;
                    const std::size_t gap = layout.dstStride - rowBytes;
                    if (!untouched(dst.subspan(gapStart, gap))) return false;
                }
            }
            if (!untouched(std::span<const std::byte>(dstStorage.data(), offset))) return false;
            const std::span<const std::byte> tail(dstStorage.data() + offset + dstExtent,
                                                  8 - offset);
            if (!untouched(tail)) return false;
            // Back into a copy of the source: the rows return, the gaps stay as they were.
            std::vector<std::byte> back = source;
            const std::span<std::byte> backRows(back.data(), srcExtent);
            const SwizzleResult returned = stx_skia::swap_red_blue_rows(
                    backRows, RowBytes(layout.srcStride), dst, RowBytes(layout.dstStride),
                    PixelCount(layout.width), RowCount(layout.height));
            if (returned != SwizzleResult::ok || back != source) return false;
            // In place over the source, twice.
            std::vector<std::byte> inPlace = source;
            const std::span<std::byte> rows(inPlace.data(), srcExtent);
            for (int pass = 0; pass < 2; ++pass) {
                const SwizzleResult swapped = stx_skia::swap_red_blue_rows(
                        rows, RowBytes(layout.srcStride), rows, RowBytes(layout.srcStride),
                        PixelCount(layout.width), RowCount(layout.height));
                if (swapped != SwizzleResult::ok) return false;
            }
            if (inPlace != source) return false;
        }
    }
    return true;
}

// Runs one rows call into a sentinel-filled destination and checks the result and that
// nothing was written.
bool rows_fail(SwizzleResult expected, std::span<std::byte> dst, RowBytes dstStride,
               std::span<const std::byte> src, RowBytes srcStride, PixelCount width,
               RowCount height, const char* name) {
    std::vector<std::byte> before(dst.begin(), dst.end());
    const SwizzleResult result =
            stx_skia::swap_red_blue_rows(dst, dstStride, src, srcStride, width, height);
    const bool unchanged = std::equal(dst.begin(), dst.end(), before.begin());
    if (result != expected || !unchanged) {
        std::printf("swizzle: %s gave result %d, destination %s\n", name,
                    static_cast<int>(result), unchanged ? "unchanged" : "written");
        return false;
    }
    return true;
}

bool validation_cases() {
    std::vector<std::byte> a(4096, kSentinel);
    std::vector<std::byte> b(4096, kSentinel);
    const std::span<std::byte> dst(a);
    const std::span<const std::byte> src(b);
    bool valid = true;
    // A stride narrower than a row, for either buffer.
    valid = rows_fail(SwizzleResult::row_too_narrow, dst, RowBytes(15), src, RowBytes(16),
                      PixelCount(4), RowCount(2), "narrow destination stride") && valid;
    valid = rows_fail(SwizzleResult::row_too_narrow, dst, RowBytes(16), src, RowBytes(12),
                      PixelCount(4), RowCount(2), "narrow source stride") && valid;
    // width x 4 overflowing.
    valid = rows_fail(SwizzleResult::extent_overflow, dst, RowBytes(kMaxSize), src,
                      RowBytes(kMaxSize), PixelCount(kMaxSize / 4 + 1), RowCount(1),
                      "row width overflow") && valid;
    // stride x (height - 1) overflowing.
    valid = rows_fail(SwizzleResult::extent_overflow, dst, RowBytes(kMaxSize / 2 + 1), src,
                      RowBytes(16), PixelCount(4), RowCount(3), "stride product overflow") &&
            valid;
    // The last row's start fits, but adding its width does not.
    valid = rows_fail(SwizzleResult::extent_overflow, dst, RowBytes(16), src,
                      RowBytes(kMaxSize - 4), PixelCount(2), RowCount(2),
                      "extent sum overflow") && valid;
    // Spans shorter than their extents.
    valid = rows_fail(SwizzleResult::buffer_too_short, dst.first(16 * 3 + 15), RowBytes(16),
                      src, RowBytes(16), PixelCount(4), RowCount(4), "short destination") &&
            valid;
    valid = rows_fail(SwizzleResult::buffer_too_short, dst, RowBytes(16), src.first(63),
                      RowBytes(16), PixelCount(4), RowCount(4), "short source") && valid;
    // Partial overlaps: the same start with different strides, an offset start, and a
    // destination that begins inside the source's last row.
    const std::span<const std::byte> self(a);
    valid = rows_fail(SwizzleResult::partial_overlap, dst, RowBytes(32), self, RowBytes(16),
                      PixelCount(4), RowCount(4), "same start, different strides") && valid;
    valid = rows_fail(SwizzleResult::partial_overlap, dst.subspan(4), RowBytes(16), self,
                      RowBytes(16), PixelCount(4), RowCount(4), "offset start") && valid;
    valid = rows_fail(SwizzleResult::partial_overlap, dst.subspan(16 * 3 + 8), RowBytes(16),
                      self, RowBytes(16), PixelCount(4), RowCount(4),
                      "start inside the last source row") && valid;
    // Rows that touch only in another buffer's gaps are still overlapping extents.
    valid = rows_fail(SwizzleResult::partial_overlap, dst.subspan(8), RowBytes(32), self,
                      RowBytes(32), PixelCount(2), RowCount(4), "interleaved rows") && valid;
    // Zero width or height succeeds without writing; one row needs no stride.
    valid = rows_fail(SwizzleResult::ok, dst, RowBytes(16), src, RowBytes(16), PixelCount(0),
                      RowCount(4), "zero width") && valid;
    valid = rows_fail(SwizzleResult::ok, dst, RowBytes(16), src, RowBytes(16), PixelCount(4),
                      RowCount(0), "zero height") && valid;
    if (stx_skia::swap_red_blue_rows(dst, RowBytes(0), src, RowBytes(0), PixelCount(4),
                                     RowCount(1)) != SwizzleResult::ok) {
        std::puts("swizzle: one row with no stride was refused");
        valid = false;
    }
    std::fill(a.begin(), a.end(), kSentinel);

    // Whole-span calls.
    const std::array<std::pair<SwizzleResult, std::pair<std::size_t, std::size_t>>, 3> sizes{
        std::pair{SwizzleResult::size_not_whole_pixels, std::pair<std::size_t, std::size_t>{6, 6}},
        std::pair{SwizzleResult::size_not_whole_pixels, std::pair<std::size_t, std::size_t>{8, 7}},
        std::pair{SwizzleResult::size_mismatch, std::pair<std::size_t, std::size_t>{8, 12}}};
    for (const std::pair<SwizzleResult, std::pair<std::size_t, std::size_t>>& size : sizes) {
        const SwizzleResult result = stx_skia::swap_red_blue(dst.first(size.second.first),
                                                             src.first(size.second.second));
        if (result != size.first || !untouched(dst)) {
            std::printf("swizzle: size case %zu/%zu gave %d\n", size.second.first,
                        size.second.second, static_cast<int>(result));
            valid = false;
        }
    }
    const SwizzleResult shifted = stx_skia::swap_red_blue(dst.subspan(4, 64), self.first(64));
    if (shifted != SwizzleResult::partial_overlap || !untouched(dst)) {
        std::puts("swizzle: overlapping spans were not refused");
        valid = false;
    }
    if (stx_skia::swap_red_blue({}, {}) != SwizzleResult::ok) valid = false;

    // Stride conversion at its overflow boundary.
    const std::optional<RowBytes> largest = stx_skia::row_bytes_for(PixelCount(kMaxSize / 4));
    if (!largest.has_value() || (*largest).value != kMaxSize / 4 * 4) valid = false;
    if (stx_skia::row_bytes_for(PixelCount(kMaxSize / 4 + 1)).has_value()) valid = false;
    const std::optional<RowBytes> android = stx_skia::row_bytes_for(PixelCount(1088));
    if (!android.has_value() || (*android).value != 4352) valid = false;
    return valid;
}

// ---- Benchmark ---------------------------------------------------------------------------

// The byte-permutation form, written as GUI.Forms' existing loops are.
void bytes_disjoint(std::byte* dst, const std::byte* src, std::size_t count) {
    for (std::size_t pixel = 0; pixel < count; ++pixel) {
        const std::size_t offset = pixel * 4;
        dst[offset] = src[offset + 2];
        dst[offset + 1] = src[offset + 1];
        dst[offset + 2] = src[offset];
        dst[offset + 3] = src[offset + 3];
    }
}

void bytes_in_place(std::byte* pixels, std::size_t count) {
    for (std::size_t pixel = 0; pixel < count; ++pixel) {
        const std::size_t offset = pixel * 4;
        const std::byte red = pixels[offset];
        pixels[offset] = pixels[offset + 2];
        pixels[offset + 2] = red;
    }
}

// One pixel at a time with vectorization disabled: the shape of Skia's portable fallback,
// which baseline x86-64 uses without run-time dispatch.
void scalar_disjoint(std::byte* dst, const std::byte* src, std::size_t count) {
#if defined(__clang__)
#pragma clang loop vectorize(disable) interleave(disable)
#endif
    for (std::size_t pixel = 0; pixel < count; ++pixel) {
        const std::size_t offset = pixel * 4;
        dst[offset] = src[offset + 2];
        dst[offset + 1] = src[offset + 1];
        dst[offset + 2] = src[offset];
        dst[offset + 3] = src[offset + 3];
    }
}

void scalar_in_place(std::byte* pixels, std::size_t count) {
#if defined(__clang__)
#pragma clang loop vectorize(disable) interleave(disable)
#endif
    for (std::size_t pixel = 0; pixel < count; ++pixel) {
        const std::size_t offset = pixel * 4;
        const std::byte red = pixels[offset];
        pixels[offset] = pixels[offset + 2];
        pixels[offset + 2] = red;
    }
}

// skia is SkSwapRB after SkOpts' run-time dispatch (SSSE3 or AVX2 on x86-64); skia_default is
// the routine compiled for the build's baseline before dispatch, which at the x86-64 SSE2
// baseline is Skia's one-pixel loop. They are the same routine on arm64.
enum class Kernel { stx, bytes, skia, skia_default, scalar };
constexpr std::array<Kernel, 5> kKernels{Kernel::stx, Kernel::bytes, Kernel::skia,
                                         Kernel::skia_default, Kernel::scalar};

// Skia's swap as compiled, captured before SkGraphics::Init's dispatch replaces it.
SkOpts::Swizzle_8888_u32 gSkiaDefault = nullptr;

const char* kernel_name(Kernel kernel) {
    switch (kernel) {
        case Kernel::stx: return "stx";
        case Kernel::bytes: return "bytes";
        case Kernel::skia: return "skia";
        case Kernel::skia_default: return "skia_default";
        case Kernel::scalar: return "scalar";
    }
    return "unknown";
}

struct Case {
    const char* name = "";
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t stride = 0;  // bytes, for both buffers
};

// One pass of kernel over every row. Skia's routine needs 4-byte-aligned words; the
// benchmark buffers are allocated as words, so the rows are aligned.
void run_kernel(Kernel kernel, bool inPlace, std::byte* dst, const std::byte* src,
                const Case& shape) {
    if (kernel == Kernel::stx) {
        const std::size_t extent = shape.stride * (shape.height - 1) + shape.width * 4;
        const std::span<std::byte> dstSpan(dst, extent);
        const std::span<const std::byte> srcSpan(inPlace ? dst : src, extent);
        const SwizzleResult result = stx_skia::swap_red_blue_rows(
                dstSpan, RowBytes(shape.stride), srcSpan, RowBytes(shape.stride),
                PixelCount(shape.width), RowCount(shape.height));
        if (result != SwizzleResult::ok) std::abort();
        return;
    }
    for (std::size_t row = 0; row < shape.height; ++row) {
        std::byte* const dstRow = dst + row * shape.stride;
        const std::byte* const srcRow = (inPlace ? dst : src) + row * shape.stride;
        switch (kernel) {
            case Kernel::bytes:
                if (inPlace) bytes_in_place(dstRow, shape.width);
                else bytes_disjoint(dstRow, srcRow, shape.width);
                break;
            case Kernel::scalar:
                if (inPlace) scalar_in_place(dstRow, shape.width);
                else scalar_disjoint(dstRow, srcRow, shape.width);
                break;
            case Kernel::skia: {
                std::uint32_t* const words = reinterpret_cast<std::uint32_t*>(dstRow);
                const std::uint32_t* const from = reinterpret_cast<const std::uint32_t*>(srcRow);
                SkSwapRB(words, from, static_cast<int>(shape.width));
                break;
            }
            case Kernel::skia_default: {
                std::uint32_t* const words = reinterpret_cast<std::uint32_t*>(dstRow);
                const std::uint32_t* const from = reinterpret_cast<const std::uint32_t*>(srcRow);
                gSkiaDefault(words, from, static_cast<int>(shape.width));
                break;
            }
            case Kernel::stx:
                break;
        }
    }
}

int benchmark() {
    const std::array<Case, 5> cases{Case{"64px", 64, 1, 256}, Case{"row1920", 1920, 1, 7680},
                                    Case{"rect300x200", 300, 200, 4096},
                                    Case{"frame1920x1080", 1920, 1080, 7680},
                                    Case{"frame3840x2160", 3840, 2160, 15360}};
    corpus::Sequence sequence{};
    std::puts("kernel,case,mode,pixels,iterations,seconds_per_pass,hash");
    for (const Case& shape : cases) {
        const std::size_t extent = shape.stride * (shape.height - 1) + shape.width * 4;
        std::vector<std::uint32_t> srcWords(extent / 4 + 1);
        std::vector<std::uint32_t> dstWords(extent / 4 + 1);
        for (std::uint32_t& word : srcWords) word = sequence.next();
        std::byte* const src = reinterpret_cast<std::byte*>(srcWords.data());
        std::byte* const dst = reinterpret_cast<std::byte*>(dstWords.data());
        const std::size_t pixels = shape.width * shape.height;
        // About 256 MiB of pixels per measurement, at least one pass.
        const std::size_t target = (std::size_t{256} << 20) / (pixels * 4);
        const std::size_t iterations = target == 0 ? 1 : target;
        for (const Kernel kernel : kKernels) {
            for (int mode = 0; mode < 2; ++mode) {
                const bool inPlace = mode == 1;
                std::memcpy(dst, src, extent);
                run_kernel(kernel, inPlace, dst, src, shape);  // warm-up
                const std::chrono::steady_clock::time_point start =
                        std::chrono::steady_clock::now();
                for (std::size_t i = 0; i < iterations; ++i) {
                    run_kernel(kernel, inPlace, dst, src, shape);
                }
                const std::chrono::steady_clock::time_point stop =
                        std::chrono::steady_clock::now();
                const std::chrono::duration<double> elapsed = stop - start;
                std::vector<std::uint8_t> bytes(extent);
                std::memcpy(bytes.data(), dst, extent);
                std::printf("%s,%s,%s,%zu,%zu,%.9g,%llu\n", kernel_name(kernel), shape.name,
                            inPlace ? "in_place" : "disjoint", pixels, iterations,
                            elapsed.count() / static_cast<double>(iterations),
                            static_cast<unsigned long long>(corpus::fnv1a(bytes)));
            }
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    gSkiaDefault = SkOpts::RGBA_to_BGRA;
    // Caches the CPU's features, then lets SkOpts pick its swap: without the cache, Skia's
    // dispatch sees no SSSE3 or AVX2 and keeps the default.
    SkGraphics::Init();
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) return benchmark();
    bool valid = span_cases();
    valid = row_cases() && valid;
    valid = validation_cases() && valid;
    if (!valid) {
        std::puts("swizzle: FAIL");
        return 1;
    }
    std::puts("swizzle: spans, rows, round trips and validation PASS");
    return 0;
}
