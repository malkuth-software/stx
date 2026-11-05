/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPixmap.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkColorType.h"
#include "include/core/SkUnPreMultiply.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkTPin.h"
#include "src/core/SkColorData.h"
#include "src/core/SkColorPriv.h"
#include "src/core/SkConvertPixels.h"
#include "src/core/SkHalf.h"
#include "src/core/SkImageInfoPriv.h"
#include "src/core/SkMask.h"
#include "src/core/SkReadPixelsRec.h"
#include "src/core/SkSafeMath.h"
#include "src/core/SkSwizzlePriv.h"
#include "src/core/SkVx.h"
#include "src/opts/SkMemset_opts.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <utility>

void SkPixmap::reset() {
    fPixels = nullptr;
    fRowBytes = 0;
    fInfo = SkImageInfo::MakeUnknown();
}

void SkPixmap::reset(const SkImageInfo& info, const void* addr, size_t rowBytes) {
    if (addr) {
        SkDEBUGCODE(info.validate());
        SkASSERT(info.validRowBytes(rowBytes));
    }
    fPixels = addr;
    fRowBytes = rowBytes;
    fInfo = info;
}

bool SkPixmap::reset(const SkMask& src) {
    if (SkMask::kA8_Format == src.fFormat) {
        const SkImageInfo info = SkImageInfo::MakeA8(src.fBounds.width(), src.fBounds.height());
        reset(info, src.fImage, src.fRowBytes);
        return true;
    }
    reset();
    return false;
}

void SkPixmap::setColorSpace(sk_sp<SkColorSpace> cs) {
    fInfo = fInfo.makeColorSpace(std::move(cs));
}

SkColorSpace* SkPixmap::colorSpace() const {
    SkColorSpace* space = fInfo.colorSpace();
    return space;
}

sk_sp<SkColorSpace> SkPixmap::refColorSpace() const {
    sk_sp<SkColorSpace> space = fInfo.refColorSpace();
    return space;
}

bool SkPixmap::extractSubset(SkPixmap* result, const SkIRect& subset) const {
    if (result == nullptr) return false;
    SkIRect srcRect = SkIRect::MakeEmpty();
    SkIRect r = SkIRect::MakeEmpty();
    srcRect.setWH(width(), height());
    if (!r.intersect(srcRect, subset)) {
        return false;   // r is empty (i.e. no intersection)
    }

    // If the upper left of the rectangle was outside the bounds of this SkBitmap, we should have
    // exited above.
    SkASSERT(static_cast<unsigned>(r.fLeft) < static_cast<unsigned>(width()));
    SkASSERT(static_cast<unsigned>(r.fTop) < static_cast<unsigned>(height()));

    const void* pixels = nullptr;
    if (fPixels) {
        SkSafeMath extent;
        const size_t bpp = static_cast<size_t>(fInfo.bytesPerPixel());
        const size_t row = extent.mul(static_cast<size_t>(r.fTop), fRowBytes);
        const size_t column = extent.mul(static_cast<size_t>(r.fLeft), bpp);
        const size_t offset = extent.add(row, column);
        if (!extent.ok()) return false;
        pixels = static_cast<const uint8_t*>(fPixels) + offset;
    }
    const SkImageInfo subset_info = fInfo.makeDimensions(r.size());
    (*result).reset(subset_info, pixels, fRowBytes);
    return true;
}

// Pixel coordinates and the backing extent have been validated by the caller.
static const void* fast_getaddr(const SkPixmap& pm, int x, int y) {
    const size_t column = static_cast<size_t>(x) << SkColorTypeShiftPerPixel(pm.colorType());
    const size_t row = static_cast<size_t>(y) * pm.rowBytes();
    const char* pixels = static_cast<const char*>(pm.addr());
    const void* address = pixels + row + column;
    return address;
}

static SkColor unpremultiplied_color(uint32_t color, bool needs_unpremul) {
    if (needs_unpremul) {
        const SkColor converted = SkUnPreMultiply::PMColorToColor(color);
        return converted;
    }
    const SkColor converted = SkSwizzle_BGRA_to_PMColor(color);
    return converted;
}

static float srgb_to_linear(float x) {
    if (x <= 0.04045f) {
        const float linear = x * (1.0f / 12.92f);
        return linear;
    }
    const float shifted = x * (1.0f / 1.055f) + (0.055f / 1.055f);
    const float linear = std::pow(shifted, 2.4f);
    return linear;
}

static skvx::float4 load_half_color(const void* pixels) {
    const skvx::half4 packed = skvx::half4::Load(pixels);
    const skvx::float4 color = from_half(packed);
    return color;
}

// Each specialization is a named typed fill kernel. The chosen format stays
// outside the row loop and the caller has validated writable storage.
template <typename Pixel>
static void fill_pixel_row(void* pixels, uint64_t value, int count) {
    const Pixel pixel = static_cast<Pixel>(value);
    SkASSERT(value == pixel);
    Pixel* row = static_cast<Pixel*>(pixels);
    SK_OPTS_NS::memsetT(row, pixel, count);
}

float SkPixmap::getAlphaf(int x, int y) const {
    SkASSERT(addr());
    SkASSERT(static_cast<unsigned int>(x) < static_cast<unsigned int>(width()));
    SkASSERT(static_cast<unsigned int>(y) < static_cast<unsigned int>(height()));

    float value = 0;
    const void* srcPtr = fast_getaddr(*this, x, y);

    switch (colorType()) {
        case kUnknown_SkColorType:
            return 0;
        case kGray_8_SkColorType:
        case kR8G8_unorm_SkColorType:
        case kR16_unorm_SkColorType:
        case kR16_float_SkColorType:
        case kR16G16_unorm_SkColorType:
        case kR16G16_float_SkColorType:
        case kRGB_565_SkColorType:
        case kRGB_888x_SkColorType:
        case kRGB_101010x_SkColorType:
        case kBGR_101010x_SkColorType:
        case kBGR_101010x_XR_SkColorType:
        case kRGB_F16F16F16x_SkColorType:
        case kR8_unorm_SkColorType:
            return 1;
        case kAlpha_8_SkColorType:
            value = static_cast<const uint8_t*>(srcPtr)[0] * (1.0f/255);
            break;
        case kA16_unorm_SkColorType:
            value = static_cast<const uint16_t*>(srcPtr)[0] * (1.0f/65535);
            break;
        case kA16_float_SkColorType: {
            SkHalf half = static_cast<const SkHalf*>(srcPtr)[0];
            value = SkHalfToFloat(half);
            break;
        }
        case kARGB_4444_SkColorType: {
            uint16_t u16 = static_cast<const uint16_t*>(srcPtr)[0];
            value = SkGetPackedA4444(u16) * (1.0f/15);
            break;
        }
        case kRGBA_8888_SkColorType:
        case kBGRA_8888_SkColorType:
        case kSRGBA_8888_SkColorType:
            value = static_cast<const uint8_t*>(srcPtr)[3] * (1.0f/255);
            break;
        case kRGBA_1010102_SkColorType:
        case kBGRA_1010102_SkColorType: {
            uint32_t u32 = static_cast<const uint32_t*>(srcPtr)[0];
            value = (u32 >> 30) * (1.0f/3);
            break;
        }
        case kBGRA_10101010_XR_SkColorType: {
            uint64_t u64 = static_cast<const uint64_t*>(srcPtr)[0];
            value = ((u64 >> 54) - 384) / 510.f;
            break;
        }
        case kRGBA_10x6_SkColorType: {
            uint64_t u64 = static_cast<const uint64_t*>(srcPtr)[0];
            value = (u64 >> 54) * (1.0f/1023);
            break;
        }
        case kR16G16B16A16_unorm_SkColorType: {
            uint64_t u64 = static_cast<const uint64_t*>(srcPtr)[0];
            value = (u64 >> 48) * (1.0f/65535);
            break;
        }
        case kRGBA_F16Norm_SkColorType:
        case kRGBA_F16_SkColorType: {
            const skvx::float4 color = load_half_color(srcPtr);
            value = color[3];
            break;
        }
        case kRGBA_F32_SkColorType:
            value = static_cast<const float*>(srcPtr)[3];
            break;
    }
    return value;
}

bool SkPixmap::readPixels(const SkImageInfo& dstInfo, void* dstPixels, size_t dstRB,
                          int x, int y) const {
    if (!SkColorTypeIsValid(static_cast<unsigned int>(colorType())) ||
        !SkAlphaTypeIsValid(static_cast<unsigned int>(alphaType())) ||
        !SkColorTypeIsValid(static_cast<unsigned int>(dstInfo.colorType())) ||
        !SkAlphaTypeIsValid(static_cast<unsigned int>(dstInfo.alphaType()))) return false;
    if (fPixels == nullptr || !fInfo.validRowBytes(fRowBytes)) return false;
    if (!checkedByteSize()) return false;
    if (!SkImageInfoValidConversion(dstInfo, fInfo)) {
        return false;
    }

    SkReadPixelsRec rec(dstInfo, dstPixels, dstRB, x, y);
    if (!rec.trim(fInfo.width(), fInfo.height())) {
        return false;
    }

    const void* srcPixels = addr(rec.fX, rec.fY);
    const SkImageInfo srcInfo = fInfo.makeDimensions(rec.fInfo.dimensions());
    const bool copied = SkConvertPixels(rec.fInfo, rec.fPixels, rec.fRowBytes, srcInfo, srcPixels,
                                         rowBytes());
    return copied;
}

SkColor SkPixmap::getColor(int x, int y) const {
    SkASSERT(addr());
    SkASSERT(static_cast<unsigned int>(x) < static_cast<unsigned int>(width()));
    SkASSERT(static_cast<unsigned int>(y) < static_cast<unsigned int>(height()));

    const bool needsUnpremul = (kPremul_SkAlphaType == fInfo.alphaType());

    switch (colorType()) {
        case kGray_8_SkColorType: {
            uint8_t value = *addr8(x, y);
            const SkColor color = SkColorSetRGB(value, value, value);
            return color;
        }
        case kR8_unorm_SkColorType: {
            const SkColor color = SkColorSetRGB(*addr8(x, y), 0, 0);
            return color;
        }
        case kAlpha_8_SkColorType: {
            const SkColor color = SkColorSetA(0, *addr8(x, y));
            return color;
        }
        case kA16_unorm_SkColorType: {
            const uint16_t pixel = *addr16(x, y);
            const float scaled = pixel * (255.0f / 65535.0f);
            const U8CPU alpha = static_cast<U8CPU>(scaled);
            const SkColor color = SkColorSetA(0, alpha);
            return color;
        }
        case kA16_float_SkColorType: {
            const SkHalf pixel = *addr16(x, y);
            const float alpha = SkHalfToFloat(pixel);
            const U8CPU scaled = static_cast<U8CPU>(255.0f * alpha);
            const SkColor color = SkColorSetA(0, scaled);
            return color;
        }
        case kR16_float_SkColorType: {
            const SkHalf pixel = *addr16(x, y);
            const float red = SkHalfToFloat(pixel);
            const U8CPU scaled = static_cast<U8CPU>(255.0f * red);
            const SkColor color = SkColorSetRGB(scaled, 0, 0);
            return color;
        }
        case kRGB_565_SkColorType: {
            const SkColor color = SkPixel16ToColor(*addr16(x, y));
            return color;
        }
        case kARGB_4444_SkColorType: {
            SkPMColor c = SkPixel4444ToPixel32(*addr16(x, y));
            const SkColor color = unpremultiplied_color(c, needsUnpremul);
            return color;
        }
        case kR8G8_unorm_SkColorType: {
            uint16_t value = *addr16(x, y);
            const uint8_t r = static_cast<uint8_t>(value);
            const uint8_t g = static_cast<uint8_t>(value >> 8);
            const SkColor color = SkColorSetRGB(r, g, 0);
            return color;
        }
        case kR16_unorm_SkColorType: {
            uint16_t value = *addr16(x, y);
            const U8CPU red = static_cast<U8CPU>(value * (255.0f / 65535.0f));
            const SkColor color = SkColorSetRGB(red, 0, 0);
            return color;
        }
        case kR16G16_unorm_SkColorType: {
            uint32_t value = *addr32(x, y);
            const uint8_t r = static_cast<uint8_t>((value & 0xffff) * (255.0f / 65535.0f));
            const uint8_t g = static_cast<uint8_t>((value >> 16) * (255.0f / 65535.0f));
            const SkColor color = SkColorSetRGB(r, g, 0);
            return color;
        }
        case kR16G16_float_SkColorType: {
            uint32_t value = *addr32(x, y);
            float r = SkHalfToFloat(static_cast<SkHalf>(value));
            float g = SkHalfToFloat(static_cast<SkHalf>(value >> 16));
            const uint8_t r8 = static_cast<uint8_t>(255.0f * r);
            const uint8_t g8 = static_cast<uint8_t>(255.0f * g);
            const SkColor color = SkColorSetRGB(r8, g8, 0);
            return color;
        }
        case kRGB_888x_SkColorType: {
            uint32_t value = *addr32(x, y);
            const SkColor color = SkSwizzle_RB(value | 0xff000000);
            return color;
        }
        case kBGRA_8888_SkColorType: {
            uint32_t value = *addr32(x, y);
            const SkPMColor premul = SkSwizzle_BGRA_to_PMColor(value);
            const SkColor color = unpremultiplied_color(premul, needsUnpremul);
            return color;
        }
        case kRGBA_8888_SkColorType: {
            uint32_t value = *addr32(x, y);
            const SkPMColor premul = SkSwizzle_RGBA_to_PMColor(value);
            const SkColor color = unpremultiplied_color(premul, needsUnpremul);
            return color;
        }
        case kSRGBA_8888_SkColorType: {
            uint32_t value = *addr32(x, y);
            float r = ((value >>  0) & 0xff) * (1/255.0f);
            float g = ((value >>  8) & 0xff) * (1/255.0f);
            float b = ((value >> 16) & 0xff) * (1/255.0f);
            float a = ((value >> 24) & 0xff) * (1/255.0f);

            r = srgb_to_linear(r);
            g = srgb_to_linear(g);
            b = srgb_to_linear(b);
            if (a != 0 && needsUnpremul) {
                r = SkTPin(r/a, 0.0f, 1.0f);
                g = SkTPin(g/a, 0.0f, 1.0f);
                b = SkTPin(b/a, 0.0f, 1.0f);
            }
            r *= 255.0f;
            g *= 255.0f;
            b *= 255.0f;
            a *= 255.0f;
            const U8CPU a8 = static_cast<U8CPU>(a);
            const U8CPU r8 = static_cast<U8CPU>(r);
            const U8CPU g8 = static_cast<U8CPU>(g);
            const U8CPU b8 = static_cast<U8CPU>(b);
            const SkColor color = SkColorSetARGB(a8, r8, g8, b8);
            return color;
        }
        case kRGB_101010x_SkColorType: {
            uint32_t value = *addr32(x, y);
            // Convert 10-bit rgb to 8-bit rgb
            const U8CPU r = static_cast<U8CPU>(((value >> 0) & 0x3ff) * (255.0f / 1023.0f));
            const U8CPU g = static_cast<U8CPU>(((value >> 10) & 0x3ff) * (255.0f / 1023.0f));
            const U8CPU b = static_cast<U8CPU>(((value >> 20) & 0x3ff) * (255.0f / 1023.0f));
            const SkColor color = SkColorSetRGB(r, g, b);
            return color;
        }
        case kBGR_101010x_XR_SkColorType: {
            SkASSERT(false);
            return 0;
        }
        case kBGR_101010x_SkColorType: {
            uint32_t value = *addr32(x, y);
            // Convert 10-bit bgr to 8-bit rgb
            const U8CPU r = static_cast<U8CPU>(((value >> 20) & 0x3ff) * (255.0f / 1023.0f));
            const U8CPU g = static_cast<U8CPU>(((value >> 10) & 0x3ff) * (255.0f / 1023.0f));
            const U8CPU b = static_cast<U8CPU>(((value >> 0) & 0x3ff) * (255.0f / 1023.0f));
            const SkColor color = SkColorSetRGB(r, g, b);
            return color;
        }
        case kBGRA_1010102_SkColorType:
        case kRGBA_1010102_SkColorType: {
            uint32_t value = *addr32(x, y);
            float r = ((value >>  0) & 0x3ff) * (1/1023.0f);
            float g = ((value >> 10) & 0x3ff) * (1/1023.0f);
            float b = ((value >> 20) & 0x3ff) * (1/1023.0f);
            float a = ((value >> 30) & 0x3  ) * (1/   3.0f);
            if (colorType() == kBGRA_1010102_SkColorType) {
                std::swap(r, b);
            }
            if (a != 0 && needsUnpremul) {
                r = SkTPin(r/a, 0.0f, 1.0f);
                g = SkTPin(g/a, 0.0f, 1.0f);
                b = SkTPin(b/a, 0.0f, 1.0f);
            }
            b *= 255.0f;
            g *= 255.0f;
            r *= 255.0f;
            a *= 255.0f;
            const U8CPU a8 = static_cast<U8CPU>(a);
            const U8CPU r8 = static_cast<U8CPU>(r);
            const U8CPU g8 = static_cast<U8CPU>(g);
            const U8CPU b8 = static_cast<U8CPU>(b);
            const SkColor color = SkColorSetARGB(a8, r8, g8, b8);
            return color;
        }
        case kBGRA_10101010_XR_SkColorType: {
            SkASSERT(false);
            return 0;
        }
        case kRGBA_10x6_SkColorType: {
            uint64_t value = *addr64(x, y);
            const U8CPU a = static_cast<U8CPU>(((value >> 54) & 0x3ff) * (255.0f / 1023.0f));
            const U8CPU r = static_cast<U8CPU>(((value >> 6) & 0x3ff) * (255.0f / 1023.0f));
            const U8CPU g = static_cast<U8CPU>(((value >> 22) & 0x3ff) * (255.0f / 1023.0f));
            const U8CPU b = static_cast<U8CPU>(((value >> 38) & 0x3ff) * (255.0f / 1023.0f));
            const SkColor color = SkColorSetARGB(a, r, g, b);
            return color;
        }
        case kR16G16B16A16_unorm_SkColorType: {
            uint64_t value = *addr64(x, y);
            float r = ((value >>  0) & 0xffff) * (1/65535.0f);
            float g = ((value >> 16) & 0xffff) * (1/65535.0f);
            float b = ((value >> 32) & 0xffff) * (1/65535.0f);
            float a = ((value >> 48) & 0xffff) * (1/65535.0f);
            if (a != 0 && needsUnpremul) {
                r *= (1.0f/a);
                g *= (1.0f/a);
                b *= (1.0f/a);
            }
            r *= 255.0f;
            g *= 255.0f;
            b *= 255.0f;
            a *= 255.0f;
            const U8CPU a8 = static_cast<U8CPU>(a);
            const U8CPU r8 = static_cast<U8CPU>(r);
            const U8CPU g8 = static_cast<U8CPU>(g);
            const U8CPU b8 = static_cast<U8CPU>(b);
            const SkColor color = SkColorSetARGB(a8, r8, g8, b8);
            return color;
        }
        case kRGB_F16F16F16x_SkColorType: {
            const uint64_t* pixels = addr64(x, y);
            skvx::float4 p4 = load_half_color(pixels);
            p4[3] = 1.0f;
            // p4 is RGBA, but we want BGRA, so we need to swap next
            const skvx::float4 bgra = swizzle_rb(p4);
            const SkColor color = Sk4f_toL32(bgra);
            return color;
        }
        case kRGBA_F16Norm_SkColorType:
        case kRGBA_F16_SkColorType: {
            const uint64_t* pixels = addr64(x, y);
            skvx::float4 p4 = load_half_color(pixels);
            if (p4[3] && needsUnpremul) {
                float inva = 1 / p4[3];
                p4 = p4 * skvx::float4(inva, inva, inva, 1);
            }
            // p4 is RGBA, but we want BGRA, so we need to swap next
            const skvx::float4 bgra = swizzle_rb(p4);
            const SkColor color = Sk4f_toL32(bgra);
            return color;
        }
        case kRGBA_F32_SkColorType: {
            const float* rgba = static_cast<const float*>(fast_getaddr(*this, x, y));
            skvx::float4 p4 = skvx::float4::Load(rgba);
            // From here on, just like F16:
            if (p4[3] && needsUnpremul) {
                float inva = 1 / p4[3];
                p4 = p4 * skvx::float4(inva, inva, inva, 1);
            }
            // p4 is RGBA, but we want BGRA, so we need to swap next
            const skvx::float4 bgra = swizzle_rb(p4);
            const SkColor color = Sk4f_toL32(bgra);
            return color;
        }
        case kUnknown_SkColorType:
            break;
    }
    SkDEBUGFAIL("");
    const SkColor color = SkColorSetARGB(0, 0, 0, 0);
    return color;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

SkColor4f SkPixmap::getColor4f(int x, int y) const {
    SkASSERT(addr());
    SkASSERT(static_cast<unsigned int>(x) < static_cast<unsigned int>(width()));
    SkASSERT(static_cast<unsigned int>(y) < static_cast<unsigned int>(height()));

    const bool needsUnpremul = (kPremul_SkAlphaType == fInfo.alphaType());

    switch (colorType()) {
        case kGray_8_SkColorType: {
            float value = *addr8(x, y) / 255.0f;
            const SkColor4f color = SkColor4f{value, value, value, 1.0f};
            return color;
        }
        case kR8_unorm_SkColorType: {
            const SkColor4f color = SkColor4f{(*addr8(x, y) / 255.0f), 0.0f, 0.0f, 1.0f};
            return color;
        }
        case kAlpha_8_SkColorType: {
            const SkColor4f color = SkColor4f{0.0f, 0.0f, 0.0f, (*addr8(x, y) / 255.0f)};
            return color;
        }
        case kR16_unorm_SkColorType: {
            const SkColor4f color = SkColor4f{*addr16(x, y) / 65535.0f, 0.0f, 0.0f, 1.0f};
            return color;
        }
        case kA16_unorm_SkColorType: {
            const SkColor4f color = SkColor4f{0.0f, 0.0f, 0.0f, (*addr16(x, y) / 65535.0f)};
            return color;
        }
        case kR16_float_SkColorType: {
            const SkColor4f color = SkColor4f{SkHalfToFloat(*addr16(x, y)), 0.f, 0.f, 1.f};
            return color;
        }
        case kA16_float_SkColorType: {
            const SkColor4f color = SkColor4f{0.0f, 0.0f, 0.0f, SkHalfToFloat(*addr16(x, y))};
            return color;
        }
        case kRGB_565_SkColorType: {
            const uint16_t pixel = *addr16(x, y);
            const SkColor packed = SkPixel16ToColor(pixel);
            const SkColor4f color = SkColor4f::FromColor(packed);
            return color;
        }
        case kARGB_4444_SkColorType: {
            SkPMColor c = SkPixel4444ToPixel32(*addr16(x, y));
            const SkColor unpremul = unpremultiplied_color(c, needsUnpremul);
            const SkColor4f color = SkColor4f::FromColor(unpremul);
            return color;
        }
        case kR8G8_unorm_SkColorType: {
            uint16_t value = *addr16(x, y);
            const uint8_t r = static_cast<uint8_t>(value);
            const uint8_t g = static_cast<uint8_t>(value >> 8);
            const SkColor packed = SkColorSetRGB(r, g, 0);
            const SkColor4f color = SkColor4f::FromColor(packed);
            return color;
        }
        case kR16G16_unorm_SkColorType: {
            uint32_t value = *addr32(x, y);
            float r = ((value >>  0) & 0xffff) * (1.0f / 65535.0f);
            float g = ((value >> 16) & 0xffff) * (1.0f / 65535.0f);
            const SkColor4f color = SkColor4f{r, g, 0.0f, 1.0f};
            return color;
        }
        case kR16G16_float_SkColorType: {
            uint32_t value = *addr32(x, y);
            float r = SkHalfToFloat(static_cast<SkHalf>(value));
            float g = SkHalfToFloat(static_cast<SkHalf>(value >> 16));
            const SkColor4f color = SkColor4f{r, g, 0.0f, 1.0f};
            return color;
        }
        case kRGB_888x_SkColorType: {
            SkColor c = SkSwizzle_RB(*addr32(x, y) | 0xff000000);
            const SkColor4f color = SkColor4f::FromColor(c);
            return color;
        }
        case kBGRA_8888_SkColorType: {
            SkPMColor c = SkSwizzle_BGRA_to_PMColor(*addr32(x, y));
            const SkColor unpremul = unpremultiplied_color(c, needsUnpremul);
            const SkColor4f color = SkColor4f::FromColor(unpremul);
            return color;
        }
        case kRGBA_8888_SkColorType: {
            SkPMColor c = SkSwizzle_RGBA_to_PMColor(*addr32(x, y));
            const SkColor unpremul = unpremultiplied_color(c, needsUnpremul);
            const SkColor4f color = SkColor4f::FromColor(unpremul);
            return color;
        }
        case kSRGBA_8888_SkColorType: {

            uint32_t value = *addr32(x, y);
            float r = ((value >>  0) & 0xff) * (1 / 255.0f);
            float g = ((value >>  8) & 0xff) * (1 / 255.0f);
            float b = ((value >> 16) & 0xff) * (1 / 255.0f);
            float a = ((value >> 24) & 0xff) * (1 / 255.0f);
            r = srgb_to_linear(r);
            g = srgb_to_linear(g);
            b = srgb_to_linear(b);
            if (a != 0 && needsUnpremul) {
                r = SkTPin(r / a, 0.0f, 1.0f);
                g = SkTPin(g / a, 0.0f, 1.0f);
                b = SkTPin(b / a, 0.0f, 1.0f);
            }
            const SkColor4f color = SkColor4f{r, g, b, a};
            return color;
        }
        case kBGR_101010x_XR_SkColorType: {
            SkASSERT(false);
            return {};
        }
        case kRGB_101010x_SkColorType: {
            uint32_t value = *addr32(x, y);
            // Convert 10-bit RGB to floats
            const SkColor4f color = SkColor4f{((value >>  0) & 0x3ff) / (1023.0f),
                             ((value >> 10) & 0x3ff) / (1023.0f),
                             ((value >> 20) & 0x3ff) / (1023.0f),
                             1.0f};
            return color;
        }
        case kBGR_101010x_SkColorType: {
            uint32_t value = *addr32(x, y);
            // Convert 10-bit BGR color values to RGBA floats
            const SkColor4f color = SkColor4f{((value >> 20) & 0x3ff) / (1023.0f),
                             ((value >> 10) & 0x3ff) / (1023.0f),
                             ((value >>  0) & 0x3ff) / (1023.0f),
                             1.0f};
            return color;
        }
        case kRGBA_1010102_SkColorType:
        case kBGRA_1010102_SkColorType: {
            uint32_t value = *addr32(x, y);
            // The low ten bits are red in RGBA and blue in BGRA.
            float r = ((value >> 0) & 0x3ff) * (1.0f / 1023.0f);
            float g = ((value >> 10) & 0x3ff) * (1.0f / 1023.0f);
            float b = ((value >> 20) & 0x3ff) * (1.0f / 1023.0f);
            float a = ((value >> 30) & 0x3) * (1.0f / 3.0f);
            if (colorType() == kBGRA_1010102_SkColorType) std::swap(r, b);
            if (a != 0 && needsUnpremul) {
                r = SkTPin(r / a, 0.0f, 1.0f);
                g = SkTPin(g / a, 0.0f, 1.0f);
                b = SkTPin(b / a, 0.0f, 1.0f);
            }
            const SkColor4f color = SkColor4f{r, g, b, a};
            return color;
        }
        case kBGRA_10101010_XR_SkColorType: {
            SkASSERT(false);
            return {};
        }
        case kRGBA_10x6_SkColorType: {
            uint64_t value = *addr64(x, y);
            const SkColor4f color = SkColor4f{((value >>  6) & 0x3ff) * (1/1023.0f),
                             ((value >> 22) & 0x3ff) * (1/1023.0f),
                             ((value >> 38) & 0x3ff) * (1/1023.0f),
                             ((value >> 54) & 0x3ff) * (1/1023.0f)};
            return color;
        }
        case kR16G16B16A16_unorm_SkColorType: {
            uint64_t value = *addr64(x, y);
            float r = ((value >>  0) & 0xffff) * (1 / 65535.0f);
            float g = ((value >> 16) & 0xffff) * (1 / 65535.0f);
            float b = ((value >> 32) & 0xffff) * (1 / 65535.0f);
            float a = ((value >> 48) & 0xffff) * (1 / 65535.0f);
            if (a != 0 && needsUnpremul) {
                r *= (1.0f / a);
                g *= (1.0f / a);
                b *= (1.0f / a);
            }
            const SkColor4f color = SkColor4f{r, g, b, a};
            return color;
        }
        case kRGBA_F16Norm_SkColorType:
        case kRGBA_F16_SkColorType: {
            const uint64_t* pixels = addr64(x, y);
            skvx::float4 p4 = load_half_color(pixels);
            if (p4[3] && needsUnpremul) {
                float inva = 1 / p4[3];
                p4 = p4 * skvx::float4(inva, inva, inva, 1);
            }
            const SkColor4f color = SkColor4f{p4[0], p4[1], p4[2], p4[3]};
            return color;
        }
        case kRGB_F16F16F16x_SkColorType: {
            const uint64_t* pixels = addr64(x, y);
            skvx::float4 p4 = load_half_color(pixels);
            p4[3] = 1.0f;
            const SkColor4f color = SkColor4f{p4[0], p4[1], p4[2], p4[3]};
            return color;
        }
        case kRGBA_F32_SkColorType: {
            const float* rgba = static_cast<const float*>(fast_getaddr(*this, x, y));
            skvx::float4 p4 = skvx::float4::Load(rgba);
            // From here on, just like F16:
            if (p4[3] && needsUnpremul) {
                float inva = 1 / p4[3];
                p4 = p4 * skvx::float4(inva, inva, inva, 1);
            }
            const SkColor4f color = SkColor4f{p4[0], p4[1], p4[2], p4[3]};
            return color;
        }
        case kUnknown_SkColorType:
            break;
    }
    SkDEBUGFAIL("");
    return SkColors::kTransparent;
}

bool SkPixmap::computeIsOpaque() const {
    const int height = fInfo.height();
    const int width = fInfo.width();
    const SkColorType format = colorType();
    if (!SkColorTypeIsValid(static_cast<unsigned int>(format)) || format == kUnknown_SkColorType) return false;
    if (SkColorTypeIsAlwaysOpaque(format)) return true;
    if (width <= 0 || height <= 0) return true;
    if (fPixels == nullptr || !fInfo.validRowBytes(fRowBytes)) return false;
    if (!checkedByteSize()) return false;

    switch (format) {
        case kAlpha_8_SkColorType: {
            unsigned a = 0xFF;
            for (int y = 0; y < height; ++y) {
                const uint8_t* row = addr8(0, y);
                for (int x = 0; x < width; ++x) {
                    a &= row[x];
                }
                if (0xFF != a) {
                    return false;
                }
            }
            return true;
        }
        case kA16_unorm_SkColorType: {
            unsigned a = 0xFFFF;
            for (int y = 0; y < height; ++y) {
                const uint16_t* row = addr16(0, y);
                for (int x = 0; x < width; ++x) {
                    a &= row[x];
                }
                if (0xFFFF != a) {
                    return false;
                }
            }
            return true;
        }
        case kA16_float_SkColorType: {
            for (int y = 0; y < height; ++y) {
                const SkHalf* row = addr16(0, y);
                for (int x = 0; x < width; ++x) {
                    if (row[x] < SK_Half1) {
                        return false;
                    }
                }
            }
            return true;
        }
        case kRGB_565_SkColorType:
        case kGray_8_SkColorType:
        case kR8G8_unorm_SkColorType:
        case kR16_unorm_SkColorType:
        case kR16_float_SkColorType:
        case kR16G16_unorm_SkColorType:
        case kR16G16_float_SkColorType:
        case kRGB_888x_SkColorType:
        case kRGB_101010x_SkColorType:
        case kBGR_101010x_SkColorType:
        case kRGB_F16F16F16x_SkColorType:
        case kBGR_101010x_XR_SkColorType:
        case kR8_unorm_SkColorType:
            return true;
        case kARGB_4444_SkColorType: {
            unsigned c = 0xFFFF;
            for (int y = 0; y < height; ++y) {
                const SkPMColor16* row = addr16(0, y);
                for (int x = 0; x < width; ++x) {
                    c &= row[x];
                }
                if (0xF != SkGetPackedA4444(c)) {
                    return false;
                }
            }
            return true;
        }
        case kBGRA_8888_SkColorType:
        case kRGBA_8888_SkColorType:
        case kSRGBA_8888_SkColorType: {
            SkPMColor c = ~SkPMColor{0};
            for (int y = 0; y < height; ++y) {
                const SkPMColor* row = addr32(0, y);
                for (int x = 0; x < width; ++x) {
                    c &= row[x];
                }
                if (0xFF != SkGetPackedA32(c)) {
                    return false;
                }
            }
            return true;
        }
        case kRGBA_F16Norm_SkColorType:
        case kRGBA_F16_SkColorType: {
            const uint8_t* pixels = static_cast<const uint8_t*>(addr());
            for (int y = 0; y < height; ++y) {
                const size_t offset = static_cast<size_t>(y) * fRowBytes;
                const SkHalf* row = reinterpret_cast<const SkHalf*>(pixels + offset);
                for (int x = 0; x < width; ++x) {
                    if (row[4 * x + 3] < SK_Half1) {
                        return false;
                    }
                }
            }
            return true;
        }
        case kRGBA_F32_SkColorType: {
            const uint8_t* pixels = static_cast<const uint8_t*>(addr());
            for (int y = 0; y < height; ++y) {
                const size_t offset = static_cast<size_t>(y) * fRowBytes;
                const float* row = reinterpret_cast<const float*>(pixels + offset);
                for (int x = 0; x < width; ++x) {
                    if (row[4 * x + 3] < 1.0f) {
                        return false;
                    }
                }
            }
            return true;
        }
        case kRGBA_1010102_SkColorType:
        case kBGRA_1010102_SkColorType: {
            uint32_t c = ~0;
            for (int y = 0; y < height; ++y) {
                const uint32_t* row = addr32(0, y);
                for (int x = 0; x < width; ++x) {
                    c &= row[x];
                }
                if (0b11 != c >> 30) {
                    return false;
                }
            }
            return true;
        }
        case kBGRA_10101010_XR_SkColorType:{
            static constexpr uint64_t kOne = 510 + 384;
            for (int y = 0; y < height; ++y) {
                const uint64_t* row = addr64(0, y);
                for (int x = 0; x < width; ++x) {
                    if ((row[x] >> 54) < kOne) {
                        return false;
                    }
                }
            }
            return true;
        }
        case kRGBA_10x6_SkColorType: {
            uint16_t acc = 0xFFC0;  // Ignore bottom six bits
            for (int y = 0; y < height; ++y) {
                const uint64_t* row = addr64(0, y);
                for (int x = 0; x < width; ++x) {
                    acc &= (row[x] >> 48);
                }
                if (0xFFC0 != acc) {
                    return false;
                }
            }
            return true;
        }
        case kR16G16B16A16_unorm_SkColorType: {
            uint16_t acc = 0xFFFF;
            for (int y = 0; y < height; ++y) {
                const uint64_t* row = addr64(0, y);
                for (int x = 0; x < width; ++x) {
                    acc &= (row[x] >> 48);
                }
                if (0xFFFF != acc) {
                    return false;
                }
            }
            return true;
        }
        case kUnknown_SkColorType:
            SkDEBUGFAIL("");
            break;
    }
    return false;
}

bool SkPixmap::erase(SkColor color, const SkIRect& subset) const {
    const SkColor4f rgba = SkColor4f::FromColor(color);
    const bool erased = erase(rgba, &subset);
    return erased;
}

bool SkPixmap::erase(const SkColor4f& color, const SkIRect* subset) const {
    if (!SkColorTypeIsValid(static_cast<unsigned int>(colorType())) ||
        !SkAlphaTypeIsValid(static_cast<unsigned int>(alphaType()))) return false;
    if (colorType() == kUnknown_SkColorType || fPixels == nullptr || isEmpty()) return false;
    if (!fInfo.validRowBytes(fRowBytes)) return false;
    if (!checkedByteSize()) return false;

    SkIRect clip = bounds();
    if (subset && !clip.intersect(*subset)) return false;

    // SRC drawing converts the unpremultiplied input once, before filling rows.
    const SkPMColor4f premul = color.premul();
    sk_sp<SkColorSpace> space = refColorSpace();
    const SkImageInfo dst = SkImageInfo::Make(1, 1, colorType(), alphaType(), std::move(space));
    const SkImageInfo src = SkImageInfo::Make(1, 1, kRGBA_F32_SkColorType,
                                             kPremul_SkAlphaType, nullptr);
    uint64_t dstPixel[2] = {}; // Largest pixel is four binary32 components.
    SkASSERT(static_cast<size_t>(dst.bytesPerPixel()) <= sizeof(dstPixel));
    if (!SkConvertPixels(dst, dstPixel, sizeof(dstPixel), src, &premul, sizeof(premul))) return false;

    const int count = clip.width();
    const size_t stride = fRowBytes;
    const size_t pixel_bytes = static_cast<size_t>(fInfo.bytesPerPixel());
    const size_t left_offset = static_cast<size_t>(clip.fLeft) * pixel_bytes;
    uint8_t* destination = static_cast<uint8_t*>(writable_addr());
    if (colorType() == kRGBA_F32_SkColorType) {
        SkColor4f dstColor = {};
        std::memcpy(&dstColor, dstPixel, sizeof(dstColor));
        for (int y = clip.fTop; y < clip.fBottom; ++y) {
            const size_t row_offset = static_cast<size_t>(y) * stride + left_offset;
            SkColor4f* pixels = reinterpret_cast<SkColor4f*>(destination + row_offset);
            SK_OPTS_NS::memsetT(pixels, dstColor, count);
        }
    } else {
        using FillRow = void (*)(void*, uint64_t, int);
        const FillRow fills[] = {
            fill_pixel_row<uint8_t>, fill_pixel_row<uint16_t>,
            fill_pixel_row<uint32_t>, fill_pixel_row<uint64_t>
        };
        const unsigned int shift = static_cast<unsigned int>(shiftPerPixel());
        // Only RGBA_F32 has 16-byte pixels, and it is handled above.
        if (shift >= std::size(fills)) {
            return false;
        }
        const FillRow fill = fills[shift];
        const uint64_t pixel = dstPixel[0];
        for (int y = clip.fTop; y < clip.fBottom; ++y) {
            const size_t row_offset = static_cast<size_t>(y) * stride + left_offset;
            void* pixels = destination + row_offset;
            fill(pixels, pixel, count);
        }
    }
    return true;
}
