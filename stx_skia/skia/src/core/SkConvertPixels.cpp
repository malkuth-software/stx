/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/core/SkConvertPixels.h"

#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSize.h"
#include "include/private/SkAssert.h"
#include "include/private/SkTPin.h"
#include "src/core/SkColorData.h"
#include "src/core/SkColorSpaceXformSteps.h"
#include "src/core/SkHalf.h"
#include "src/core/SkImageInfoPriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpContexts.h"
#include "src/core/SkRectMemcpy.h"
#include "src/core/SkSwizzlePriv.h"
#include "src/core/SkSafeMath.h"

#include <climits>
#include <cstdint>
#include <cstring>

static bool rect_memcpy(const SkImageInfo& dstInfo,       void* dstPixels, size_t dstRB,
                        const SkImageInfo& srcInfo, const void* srcPixels, size_t srcRB,
                        const SkColorSpaceXformSteps& steps) {
    // We can copy the pixels when no color type, alpha type, or color space changes.
    if (dstInfo.colorType() != srcInfo.colorType()) {
        return false;
    }
    if (dstInfo.colorType() != kAlpha_8_SkColorType
            && steps.fFlags.mask() != 0b00000) {
        return false;
    }

    SkRectMemcpy(dstPixels, dstRB,
                 srcPixels, srcRB, dstInfo.minRowBytes(), dstInfo.height());
    return true;
}

static bool is_8888(SkColorType color_type) {
    const bool is_rgba = color_type == kRGBA_8888_SkColorType;
    const bool is_bgra = color_type == kBGRA_8888_SkColorType;
    const bool supported = is_rgba || is_bgra;
    return supported;
}

static bool swizzle_or_premul(const SkImageInfo& dstInfo,       void* dstPixels, size_t dstRB,
                              const SkImageInfo& srcInfo, const void* srcPixels, size_t srcRB,
                              const SkColorSpaceXformSteps& steps) {
    if (!is_8888(dstInfo.colorType()) ||
        !is_8888(srcInfo.colorType()) ||
        steps.fFlags.linearize         ||
        steps.fFlags.gamut_transform   ||
#if !defined(SK_ARM_HAS_NEON)
        steps.fFlags.unpremul          ||
#endif
        steps.fFlags.encode) {
        return false;
    }

    const bool swapRB = dstInfo.colorType() != srcInfo.colorType();

    SkOpts::Swizzle_8888_u32 convert_row = nullptr;

    if (steps.fFlags.premul) {
        convert_row = swapRB ? SkOpts::RGBA_to_bgrA
                    : SkOpts::RGBA_to_rgbA;
    } else if (steps.fFlags.unpremul) {
        convert_row = swapRB ? SkOpts::rgbA_to_BGRA
                    : SkOpts::rgbA_to_RGBA;
    } else {
        // If we're not swizzling, we ought to have used rect_memcpy().
        SkASSERT(swapRB);
        convert_row = SkOpts::RGBA_to_BGRA;
    }

    const int width = dstInfo.width();
    const int height = dstInfo.height();
    uint8_t* dst_bytes = static_cast<uint8_t*>(dstPixels);
    const uint8_t* src_bytes = static_cast<const uint8_t*>(srcPixels);
    for (int y = 0; y < height; ++y) {
        const size_t row = static_cast<size_t>(y);
        uint32_t* dst_row = reinterpret_cast<uint32_t*>(dst_bytes + row * dstRB);
        const uint32_t* src_row = reinterpret_cast<const uint32_t*>(src_bytes + row * srcRB);
        convert_row(dst_row, src_row, width);
    }
    return true;
}

static bool convert_to_alpha8(const SkImageInfo& dstInfo,       void* vdst, size_t dstRB,
                              const SkImageInfo& srcInfo, const void*  src, size_t srcRB,
                              const SkColorSpaceXformSteps&) {
    if (dstInfo.colorType() != kAlpha_8_SkColorType) {
        return false;
    }
    uint8_t* dst_bytes = static_cast<uint8_t*>(vdst);
    const uint8_t* src_bytes = static_cast<const uint8_t*>(src);
    const int width = srcInfo.width();
    const int height = srcInfo.height();

    switch (srcInfo.colorType()) {
        case kUnknown_SkColorType:
        case kAlpha_8_SkColorType: {
            // Unknown should never happen.
            // Alpha8 should have been handled by rect_memcpy().
            SkASSERT(false);
            return false;
        }

        case kA16_unorm_SkColorType: {
            for (int y = 0; y < height; ++y) {
                const size_t row = static_cast<size_t>(y);
                uint8_t* dst = dst_bytes + row * dstRB;
                const uint16_t* src16 = reinterpret_cast<const uint16_t*>(src_bytes + row * srcRB);
                for (int x = 0; x < width; ++x) {
                    dst[x] = static_cast<uint8_t>(src16[x] >> 8);
                }
            }
            return true;
        }

        case kGray_8_SkColorType:
        case kRGB_565_SkColorType:
        case kR8G8_unorm_SkColorType:
        case kR16_unorm_SkColorType:
        case kR16_float_SkColorType:
        case kR16G16_unorm_SkColorType:
        case kR16G16_float_SkColorType:
        case kRGB_888x_SkColorType:
        case kRGB_101010x_SkColorType:
        case kBGR_101010x_SkColorType:
        case kBGR_101010x_XR_SkColorType:
        case kRGB_F16F16F16x_SkColorType:
        case kR8_unorm_SkColorType: {
            for (int y = 0; y < height; ++y) {
                const size_t row = static_cast<size_t>(y);
                uint8_t* dst = dst_bytes + row * dstRB;
                std::memset(dst, 0xFF, static_cast<size_t>(width));
            }
            return true;
        }

        case kARGB_4444_SkColorType: {
            for (int y = 0; y < height; ++y) {
                const size_t row = static_cast<size_t>(y);
                uint8_t* dst = dst_bytes + row * dstRB;
                const uint16_t* src16 = reinterpret_cast<const uint16_t*>(src_bytes + row * srcRB);
                for (int x = 0; x < width; ++x) {
                    const unsigned int alpha = SkPacked4444ToA32(src16[x]);
                    dst[x] = static_cast<uint8_t>(alpha);
                }
            }
            return true;
        }

        case kBGRA_8888_SkColorType:
        case kRGBA_8888_SkColorType:
        case kSRGBA_8888_SkColorType: {
            for (int y = 0; y < height; ++y) {
                const size_t row = static_cast<size_t>(y);
                uint8_t* dst = dst_bytes + row * dstRB;
                const uint32_t* src32 = reinterpret_cast<const uint32_t*>(src_bytes + row * srcRB);
                for (int x = 0; x < width; ++x) {
                    dst[x] = static_cast<uint8_t>(src32[x] >> 24);
                }
            }
            return true;
        }

        case kRGBA_1010102_SkColorType:
        case kBGRA_1010102_SkColorType: {
            for (int y = 0; y < height; ++y) {
                const size_t row = static_cast<size_t>(y);
                uint8_t* dst = dst_bytes + row * dstRB;
                const uint32_t* src32 = reinterpret_cast<const uint32_t*>(src_bytes + row * srcRB);
                for (int x = 0; x < width; ++x) {
                    const uint32_t alpha = (src32[x] >> 30) * 0x55;
                    dst[x] = static_cast<uint8_t>(alpha);
                }
            }
            return true;
        }

        case kRGBA_F16Norm_SkColorType:
        case kRGBA_F16_SkColorType: {
            for (int y = 0; y < height; ++y) {
                const size_t row = static_cast<size_t>(y);
                uint8_t* dst = dst_bytes + row * dstRB;
                const uint64_t* src64 = reinterpret_cast<const uint64_t*>(src_bytes + row * srcRB);
                for (int x = 0; x < width; ++x) {
                    const SkHalf alpha_half = static_cast<SkHalf>(src64[x] >> 48);
                    const float alpha = SkHalfToFloat(alpha_half);
                    const float scaled = 255.0f * alpha;
                    dst[x] = static_cast<uint8_t>(scaled);
                }
            }
            return true;
        }

        case kRGBA_F32_SkColorType: {
            for (int y = 0; y < height; ++y) {
                const size_t row = static_cast<size_t>(y);
                uint8_t* dst = dst_bytes + row * dstRB;
                const float* rgba = reinterpret_cast<const float*>(src_bytes + row * srcRB);
                for (int x = 0; x < width; ++x) {
                    const float scaled = 255.0f * rgba[4*x+3];
                    dst[x] = static_cast<uint8_t>(scaled);
                }
            }
            return true;
        }

        case kA16_float_SkColorType: {
            for (int y = 0; y < height; ++y) {
                const size_t row = static_cast<size_t>(y);
                uint8_t* dst = dst_bytes + row * dstRB;
                const uint16_t* srcF16 = reinterpret_cast<const uint16_t*>(src_bytes + row * srcRB);
                for (int x = 0; x < width; ++x) {
                    const float alpha = SkHalfToFloat(srcF16[x]);
                    const float scaled = 255.0f * alpha;
                    dst[x] = static_cast<uint8_t>(scaled);
                }
            }
            return true;
        }

        case kBGRA_10101010_XR_SkColorType: {
            for (int y = 0; y < height; ++y) {
                const size_t row = static_cast<size_t>(y);
                uint8_t* dst = dst_bytes + row * dstRB;
                const uint64_t* src64 = reinterpret_cast<const uint64_t*>(src_bytes + row * srcRB);
                for (int x = 0; x < width; ++x) {
                    static constexpr int64_t kZero = 384;
                    static constexpr int64_t kRange = 510;
                    static constexpr int64_t kMaxU8 = 0xff;
                    static constexpr int64_t kMinU8 = 0x00;
                    static constexpr int64_t kDivisor = kRange / kMaxU8;
                    const int64_t raw_alpha = static_cast<int64_t>(src64[x] >> 54);
                    // f(384) = 0
                    // f(894) = 255
                    const int64_t alpha =
                            SkTPin((raw_alpha - kZero) / kDivisor, kMinU8, kMaxU8);
                    dst[x] = static_cast<uint8_t>(alpha);
                }
            }
            return true;
        }
        case kRGBA_10x6_SkColorType:
        case kR16G16B16A16_unorm_SkColorType: {
            for (int y = 0; y < height; ++y) {
                const size_t row = static_cast<size_t>(y);
                uint8_t* dst = dst_bytes + row * dstRB;
                const uint64_t* src64 = reinterpret_cast<const uint64_t*>(src_bytes + row * srcRB);
                for (int x = 0; x < width; ++x) {
                    dst[x] = static_cast<uint8_t>(src64[x] >> 56);
                }
            }
            return true;
        }
    }
    return false;
}

// Default: Use the pipeline.
static void convert_with_pipeline(const SkImageInfo& dstInfo, void* dstRow, int dstStride,
                                  const SkImageInfo& srcInfo, const void* srcRow, int srcStride,
                                  const SkColorSpaceXformSteps& steps) {
    SkRasterPipelineContexts::MemoryCtx src = {const_cast<void*>(srcRow), srcStride};
    SkRasterPipelineContexts::MemoryCtx dst = {dstRow, dstStride};

    SkRasterPipeline_<256> pipeline;
    pipeline.appendLoad(srcInfo.colorType(), &src);
    steps.apply(&pipeline);
    pipeline.appendStore(dstInfo.colorType(), &dst);
    pipeline.run(0,0, srcInfo.width(), srcInfo.height());
}

bool SkConvertPixels(const SkImageInfo& dstInfo,       void* dstPixels, size_t dstRB,
                     const SkImageInfo& srcInfo, const void* srcPixels, size_t srcRB) {
    // All format and extent decisions precede the pixel kernels. A one-row
    // color array may use stride zero (vertices and patch color conversion).
    if (dstInfo.dimensions() != srcInfo.dimensions()) return false;
    if (!SkColorTypeIsValid(static_cast<unsigned int>(srcInfo.colorType())) ||
        !SkColorTypeIsValid(static_cast<unsigned int>(dstInfo.colorType())) ||
        !SkAlphaTypeIsValid(static_cast<unsigned int>(srcInfo.alphaType())) ||
        !SkAlphaTypeIsValid(static_cast<unsigned int>(dstInfo.alphaType()))) return false;
    if (!SkImageInfoValidConversion(dstInfo, srcInfo)) return false;
    if (srcPixels == nullptr || dstPixels == nullptr) return false;
    const size_t src_pixel_bytes = static_cast<size_t>(srcInfo.bytesPerPixel());
    const size_t dst_pixel_bytes = static_cast<size_t>(dstInfo.bytesPerPixel());
    const size_t src_stride = srcRB / src_pixel_bytes;
    const size_t dst_stride = dstRB / dst_pixel_bytes;
    if (src_stride > INT_MAX || dst_stride > INT_MAX ||
        srcRB % src_pixel_bytes != 0 || dstRB % dst_pixel_bytes != 0) return false;
    const size_t src_row_bytes = srcInfo.minRowBytes();
    const size_t dst_row_bytes = dstInfo.minRowBytes();
    if (srcInfo.height() > 1 && (srcRB < src_row_bytes || dstRB < dst_row_bytes)) return false;
    SkSafeMath extent;
    const size_t preceding_rows = static_cast<size_t>(srcInfo.height() - 1);
    const size_t src_last_row = extent.mul(preceding_rows, srcRB);
    const size_t dst_last_row = extent.mul(preceding_rows, dstRB);
    extent.add(src_last_row, src_row_bytes);
    extent.add(dst_last_row, dst_row_bytes);
    if (!extent.ok()) return false;

    const int srcStride = static_cast<int>(src_stride);
    const int dstStride = static_cast<int>(dst_stride);
    SkColorSpaceXformSteps steps{srcInfo.colorSpace(), srcInfo.alphaType(),
                                dstInfo.colorSpace(), dstInfo.alphaType()};

    if (rect_memcpy(dstInfo, dstPixels, dstRB, srcInfo, srcPixels, srcRB, steps)) return true;
    if (swizzle_or_premul(dstInfo, dstPixels, dstRB, srcInfo, srcPixels, srcRB, steps)) return true;
    if (convert_to_alpha8(dstInfo, dstPixels, dstRB, srcInfo, srcPixels, srcRB, steps)) return true;
    convert_with_pipeline(dstInfo, dstPixels, dstStride, srcInfo, srcPixels, srcStride, steps);
    return true;
}
