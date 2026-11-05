/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkBlurMask.h"

#include "include/core/SkBlurTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/private/SkMath.h"
#include "include/private/SkSafe32.h"
#include "include/private/SkTPin.h"
#include "include/private/SkTemplates.h"
#include "include/private/SkTo.h"
#include "src/core/SkColorPriv.h"
#include "src/core/SkMaskBlurFilter.h"
#include "src/core/SkMathPriv.h"

#include <algorithm>
#include <limits>
#include <cmath>
#include <cstring>
#include <utility>

using namespace skia_private;

// This constant approximates the scaling done in the software path's
// "high quality" mode, in SkBlurMask::Blur() (1 / sqrt(3)).
// IMHO, it actually should be 1:  we blur "less" than we should do
// according to the CSS and canvas specs, simply because Safari does the same.
// Firefox used to do the same too, until 4.0 where they fixed it.  So at some
// point we should probably get rid of these scaling constants and rebaseline
// all the blur tests.
static const SkScalar kBLUR_SIGMA_SCALE = 0.57735f;

SkScalar SkBlurMask::ConvertRadiusToSigma(SkScalar radius) {
    const SkScalar sigma = radius > 0 ? kBLUR_SIGMA_SCALE * radius + 0.5f : 0.0f;
    return sigma;
}

SkScalar SkBlurMask::ConvertSigmaToRadius(SkScalar sigma) {
    const SkScalar radius = sigma > 0.5f ? (sigma - 0.5f) / kBLUR_SIGMA_SCALE : 0.0f;
    return radius;
}


// Caller-established live row extents; dst may equal blur for matching positions.
// Source iterators advance only when another row remains in the input allocation.
template <typename AlphaIter>
static void merge_src_with_blur(uint8_t dst[], int dstRB,
                                AlphaIter src, int srcRB,
                                const uint8_t blur[], int blurRB,
                                int sw, int sh) {
    for (int y = 0; y < sh; ++y) {
        uint8_t* output = dst + static_cast<size_t>(y) * dstRB;
        const uint8_t* blurred = blur + static_cast<size_t>(y) * blurRB;
        AlphaIter rowSrc(src);
        for (int x = 0; x < sw; ++x) {
            output[x] = SkToU8(SkAlphaMul(blurred[x], SkAlpha255To256(*rowSrc)));
            ++rowSrc;
        }
        if (y + 1 < sh) src >>= srcRB;
    }
}

template <typename AlphaIter>
static void clamp_solid_with_orig(uint8_t dst[], int dstRowBytes,
                                  AlphaIter src, int srcRowBytes,
                                  int sw, int sh) {
    for (int y = 0; y < sh; ++y) {
        uint8_t* output = dst + static_cast<size_t>(y) * dstRowBytes;
        AlphaIter rowSrc(src);
        for (int x = 0; x < sw; ++x) {
            const int s = *rowSrc;
            const int d = output[x];
            output[x] = SkToU8(s + d - SkMulDiv255Round(s, d));
            ++rowSrc;
        }
        if (y + 1 < sh) src >>= srcRowBytes;
    }
}

template <typename AlphaIter>
static void clamp_outer_with_orig(uint8_t dst[], int dstRowBytes,
                                  AlphaIter src, int srcRowBytes,
                                  int sw, int sh) {
    for (int y = 0; y < sh; ++y) {
        uint8_t* output = dst + static_cast<size_t>(y) * dstRowBytes;
        AlphaIter rowSrc(src);
        for (int x = 0; x < sw; ++x) {
            const int srcValue = *rowSrc;
            if (srcValue != 0) {
                output[x] = SkToU8(SkAlphaMul(output[x], SkAlpha255To256(255 - srcValue)));
            }
            ++rowSrc;
        }
        if (y + 1 < sh) src >>= srcRowBytes;
    }
}

static bool valid_style(SkBlurStyle style) {
    switch (style) {
        case kNormal_SkBlurStyle:
        case kSolid_SkBlurStyle:
        case kOuter_SkBlurStyle:
        case kInner_SkBlurStyle:
            return true;
        default:
            return false;
    }
}
///////////////////////////////////////////////////////////////////////////////

bool SkBlurMask::BoxBlur(SkMaskBuilder* dst,
                         const SkMask& src,
                         SkScalar sigma,
                         SkBlurStyle style,
                         SkIVector* margin) {
    if (dst == nullptr || !valid_style(style) || !std::isfinite(sigma) || sigma < 0) return false;
    const int64_t width = src.fBounds.width64();
    const int64_t height = src.fBounds.height64();
    if (width <= 0 || height <= 0) return false;
    // A metadata-only query computes just the margin. Inverse fills ask it with saturated
    // infinite bounds, wider than INT32_MAX; only produced pixels need int extents.
    if (src.fImage != nullptr && (width > INT32_MAX || height > INT32_MAX)) return false;
    if (src.fFormat != SkMask::kBW_Format &&
        src.fFormat != SkMask::kA8_Format &&
        src.fFormat != SkMask::kARGB32_Format &&
        src.fFormat != SkMask::kLCD16_Format)
    {
        return false;
    }

    SkMaskBlurFilter blurFilter{sigma, sigma};
    if (blurFilter.hasNoBlur()) {
        // If there is no effective blur most styles will just produce the original mask.
        // However, kOuter_SkBlurStyle will produce an empty mask.
        if (style == kOuter_SkBlurStyle) {
            (*dst).image() = nullptr;
            (*dst).bounds() = SkIRect::MakeEmpty();
            (*dst).rowBytes() = (*dst).fBounds.width();
            (*dst).format() = SkMask::kA8_Format;
            if (margin != nullptr) {
                // This filter will disregard the src.fImage completely.
                // The margin is actually {-(src.fBounds.width() / 2), -(src.fBounds.height() / 2)}
                // but it is not clear if callers will fall over with negative margins.
                *margin = SkIVector{0, 0};
            }
            return true;
        }
        return false;
    }
    const SkIVector border = blurFilter.blur(src, dst);

    if (src.fImage != nullptr && (*dst).fImage == nullptr) {
        // The call to blur() failed to set our destination image up (e.g. an overflow).
        // Note that if src.fImage was null, (*dst).fImage will also be null and that's
        // *not* an error case - the code should continue to calculate the border.
        return false;
    }

    if (margin != nullptr) {
        *margin = border;
    }

    if (src.fImage == nullptr) {
        if (style == kInner_SkBlurStyle) {
            (*dst).bounds() = src.fBounds; // restore trimmed bounds
            (*dst).rowBytes() = (*dst).fBounds.width();
        }
        return true;
    }

    switch (style) {
        case kNormal_SkBlurStyle:
            break;
        case kSolid_SkBlurStyle: {
            const size_t offset = static_cast<size_t>(border.y()) * (*dst).fRowBytes + border.x();
            uint8_t* dstStart = (*dst).image() + offset;
            switch (src.fFormat) {
                case SkMask::kBW_Format:
                    clamp_solid_with_orig(
                            dstStart, (*dst).fRowBytes,
                            SkMask::AlphaIter<SkMask::kBW_Format>(src.fImage, 0), src.fRowBytes,
                            src.fBounds.width(), src.fBounds.height());
                    break;
                case SkMask::kA8_Format:
                    clamp_solid_with_orig(
                            dstStart, (*dst).fRowBytes,
                            SkMask::AlphaIter<SkMask::kA8_Format>(src.fImage), src.fRowBytes,
                            src.fBounds.width(), src.fBounds.height());
                    break;
                case SkMask::kARGB32_Format: {
                    const uint32_t* srcARGB = reinterpret_cast<const uint32_t*>(src.fImage);
                    clamp_solid_with_orig(
                            dstStart, (*dst).fRowBytes,
                            SkMask::AlphaIter<SkMask::kARGB32_Format>(srcARGB), src.fRowBytes,
                            src.fBounds.width(), src.fBounds.height());
                } break;
                case SkMask::kLCD16_Format: {
                    const uint16_t* srcLCD = reinterpret_cast<const uint16_t*>(src.fImage);
                    clamp_solid_with_orig(
                            dstStart, (*dst).fRowBytes,
                            SkMask::AlphaIter<SkMask::kLCD16_Format>(srcLCD), src.fRowBytes,
                            src.fBounds.width(), src.fBounds.height());
                } break;
                default:
                    SK_ABORT("Unhandled format.");
            }
        } break;
        case kOuter_SkBlurStyle: {
            const size_t offset = static_cast<size_t>(border.y()) * (*dst).fRowBytes + border.x();
            uint8_t* dstStart = (*dst).image() + offset;
            switch (src.fFormat) {
                case SkMask::kBW_Format:
                    clamp_outer_with_orig(
                            dstStart, (*dst).fRowBytes,
                            SkMask::AlphaIter<SkMask::kBW_Format>(src.fImage, 0), src.fRowBytes,
                            src.fBounds.width(), src.fBounds.height());
                    break;
                case SkMask::kA8_Format:
                    clamp_outer_with_orig(
                            dstStart, (*dst).fRowBytes,
                            SkMask::AlphaIter<SkMask::kA8_Format>(src.fImage), src.fRowBytes,
                            src.fBounds.width(), src.fBounds.height());
                    break;
                case SkMask::kARGB32_Format: {
                    const uint32_t* srcARGB = reinterpret_cast<const uint32_t*>(src.fImage);
                    clamp_outer_with_orig(
                            dstStart, (*dst).fRowBytes,
                            SkMask::AlphaIter<SkMask::kARGB32_Format>(srcARGB), src.fRowBytes,
                            src.fBounds.width(), src.fBounds.height());
                } break;
                case SkMask::kLCD16_Format: {
                    const uint16_t* srcLCD = reinterpret_cast<const uint16_t*>(src.fImage);
                    clamp_outer_with_orig(
                            dstStart, (*dst).fRowBytes,
                            SkMask::AlphaIter<SkMask::kLCD16_Format>(srcLCD), src.fRowBytes,
                            src.fBounds.width(), src.fBounds.height());
                } break;
                default:
                    SK_ABORT("Unhandled format.");
            }
        } break;
        case kInner_SkBlurStyle: {
            // now we allocate the "real" dst, mirror the size of src
            SkAutoMaskFreeImage blurOwner((*dst).image());
            SkMaskBuilder blur = std::move(*dst);

            *dst = SkMaskBuilder(nullptr, src.fBounds, src.fBounds.width(), blur.format());
            size_t dstSize = (*dst).computeImageSize();
            if (0 == dstSize) {
                return false;   // too big to allocate, abort
            }
            (*dst).image() = SkMaskBuilder::AllocImage(dstSize);

            const size_t offset = static_cast<size_t>(border.y()) * blur.fRowBytes + border.x();
            const uint8_t* blurStart = blur.image() + offset;
            switch (src.fFormat) {
                case SkMask::kBW_Format:
                    merge_src_with_blur(
                            (*dst).image(), (*dst).fRowBytes,
                            SkMask::AlphaIter<SkMask::kBW_Format>(src.fImage, 0), src.fRowBytes,
                            blurStart, blur.fRowBytes,
                            src.fBounds.width(), src.fBounds.height());
                    break;
                case SkMask::kA8_Format:
                    merge_src_with_blur(
                            (*dst).image(), (*dst).fRowBytes,
                            SkMask::AlphaIter<SkMask::kA8_Format>(src.fImage), src.fRowBytes,
                            blurStart, blur.fRowBytes,
                            src.fBounds.width(), src.fBounds.height());
                    break;
                case SkMask::kARGB32_Format: {
                    const uint32_t* srcARGB = reinterpret_cast<const uint32_t*>(src.fImage);
                    merge_src_with_blur(
                            (*dst).image(), (*dst).fRowBytes,
                            SkMask::AlphaIter<SkMask::kARGB32_Format>(srcARGB), src.fRowBytes,
                            blurStart, blur.fRowBytes,
                            src.fBounds.width(), src.fBounds.height());
                } break;
                case SkMask::kLCD16_Format: {
                    const uint16_t* srcLCD = reinterpret_cast<const uint16_t*>(src.fImage);
                    merge_src_with_blur(
                            (*dst).image(), (*dst).fRowBytes,
                            SkMask::AlphaIter<SkMask::kLCD16_Format>(srcLCD), src.fRowBytes,
                            blurStart, blur.fRowBytes,
                            src.fBounds.width(), src.fBounds.height());
                } break;
                default:
                    SK_ABORT("Unhandled format.");
            }
        } break;
        default:
            SK_ABORT("Invalid validated blur style.");
    }

    return true;
}

/* Convolving a box with itself three times results in a piecewise
   quadratic function:

   0                              x <= -1.5
   9/8 + 3/2 x + 1/2 x^2   -1.5 < x <= -.5
   3/4 - x^2                -.5 < x <= .5
   9/8 - 3/2 x + 1/2 x^2    0.5 < x <= 1.5
   0                        1.5 < x

   Mathematica:

   g[x_] := Piecewise [ {
     {9/8 + 3/2 x + 1/2 x^2 ,  -1.5 < x <= -.5},
     {3/4 - x^2             ,   -.5 < x <= .5},
     {9/8 - 3/2 x + 1/2 x^2 ,   0.5 < x <= 1.5}
   }, 0]

   To get the profile curve of the blurred step function at the rectangle
   edge, we evaluate the indefinite integral, which is piecewise cubic:

   0                                        x <= -1.5
   9/16 + 9/8 x + 3/4 x^2 + 1/6 x^3   -1.5 < x <= -0.5
   1/2 + 3/4 x - 1/3 x^3              -.5 < x <= .5
   7/16 + 9/8 x - 3/4 x^2 + 1/6 x^3     .5 < x <= 1.5
   1                                  1.5 < x

   in Mathematica code:

   gi[x_] := Piecewise[ {
     { 0 , x <= -1.5 },
     { 9/16 + 9/8 x + 3/4 x^2 + 1/6 x^3, -1.5 < x <= -0.5 },
     { 1/2 + 3/4 x - 1/3 x^3          ,  -.5 < x <= .5},
     { 7/16 + 9/8 x - 3/4 x^2 + 1/6 x^3,   .5 < x <= 1.5}
   },1]
*/

static float gaussianIntegral(float x) {
    if (x > 1.5f) {
        return 0.0f;
    }
    if (x < -1.5f) {
        return 1.0f;
    }

    float x2 = x*x;
    float x3 = x2*x;

    if ( x > 0.5f ) {
        const float integral = 0.5625f - (x3 / 6.0f - 3.0f * x2 * 0.25f + 1.125f * x);
        return integral;
    }
    if ( x > -0.5f ) {
        const float integral = 0.5f - (0.75f * x - x3 / 3.0f);
        return integral;
    }
    const float integral = 0.4375f + (-x3 / 6.0f - 3.0f * x2 * 0.25f - 1.125f * x);
    return integral;
}

/*  ComputeBlurProfile fills in an array of floating
    point values between 0 and 255 for the profile signature of
    a blurred half-plane with the given blur radius.  Since we're
    going to be doing screened multiplications (i.e., 1 - (1-x)(1-y))
    all the time, we actually fill in the profile pre-inverted
    (already done 255-x).
*/

void SkBlurMask::ComputeBlurProfile(uint8_t* profile, int size, SkScalar sigma) {
    SkASSERT(SkScalarCeilToInt(6*sigma) == size);

    int center = size >> 1;

    float invr = 1.f/(2*sigma);

    profile[0] = 255;
    for (int x = 1 ; x < size ; ++x) {
        float scaled_x = (center - x - .5f) * invr;
        float gi = gaussianIntegral(scaled_x);
        profile[x] = static_cast<uint8_t>(255 - static_cast<uint8_t>(255.f * gi));
    }
}

// TODO MAYBE: Maintain a profile cache to avoid recomputing this for
// commonly used radii.  Consider baking some of the most common blur radii
// directly in as static data?

// Implementation adapted from Michael Herf's approach:
// http://stereopsis.com/shadowrect/

uint8_t SkBlurMask::ProfileLookup(const uint8_t *profile, int loc,
                                  int blurredWidth, int sharpWidth) {
    // For 0 <= loc < blurredWidth <= INT32_MAX, center before doubling.
    // This keeps the signed distance within [-INT32_MAX, INT32_MAX] without
    // widening every lane of the scanline kernel. sharpWidth is in [0,width].
    const int center = blurredWidth / 2;
    const int delta = loc - center;
    const int distance = 2 * delta + ((blurredWidth & 1) == 0 ? 1 : 0);
    const int absolute = distance < 0 ? -distance : distance;
    const int dx = absolute - sharpWidth;
    const int offset = dx > 0 ? dx / 2 : 0;
    return profile[offset];
}

void SkBlurMask::ComputeBlurredScanline(uint8_t* pixels, const uint8_t* profile,
                                       unsigned int width, SkScalar sigma) {
    const unsigned int profile_size = static_cast<unsigned int>(SkScalarCeilToInt(6 * sigma));
    SkASSERT(profile_size > 0 && width > 0 && width <= INT32_MAX);
    if (width < profile_size) {
        // The original wrapped width - profile_size as unsigned, which selected the profile
        // lookup with a negative sharp extent. Keep that result in signed 64-bit terms:
        // |2x + 1 - width| <= width - 1 and center <= profile_size - 1, so every offset is
        // below profile_size.
        const int64_t narrow_center = static_cast<int64_t>(profile_size & ~1U) - 1;
        const int64_t narrow_extent = static_cast<int64_t>(width) - profile_size - narrow_center;
        for (unsigned int x = 0; x < width; ++x) {
            const int64_t distance = 2 * static_cast<int64_t>(x) + 1 - static_cast<int64_t>(width);
            const int64_t absolute = distance < 0 ? -distance : distance;
            const int64_t offset = (absolute - narrow_extent) / 2;
            SkASSERT_RELEASE(offset >= 0 && offset < static_cast<int64_t>(profile_size));
            pixels[x] = profile[offset];
        }
        return;
    }
    const unsigned int sharp_width = width - profile_size;
    // The nearest odd value below profile_size is the doubled profile center.
    const int center = static_cast<int>(profile_size & ~1U) - 1;
    const int sharp_extent = static_cast<int>(sharp_width) - center;
    if (profile_size <= sharp_width) {
        for (unsigned int x = 0; x < width; ++x) {
            pixels[x] = ProfileLookup(profile, static_cast<int>(x), static_cast<int>(width), sharp_extent);
        }
        return;
    }
    const float span = static_cast<float>(sharp_width) / (2 * sigma);
    for (unsigned int x = 0; x < width; ++x) {
        const float giX = 1.5f - (static_cast<float>(x) + .5f) / (2 * sigma);
        const float coverage = 255 * (gaussianIntegral(giX) - gaussianIntegral(giX + span));
        pixels[x] = static_cast<uint8_t>(coverage);
    }
}

bool SkBlurMask::BlurRect(SkScalar sigma, SkMaskBuilder* dst, const SkRect& src,
                          SkBlurStyle style, SkIVector* margin,
                          SkMaskBuilder::CreateMode createMode) {
    if (dst == nullptr || !valid_style(style) || !std::isfinite(sigma) || sigma <= 0 ||
        !src.isFinite() || src.isEmpty()) return false;
    switch (createMode) {
        case SkMaskBuilder::kJustComputeBounds_CreateMode:
        case SkMaskBuilder::kJustRenderImage_CreateMode:
        case SkMaskBuilder::kComputeBoundsAndRenderImage_CreateMode:
            break;
        default:
            return false;
    }
    const int profileSize = SkScalarCeilToInt(6 * sigma);
    if (profileSize <= 0) return false;
    const int pad = profileSize / 2;
    // Check the unrounded coordinate range before the float-to-int helpers clamp.
    const double left = static_cast<double>(src.fLeft) - pad;
    const double top = static_cast<double>(src.fTop) - pad;
    const double right = static_cast<double>(src.fRight) + pad;
    const double bottom = static_cast<double>(src.fBottom) + pad;
    if (left < INT32_MIN || top < INT32_MIN || right > INT32_MAX || bottom > INT32_MAX) return false;
    const SkIRect bounds = SkIRect::MakeLTRB(SkScalarRoundToInt(src.fLeft - pad),
                                            SkScalarRoundToInt(src.fTop - pad),
                                            SkScalarRoundToInt(src.fRight + pad),
                                            SkScalarRoundToInt(src.fBottom + pad));
    const int64_t width = bounds.width64();
    const int64_t height = bounds.height64();
    if (width <= 0 || height <= 0 || width > INT32_MAX || height > INT32_MAX) return false;
    const SkIRect innerBounds = src.round();
    const int sw = SkScalarFloorToInt(src.width());
    const int sh = SkScalarFloorToInt(src.height());
    if (sw < 0 || sh < 0 || static_cast<int64_t>(sw) + 2 * pad > width ||
        static_cast<int64_t>(sh) + 2 * pad > height) return false;
    // The analytic inner shortcut historically uses floor-sized scanlines.
    // Refuse inconsistent rounded geometry instead of publishing a partial mask.
    if (style == kInner_SkBlurStyle &&
        (sw <= 0 || sh <= 0 || innerBounds.width64() != sw || innerBounds.height64() != sh)) return false;
    SkMaskBuilder result(nullptr, bounds, static_cast<uint32_t>(width), SkMask::kA8_Format);
    const size_t dstSize = result.computeImageSize();
    if (dstSize == 0) return false;
    if (margin != nullptr) (*margin).set(pad, pad);
    if (createMode == SkMaskBuilder::kJustComputeBounds_CreateMode) {
        if (style == kInner_SkBlurStyle) {
            result.bounds() = innerBounds;
            result.rowBytes() = static_cast<uint32_t>(sw);
        }
        *dst = std::move(result);
        return true;
    }

    AutoTMalloc<uint8_t> profile(profileSize);
    ComputeBlurProfile(profile.get(), profileSize, sigma);
    const int dstWidth = static_cast<int>(width);
    const int dstHeight = static_cast<int>(height);
    AutoTMalloc<uint8_t> horizontalScanline(dstWidth);
    AutoTMalloc<uint8_t> verticalScanline(dstHeight);
    ComputeBlurredScanline(horizontalScanline.get(), profile.get(), dstWidth, sigma);
    ComputeBlurredScanline(verticalScanline.get(), profile.get(), dstHeight, sigma);
    SkAutoMaskFreeImage pixels(SkMaskBuilder::AllocImage(dstSize));
    for (int y = 0; y < dstHeight; ++y) {
        uint8_t* row = pixels.get() + static_cast<size_t>(y) * dstWidth;
        const uint8_t vertical = verticalScanline[y];
        for (int x = 0; x < dstWidth; ++x) {
            // Byte coverage bounds the product by 65025. Adding 128 and then
            // its high byte reaches at most 65407: every stage fits uint16_t.
            // This is exactly SkMulDiv255Round, with its lane bounds explicit.
            const uint16_t product = static_cast<uint16_t>(horizontalScanline[x] * vertical);
            const uint16_t rounded = static_cast<uint16_t>(product + 128);
            const uint16_t combined = static_cast<uint16_t>(rounded + (rounded >> 8));
            row[x] = static_cast<uint8_t>(combined >> 8);
        }
    }
    if (style == kInner_SkBlurStyle) {
        const size_t innerSize = static_cast<size_t>(sw) * sh;
        SkAutoMaskFreeImage inner(SkMaskBuilder::AllocImage(innerSize));
        for (int y = 0; y < sh; ++y) {
            const size_t sourceOffset = static_cast<size_t>(y + pad) * dstWidth + pad;
            uint8_t* row = inner.get() + static_cast<size_t>(y) * sw;
            std::memcpy(row, pixels.get() + sourceOffset, static_cast<size_t>(sw));
        }
        pixels = std::move(inner);
        result.bounds() = innerBounds;
        result.rowBytes() = static_cast<uint32_t>(sw);
    } else if (style == kOuter_SkBlurStyle || style == kSolid_SkBlurStyle) {
        const int fill = style == kSolid_SkBlurStyle ? 255 : 0;
        for (int y = pad; y < dstHeight - pad; ++y) {
            uint8_t* row = pixels.get() + static_cast<size_t>(y) * dstWidth + pad;
            std::memset(row, fill, static_cast<size_t>(sw));
        }
    }
    result.image() = pixels.release();
    *dst = std::move(result);
    return true;
}

// The "simple" blur is a direct implementation of separable convolution with a discrete
// gaussian kernel.  It's "ground truth" in a sense; too slow to be used, but very
// useful for correctness comparisons.

bool SkBlurMask::BlurGroundTruth(SkScalar sigma, SkMaskBuilder* dst,
                                 const SkMask& src, SkBlurStyle style, SkIVector* margin) {
    if (dst == nullptr || src.fFormat != SkMask::kA8_Format || !valid_style(style) ||
        !std::isfinite(sigma) || sigma <= 0) return false;
    const int64_t sourceWidth = src.fBounds.width64();
    const int64_t sourceHeight = src.fBounds.height64();
    if (sourceWidth <= 0 || sourceHeight <= 0 || sourceWidth > INT32_MAX ||
        sourceHeight > INT32_MAX || src.fRowBytes > INT32_MAX) return false;
    if (src.fImage != nullptr && src.fRowBytes < sourceWidth) return false;
    const float variance = sigma * sigma;
    const int windowSize = SkScalarCeilToInt(sigma * 6) | 1;
    if (windowSize <= 0) return false;
    const int pad = windowSize >> 1;
    const SkMask metadata(nullptr, src.fBounds, src.fRowBytes, SkMask::kA8_Format);
    SkMaskBuilder result = SkMaskBuilder::PrepareDestination(pad, pad, metadata);
    const size_t dstSize = result.computeImageSize();
    if (dstSize == 0) return false;
    if (src.fImage == nullptr) {
        if (style == kInner_SkBlurStyle) {
            result.bounds() = src.fBounds;
            result.rowBytes() = src.fRowBytes;
        }
        *dst = std::move(result);
        if (margin != nullptr) (*margin).set(pad, pad);
        return true;
    }

    const int64_t paddedWidth = sourceWidth + 4 * static_cast<int64_t>(pad);
    const int64_t temporaryWidth = sourceHeight + 4 * static_cast<int64_t>(pad);
    const int64_t temporaryHeight = paddedWidth - 2 * static_cast<int64_t>(pad);
    if (paddedWidth > INT32_MAX || temporaryWidth > INT32_MAX || temporaryHeight > INT32_MAX) return false;
    const int64_t padCount = paddedWidth * sourceHeight;
    const int64_t tmpCount = temporaryWidth * temporaryHeight;
    if (padCount > INT32_MAX || tmpCount > INT32_MAX / static_cast<int64_t>(sizeof(float))) return false;
    const int srcWidth = static_cast<int>(sourceWidth);
    const int srcHeight = static_cast<int>(sourceHeight);
    const int dstWidth = result.fBounds.width();
    const int padWidth = static_cast<int>(paddedWidth);
    const int tmpWidth = static_cast<int>(temporaryWidth);
    const int tmpHeight = static_cast<int>(temporaryHeight);
    AutoTMalloc<float> gaussWindow(windowSize);
    gaussWindow[pad] = 1;
    float windowSum = 1;
    for (int x = 1; x <= pad; ++x) {
        const float position = static_cast<float>(x);
        const float gaussian = expf(-position * position / (2 * variance));
        gaussWindow[pad + x] = gaussian;
        gaussWindow[pad - x] = gaussian;
        windowSum += 2 * gaussian;
    }
    AutoTMalloc<uint8_t> padPixels(static_cast<size_t>(padCount));
    std::memset(padPixels.get(), 0, static_cast<size_t>(padCount));
    for (int y = 0; y < srcHeight; ++y) {
        uint8_t* padded = padPixels.get() + static_cast<size_t>(y) * padWidth + 2 * pad;
        const uint8_t* source = src.fImage + static_cast<size_t>(y) * src.fRowBytes;
        std::memcpy(padded, source, static_cast<size_t>(srcWidth));
    }
    AutoTMalloc<float> tmpImage(static_cast<size_t>(tmpCount));
    std::fill_n(tmpImage.get(), static_cast<size_t>(tmpCount), 0.0f);
    // Double padding keeps both convolution interiors free of boundary branches.
    // The first pass transposes; the second restores the original orientation.
    for (int y = 0; y < srcHeight; ++y) {
        const uint8_t* source = padPixels.get() + static_cast<size_t>(y) * padWidth;
        for (int x = pad; x < padWidth - pad; ++x) {
            const uint8_t* center = source + x;
            float sum = 0;
            for (int i = -pad; i <= pad; ++i) {
                sum += gaussWindow[pad + i] * center[i];
            }
            const size_t offset = static_cast<size_t>(x - pad) * tmpWidth + y + 2 * pad;
            tmpImage[offset] = sum / windowSum;
        }
    }
    SkAutoMaskFreeImage pixels(SkMaskBuilder::AllocImage(dstSize));
    for (int y = 0; y < tmpHeight; ++y) {
        const float* source = tmpImage.get() + static_cast<size_t>(y) * tmpWidth;
        for (int x = pad; x < tmpWidth - pad; ++x) {
            const float* center = source + x;
            float finalValue = 0;
            for (int i = -pad; i <= pad; ++i) {
                finalValue += gaussWindow[pad + i] * center[i];
            }
            finalValue /= windowSum;
            const int integerPixel = static_cast<int>(finalValue + 0.5f);
            const size_t offset = static_cast<size_t>(x - pad) * dstWidth + y;
            pixels.get()[offset] = static_cast<uint8_t>(SkTPin(SkClampPos(integerPixel), 0, 255));
        }
    }
    const size_t inset = static_cast<size_t>(pad) * result.fRowBytes + pad;
    switch (style) {
        case kNormal_SkBlurStyle:
            break;
        case kSolid_SkBlurStyle:
            clamp_solid_with_orig(pixels.get() + inset, result.fRowBytes,
                SkMask::AlphaIter<SkMask::kA8_Format>(src.fImage), src.fRowBytes, srcWidth, srcHeight);
            break;
        case kOuter_SkBlurStyle:
            clamp_outer_with_orig(pixels.get() + inset, result.fRowBytes,
                SkMask::AlphaIter<SkMask::kA8_Format>(src.fImage), src.fRowBytes, srcWidth, srcHeight);
            break;
        case kInner_SkBlurStyle: {
            const size_t innerSize = src.computeImageSize();
            if (innerSize == 0) return false;
            SkAutoMaskFreeImage inner(SkMaskBuilder::AllocImage(innerSize, SkMaskBuilder::kZeroInit_Alloc));
            merge_src_with_blur(inner.get(), src.fRowBytes,
                SkMask::AlphaIter<SkMask::kA8_Format>(src.fImage), src.fRowBytes,
                pixels.get() + inset, result.fRowBytes, srcWidth, srcHeight);
            pixels = std::move(inner);
            result.bounds() = src.fBounds;
            result.rowBytes() = src.fRowBytes;
        } break;
        default:
            SK_ABORT("Invalid validated blur style.");
    }
    result.image() = pixels.release();
    *dst = std::move(result);
    if (margin != nullptr) (*margin).set(pad, pad);
    return true;
}
