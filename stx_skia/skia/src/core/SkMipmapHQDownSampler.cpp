/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkTypes.h"

#ifndef SK_USE_DRAWING_MIPMAP_DOWNSAMPLER

#include "src/core/SkColorData.h"
#include "src/core/SkHalf.h"
#include "src/core/SkMipmap.h"
#include "src/core/SkVx.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace {

// Integer channels have enough spare bits for the largest sum (16 weighted
// samples). Half-float channels expand to binary32 lanes; arithmetic order and
// narrowing retain the original filter's rounding. No platform intrinsics.
struct ColorTypeFilter_8888 {
    using Type = uint32_t;
    using Expanded = skvx::Vec<4, uint16_t>;
    static Expanded Expand(Type pixel) {
        const skvx::byte4 bytes = skvx::byte4::Load(&pixel);
        const Expanded channels = skvx::cast<uint16_t>(bytes);
        return channels;
    }
    static Type Compact(const Expanded& channels) {
        Type packed = 0;
        const skvx::byte4 bytes = skvx::cast<uint8_t>(channels);
        bytes.store(&packed);
        return packed;
    }
};

struct ColorTypeFilter_565 {
    using Type = uint16_t;
    using Expanded = uint32_t;
    static Expanded Expand(Type pixel) {
        const Expanded channels = (pixel & ~SK_G16_MASK_IN_PLACE) |
                                  ((pixel & SK_G16_MASK_IN_PLACE) << 16);
        return channels;
    }
    static Type Compact(Expanded channels) {
        const Expanded packed = ((channels & ~SK_G16_MASK_IN_PLACE) & 0xFFFF) |
                                ((channels >> 16) & SK_G16_MASK_IN_PLACE);
        const Type result = static_cast<Type>(packed);
        return result;
    }
};

struct ColorTypeFilter_4444 {
    using Type = uint16_t;
    using Expanded = uint32_t;
    static Expanded Expand(Type pixel) {
        const Expanded channels = (pixel & 0xF0F) | ((pixel & ~0xF0F) << 12);
        return channels;
    }
    static Type Compact(Expanded channels) {
        const Expanded packed = (channels & 0xF0F) | ((channels >> 12) & ~0xF0F);
        const Type result = static_cast<Type>(packed);
        return result;
    }
};

struct ColorTypeFilter_8 {
    using Type = uint8_t;
    using Expanded = unsigned;
    static Expanded Expand(unsigned pixel) { return pixel; }
    static Type Compact(Expanded channels) {
        const Type result = static_cast<Type>(channels);
        return result;
    }
};

struct ColorTypeFilter_F16 {
    using Type = uint16_t;
    using Expanded = skvx::float4;
    static Expanded Expand(Type pixel) {
        const uint64_t packed = pixel; // Three unused half lanes are zero.
        const skvx::half4 halves = skvx::half4::Load(&packed);
        const Expanded channels = from_half(halves);
        return channels;
    }
    static Type Compact(const Expanded& channels) {
        uint64_t packed = 0;
        const skvx::half4 halves = to_half(channels);
        halves.store(&packed);
        const Type result = static_cast<Type>(packed & 0xFFFFULL);
        return result;
    }
};

struct ColorTypeFilter_RGBA_F16 {
    using Type = uint64_t;
    using Expanded = skvx::float4;
    static Expanded Expand(Type pixel) {
        const skvx::half4 halves = skvx::half4::Load(&pixel);
        const Expanded channels = from_half(halves);
        return channels;
    }
    static Type Compact(const Expanded& channels) {
        Type packed = 0;
        const skvx::half4 halves = to_half(channels);
        halves.store(&packed);
        return packed;
    }
};

struct ColorTypeFilter_88 {
    using Type = uint16_t;
    using Expanded = uint32_t;
    static Expanded Expand(Type pixel) {
        const Expanded channels = (pixel & 0xFF) | ((pixel & ~0xFF) << 8);
        return channels;
    }
    static Type Compact(Expanded channels) {
        const Expanded packed = (channels & 0xFF) | ((channels >> 8) & ~0xFF);
        const Type result = static_cast<Type>(packed);
        return result;
    }
};

struct ColorTypeFilter_1616 {
    using Type = uint32_t;
    using Expanded = uint64_t;
    static Expanded Expand(Type pixel) {
        const Expanded wide = pixel;
        const Expanded channels = (wide & 0xFFFFULL) | ((wide & 0xFFFF0000ULL) << 16);
        return channels;
    }
    static Type Compact(Expanded channels) {
        const Expanded packed = (channels & 0xFFFFULL) | ((channels >> 16) & 0xFFFF0000ULL);
        const Type result = static_cast<Type>(packed);
        return result;
    }
};

struct ColorTypeFilter_F16F16 {
    using Type = uint32_t;
    using Expanded = skvx::float4;
    static Expanded Expand(Type pixel) {
        const uint64_t packed = pixel; // Two unused half lanes are zero.
        const skvx::half4 halves = skvx::half4::Load(&packed);
        const Expanded channels = from_half(halves);
        return channels;
    }
    static Type Compact(const Expanded& channels) {
        uint64_t packed = 0;
        const skvx::half4 halves = to_half(channels);
        halves.store(&packed);
        const Type result = static_cast<Type>(packed & 0xFFFFFFFFULL);
        return result;
    }
};

struct ColorTypeFilter_16161616 {
    using Type = uint64_t;
    using Expanded = skvx::Vec<4, uint32_t>;
    static Expanded Expand(Type pixel) {
        const skvx::Vec<4, uint16_t> words = skvx::Vec<4, uint16_t>::Load(&pixel);
        const Expanded channels = skvx::cast<uint32_t>(words);
        return channels;
    }
    static Type Compact(const Expanded& channels) {
        Type packed = 0;
        const skvx::Vec<4, uint16_t> words = skvx::cast<uint16_t>(channels);
        words.store(&packed);
        return packed;
    }
};

struct ColorTypeFilter_16 {
    using Type = uint16_t;
    using Expanded = uint32_t;
    static Expanded Expand(Type pixel) { return pixel; }
    static Type Compact(Expanded channels) {
        const Type result = static_cast<Type>(channels);
        return result;
    }
};

struct ColorTypeFilter_1010102 {
    using Type = uint32_t;
    using Expanded = uint64_t;
    static Expanded Expand(Expanded pixel) {
        const Expanded channels = (pixel & 0x3ffULL) |
                                  (((pixel >> 10) & 0x3ffULL) << 16) |
                                  (((pixel >> 20) & 0x3ffULL) << 32) |
                                  (((pixel >> 30) & 0x3ULL) << 48);
        return channels;
    }
    static Type Compact(Expanded channels) {
        const Expanded packed = (channels & 0x3ffULL) |
                                (((channels >> 16) & 0x3ffULL) << 10) |
                                (((channels >> 32) & 0x3ffULL) << 20) |
                                (((channels >> 48) & 0x3ULL) << 30);
        const Type result = static_cast<Type>(packed);
        return result;
    }
};

template <typename T> T add_121(const T& a, const T& b, const T& c) {
    const T sum = a + b + b + c;
    return sum;
}

template <typename T> T shift_right(const T& value, int bits) {
    const T shifted = value >> bits;
    return shifted;
}

skvx::float4 shift_right(const skvx::float4& value, int bits) {
    const float scale = 1.0f / (1 << bits);
    const skvx::float4 shifted = value * scale;
    return shifted;
}

template <typename T> T shift_left(const T& value, int bits) {
    const T shifted = value << bits;
    return shifted;
}

skvx::float4 shift_left(const skvx::float4& value, int bits) {
    const float scale = static_cast<float>(1 << bits);
    const skvx::float4 shifted = value * scale;
    return shifted;
}

// Each destination axis is max(1, floor(source/2)). Even axes use a two-sample
// box; odd axes above one use overlapping [1,2,1] triangles. A singleton axis
// uses one sample. The eight kernels implement all resulting footprints except
// 1x1, which does not reduce. Inputs/outputs are disjoint, aligned and complete.
// Three-column kernels carry the shared edge sum into the next output sample.

template <typename F> void downsample_1_2(void* dst, const void* src, size_t srcRB, int count) {
    SkASSERT(count > 0);
    const typename F::Type* p0 = static_cast<const typename F::Type*>(src);
    const uint8_t* sourceBytes = static_cast<const uint8_t*>(src);
    const typename F::Type* p1 = reinterpret_cast<const typename F::Type*>(sourceBytes + srcRB);
    typename F::Type* d = static_cast<typename F::Type*>(dst);

    for (int i = 0; i < count; ++i) {
        const size_t sourceIndex = static_cast<size_t>(i) * 2;
        const typename F::Expanded c00 = F::Expand(p0[sourceIndex]);
        const typename F::Expanded c10 = F::Expand(p1[sourceIndex]);

        const typename F::Expanded c = c00 + c10;
        const typename F::Expanded reduced = shift_right(c, 1);
        d[i] = F::Compact(reduced);
    }
}

template <typename F> void downsample_1_3(void* dst, const void* src, size_t srcRB, int count) {
    SkASSERT(count > 0);
    const typename F::Type* p0 = static_cast<const typename F::Type*>(src);
    const uint8_t* sourceBytes = static_cast<const uint8_t*>(src);
    const typename F::Type* p1 = reinterpret_cast<const typename F::Type*>(sourceBytes + srcRB);
    const typename F::Type* p2 = reinterpret_cast<const typename F::Type*>(sourceBytes + srcRB * 2);
    typename F::Type* d = static_cast<typename F::Type*>(dst);

    for (int i = 0; i < count; ++i) {
        const size_t sourceIndex = static_cast<size_t>(i) * 2;
        const typename F::Expanded c00 = F::Expand(p0[sourceIndex]);
        const typename F::Expanded c10 = F::Expand(p1[sourceIndex]);
        const typename F::Expanded c20 = F::Expand(p2[sourceIndex]);

        const typename F::Expanded c = add_121(c00, c10, c20);
        const typename F::Expanded reduced = shift_right(c, 2);
        d[i] = F::Compact(reduced);
    }
}

template <typename F> void downsample_2_1(void* dst, const void* src, size_t srcRB, int count) {
    SkASSERT(count > 0);
    const typename F::Type* p0 = static_cast<const typename F::Type*>(src);
    typename F::Type* d = static_cast<typename F::Type*>(dst);

    for (int i = 0; i < count; ++i) {
        const size_t sourceIndex = static_cast<size_t>(i) * 2;
        const typename F::Expanded c00 = F::Expand(p0[sourceIndex]);
        const typename F::Expanded c01 = F::Expand(p0[sourceIndex + 1]);

        const typename F::Expanded c = c00 + c01;
        const typename F::Expanded reduced = shift_right(c, 1);
        d[i] = F::Compact(reduced);
    }
}

template <typename F> void downsample_2_2(void* dst, const void* src, size_t srcRB, int count) {
    SkASSERT(count > 0);
    const typename F::Type* p0 = static_cast<const typename F::Type*>(src);
    const uint8_t* sourceBytes = static_cast<const uint8_t*>(src);
    const typename F::Type* p1 = reinterpret_cast<const typename F::Type*>(sourceBytes + srcRB);
    typename F::Type* d = static_cast<typename F::Type*>(dst);

    for (int i = 0; i < count; ++i) {
        const size_t sourceIndex = static_cast<size_t>(i) * 2;
        const typename F::Expanded c00 = F::Expand(p0[sourceIndex]);
        const typename F::Expanded c01 = F::Expand(p0[sourceIndex + 1]);
        const typename F::Expanded c10 = F::Expand(p1[sourceIndex]);
        const typename F::Expanded c11 = F::Expand(p1[sourceIndex + 1]);

        const typename F::Expanded c = c00 + c10 + c01 + c11;
        const typename F::Expanded reduced = shift_right(c, 2);
        d[i] = F::Compact(reduced);
    }
}

template <typename F> void downsample_2_3(void* dst, const void* src, size_t srcRB, int count) {
    SkASSERT(count > 0);
    const typename F::Type* p0 = static_cast<const typename F::Type*>(src);
    const uint8_t* sourceBytes = static_cast<const uint8_t*>(src);
    const typename F::Type* p1 = reinterpret_cast<const typename F::Type*>(sourceBytes + srcRB);
    const typename F::Type* p2 = reinterpret_cast<const typename F::Type*>(sourceBytes + srcRB * 2);
    typename F::Type* d = static_cast<typename F::Type*>(dst);

    for (int i = 0; i < count; ++i) {
        const size_t sourceIndex = static_cast<size_t>(i) * 2;
        const typename F::Expanded c00 = F::Expand(p0[sourceIndex]);
        const typename F::Expanded c01 = F::Expand(p0[sourceIndex + 1]);
        const typename F::Expanded c10 = F::Expand(p1[sourceIndex]);
        const typename F::Expanded c11 = F::Expand(p1[sourceIndex + 1]);
        const typename F::Expanded c20 = F::Expand(p2[sourceIndex]);
        const typename F::Expanded c21 = F::Expand(p2[sourceIndex + 1]);

        const typename F::Expanded firstColumn = add_121(c00, c10, c20);
        const typename F::Expanded secondColumn = add_121(c01, c11, c21);
        const typename F::Expanded c = firstColumn + secondColumn;
        const typename F::Expanded reduced = shift_right(c, 3);
        d[i] = F::Compact(reduced);
    }
}

template <typename F> void downsample_3_1(void* dst, const void* src, size_t srcRB, int count) {
    SkASSERT(count > 0);
    const typename F::Type* p0 = static_cast<const typename F::Type*>(src);
    typename F::Type* d = static_cast<typename F::Type*>(dst);

    typename F::Expanded c02 = F::Expand(p0[0]);
    for (int i = 0; i < count; ++i) {
        const size_t sourceIndex = static_cast<size_t>(i) * 2;
        const typename F::Expanded c00 = c02;
        const typename F::Expanded c01 = F::Expand(p0[sourceIndex + 1]);
        c02 = F::Expand(p0[sourceIndex + 2]);

        const typename F::Expanded c = add_121(c00, c01, c02);
        const typename F::Expanded reduced = shift_right(c, 2);
        d[i] = F::Compact(reduced);
    }
}

template <typename F> void downsample_3_2(void* dst, const void* src, size_t srcRB, int count) {
    SkASSERT(count > 0);
    const typename F::Type* p0 = static_cast<const typename F::Type*>(src);
    const uint8_t* sourceBytes = static_cast<const uint8_t*>(src);
    const typename F::Type* p1 = reinterpret_cast<const typename F::Type*>(sourceBytes + srcRB);
    typename F::Type* d = static_cast<typename F::Type*>(dst);

    // Given pixels:
    // a0 b0 c0 d0 e0 ...
    // a1 b1 c1 d1 e1 ...
    // We want:
    // (a0 + 2*b0 + c0 + a1 + 2*b1 + c1) / 8
    // (c0 + 2*d0 + e0 + c1 + 2*d1 + e1) / 8
    // ...

    typename F::Expanded c0 = F::Expand(p0[0]);
    typename F::Expanded c1 = F::Expand(p1[0]);
    typename F::Expanded c = c0 + c1;
    for (int i = 0; i < count; ++i) {
        const size_t sourceIndex = static_cast<size_t>(i) * 2;
        const typename F::Expanded a = c;

        const typename F::Expanded b0 = F::Expand(p0[sourceIndex + 1]);
        const typename F::Expanded b1 = F::Expand(p1[sourceIndex + 1]);
        const typename F::Expanded b = b0 + b0 + b1 + b1;

        c0 = F::Expand(p0[sourceIndex + 2]);
        c1 = F::Expand(p1[sourceIndex + 2]);
        c = c0 + c1;

        const typename F::Expanded sum = a + b + c;
        const typename F::Expanded reduced = shift_right(sum, 3);
        d[i] = F::Compact(reduced);
    }
}

template <typename F> void downsample_3_3(void* dst, const void* src, size_t srcRB, int count) {
    SkASSERT(count > 0);
    const typename F::Type* p0 = static_cast<const typename F::Type*>(src);
    const uint8_t* sourceBytes = static_cast<const uint8_t*>(src);
    const typename F::Type* p1 = reinterpret_cast<const typename F::Type*>(sourceBytes + srcRB);
    const typename F::Type* p2 = reinterpret_cast<const typename F::Type*>(sourceBytes + srcRB * 2);
    typename F::Type* d = static_cast<typename F::Type*>(dst);

    // Given pixels:
    // a0 b0 c0 d0 e0 ...
    // a1 b1 c1 d1 e1 ...
    // a2 b2 c2 d2 e2 ...
    // We want:
    // (a0 + 2*b0 + c0 + 2*a1 + 4*b1 + 2*c1 + a2 + 2*b2 + c2) / 16
    // (c0 + 2*d0 + e0 + 2*c1 + 4*d1 + 2*e1 + c2 + 2*d2 + e2) / 16
    // ...

    typename F::Expanded c0 = F::Expand(p0[0]);
    typename F::Expanded c1 = F::Expand(p1[0]);
    typename F::Expanded c2 = F::Expand(p2[0]);
    typename F::Expanded c = add_121(c0, c1, c2);
    for (int i = 0; i < count; ++i) {
        const size_t sourceIndex = static_cast<size_t>(i) * 2;
        const typename F::Expanded a = c;

        const typename F::Expanded b0 = F::Expand(p0[sourceIndex + 1]);
        const typename F::Expanded b1 = F::Expand(p1[sourceIndex + 1]);
        const typename F::Expanded b2 = F::Expand(p2[sourceIndex + 1]);
        const typename F::Expanded middleColumn = add_121(b0, b1, b2);
        const typename F::Expanded b = shift_left(middleColumn, 1);

        c0 = F::Expand(p0[sourceIndex + 2]);
        c1 = F::Expand(p1[sourceIndex + 2]);
        c2 = F::Expand(p2[sourceIndex + 2]);
        c = add_121(c0, c1, c2);

        const typename F::Expanded sum = a + b + c;
        const typename F::Expanded reduced = shift_right(sum, 4);
        d[i] = F::Compact(reduced);
    }
}


using FilterProc = void(void* destination, const void* source, size_t sourceRowBytes, int count);
// Row-major [source rows - 1][source columns - 1]. The 1x1 entry is unused.
using FilterTable = std::array<FilterProc*, 9>;

class HQDownSampler final : public SkMipmapDownSampler {
public:
    explicit HQDownSampler(const FilterTable& filters) : fFilters(filters) {}
    void buildLevel(const SkPixmap& destination, const SkPixmap& source) override;
private:
    // The per-format static table outlives every sampler.
    const FilterTable& fFilters;
};

void HQDownSampler::buildLevel(const SkPixmap& destination, const SkPixmap& source) {
    const int width = source.width();
    const int height = source.height();
    const int columns = width == 1 ? 1 : 2 + (width & 1);
    const int rows = height == 1 ? 1 : 2 + (height & 1);
    const size_t filterIndex = static_cast<size_t>((rows - 1) * 3 + columns - 1);
    FilterProc* procedure = fFilters[filterIndex];
    if (procedure == nullptr) {
        return; // The singleton image has no reduced level.
    }
    const int outputWidth = destination.width();
    const int outputHeight = destination.height();
    SkASSERT(outputWidth == std::max(1, width / 2));
    SkASSERT(outputHeight == std::max(1, height / 2));
    const size_t sourceStride = source.rowBytes();
    const size_t destinationStride = destination.rowBytes();
    const uint8_t* input = static_cast<const uint8_t*>(source.addr());
    uint8_t* output = static_cast<uint8_t*>(destination.writable_addr());
    for (int y = 0; y < outputHeight; ++y) {
        // Form live row addresses only, including one-row and exact-sized buffers.
        const size_t sourceOffset = static_cast<size_t>(y) * 2 * sourceStride;
        const size_t destinationOffset = static_cast<size_t>(y) * destinationStride;
        procedure(output + destinationOffset, input + sourceOffset, sourceStride, outputWidth);
    }
}

template <typename F>
std::unique_ptr<SkMipmapDownSampler> make_sampler() {
    static constexpr FilterTable filters{
        nullptr, downsample_2_1<F>, downsample_3_1<F>,
        downsample_1_2<F>, downsample_2_2<F>, downsample_3_2<F>,
        downsample_1_3<F>, downsample_2_3<F>, downsample_3_3<F>};
    std::unique_ptr<SkMipmapDownSampler> sampler = std::make_unique<HQDownSampler>(filters);
    return sampler;
}

} // namespace

std::unique_ptr<SkMipmapDownSampler> SkMipmap::MakeDownSampler(const SkPixmap& root) {
    std::unique_ptr<SkMipmapDownSampler> sampler{};
    switch (root.colorType()) {
        case kRGBA_8888_SkColorType:
        case kBGRA_8888_SkColorType:
            sampler = make_sampler<ColorTypeFilter_8888>();
            break;
        case kRGB_565_SkColorType:
            sampler = make_sampler<ColorTypeFilter_565>();
            break;
        case kARGB_4444_SkColorType:
            sampler = make_sampler<ColorTypeFilter_4444>();
            break;
        case kAlpha_8_SkColorType:
        case kGray_8_SkColorType:
        case kR8_unorm_SkColorType:
            sampler = make_sampler<ColorTypeFilter_8>();
            break;
        case kRGBA_F16Norm_SkColorType:
        case kRGBA_F16_SkColorType:
            sampler = make_sampler<ColorTypeFilter_RGBA_F16>();
            break;
        case kR8G8_unorm_SkColorType:
            sampler = make_sampler<ColorTypeFilter_88>();
            break;
        case kR16G16_unorm_SkColorType:
            sampler = make_sampler<ColorTypeFilter_1616>();
            break;
        case kA16_unorm_SkColorType:
        case kR16_unorm_SkColorType:
            sampler = make_sampler<ColorTypeFilter_16>();
            break;
        case kRGBA_1010102_SkColorType:
        case kBGRA_1010102_SkColorType:
            sampler = make_sampler<ColorTypeFilter_1010102>();
            break;
        case kA16_float_SkColorType:
        case kR16_float_SkColorType:
            sampler = make_sampler<ColorTypeFilter_F16>();
            break;
        case kR16G16_float_SkColorType:
            sampler = make_sampler<ColorTypeFilter_F16F16>();
            break;
        case kR16G16B16A16_unorm_SkColorType:
            sampler = make_sampler<ColorTypeFilter_16161616>();
            break;
        default:
            // Formats without an admitted filter remain unsupported.
            break;
    }
    return sampler;
}

#endif
