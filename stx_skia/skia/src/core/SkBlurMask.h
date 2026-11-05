/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkBlurMask_DEFINED
#define SkBlurMask_DEFINED

#include "include/core/SkPoint.h"
#include "include/core/SkScalar.h"
#include "include/core/SkTypes.h"
#include "src/core/SkMask.h"

#include <cstdint>

enum SkBlurStyle : int;
struct SkRect;

class SkBlurMask {
public:
    // Analytic rectangle shortcut; failure leaves dst unchanged. An inner-style
    // rectangle must have matching floor-sized and rounded extents. Both pixel
    // creation modes allocate; no caller-provided output buffer is consumed.
    [[nodiscard]] static bool BlurRect(SkScalar sigma, SkMaskBuilder *dst, const SkRect &src,
                                       SkBlurStyle, SkIVector *margin = nullptr,
                                       SkMaskBuilder::CreateMode createMode =
                                           SkMaskBuilder::kComputeBoundsAndRenderImage_CreateMode);
    // Source and destination descriptors/storage must not alias. Call with an
    // empty destination; retain any prior allocation before resetting its view.
    // A successful pixel-producing call transfers newly allocated storage through
    // dst.image(); adopt it into SkAutoMaskFreeImage. Metadata-only sources do not
    // allocate. Pixels require valid format alignment, live extents and strides.
    // BoxBlur may replace dst on failure; callers must adopt any returned buffer.
    // No effective blur returns false, except outer style succeeds with an empty mask.
    [[nodiscard]] static bool BoxBlur(SkMaskBuilder* dst,
                                      const SkMask& src,
                                      SkScalar sigma,
                                      SkBlurStyle style,
                                      SkIVector* margin = nullptr);

    // the "ground truth" blur does a gaussian convolution; it's slow
    // but useful for comparison purposes. Failure leaves dst unchanged.
    [[nodiscard]] static bool BlurGroundTruth(SkScalar sigma,
                                              SkMaskBuilder* dst,
                                              const SkMask& src,
                                              SkBlurStyle,
                                              SkIVector* margin = nullptr);

    // If radius > 0, return the corresponding sigma, else return 0
    static SkScalar SK_SPI ConvertRadiusToSigma(SkScalar radius);
    // If sigma > 0.5, return the corresponding radius, else return 0
    static SkScalar SK_SPI ConvertSigmaToRadius(SkScalar sigma);

    /* Helper functions for analytic rectangle blurs */

    /** Look up the intensity of the (one dimnensional) blurred half-plane.
        @param profile The precomputed 1D blur profile; initialized by ComputeBlurProfile below.
        @param loc the location to look up; Require 0 <= loc < blurredWidth <= INT32_MAX and
                   0 <= sharpWidth <= blurredWidth; negative derived offsets clamp to zero
        @param blurred_width The width of the final, blurred rectangle
        @param sharp_width The width of the original, unblurred rectangle.
    */
    static uint8_t ProfileLookup(const uint8_t* profile, int loc, int blurredWidth, int sharpWidth);

    /** Populate the profile of a 1D blurred halfplane.
        @param profile The 1D table to fill in
        @param size    Must be ceil(6*sigma) bytes, positive; sigma finite and positive
        @param sigma   The standard deviation of the gaussian blur kernel
    */
    static void ComputeBlurProfile(uint8_t* profile, int size, SkScalar sigma);

    /** Compute an entire scanline of a blurred step function.  This is a 1D helper that
        will produce both the horizontal and vertical profiles of the blurry rectangle.
        @param pixels Location to store the resulting pixel data; allocated and managed by caller
        @param profile Precomputed blur profile computed by ComputeBlurProfile above.
        @param width Size of the pixels array.
        @param sigma Standard deviation of the gaussian blur kernel used to compute the profile;
                     profile has ceil(6*sigma) elements. width must be positive and at
                     most INT32_MAX; a width below the profile size keeps the original
                     narrow-rectangle lookup. Buffers must not overlap.
    */

    static void ComputeBlurredScanline(uint8_t* pixels, const uint8_t* profile,
                                       unsigned int width, SkScalar sigma);
};

#endif
