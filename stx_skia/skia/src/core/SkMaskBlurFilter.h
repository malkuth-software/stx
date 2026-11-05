/*
 * Copyright 2017 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkMaskBlurFilter_DEFINED
#define SkMaskBlurFilter_DEFINED

#include <algorithm>
#include <memory>
#include <tuple>

#include "include/core/SkTypes.h"
#include "src/core/SkMask.h"

// Implement a single channel Gaussian blur. The specifics for implementation are taken from:
// https://drafts.fxtf.org/filters/#feGaussianBlurElement
class SkMaskBlurFilter {
public:
    // Create an object suitable for filtering an SkMask using a filter with width sigmaW and
    // height sigmaH.
    SkMaskBlurFilter(double sigmaW, double sigmaH);

    // returns true iff the sigmas will result in an identity mask (no blurring)
    bool hasNoBlur() const;

    // Given a valid nonempty src mask, generate dst and return its border.
    // Call only when hasNoBlur() is false. Below sigma 2 both sigmas must match.
    // Admitted inputs are BW, A8, ARGB32 and LCD16 with naturally aligned pixels
    // and valid row strides/extents. src and dst descriptors/storage cannot alias.
    // dst must not own its previous allocation; the caller owns the produced
    // pixels and must adopt them even when a later operation reports failure.
    // Metadata-only input computes bounds without allocating pixel storage.
    SkIPoint blur(const SkMask& src, SkMaskBuilder* dst) const;

private:
    const double fSigmaW;
    const double fSigmaH;
};

#endif  // SkBlurMaskFilter_DEFINED
