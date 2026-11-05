/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkConvertPixels_DEFINED
#define SkConvertPixels_DEFINED

#include <cstddef>

struct SkImageInfo;

// Borrows caller-owned pixel arrays for this call. Shapes must match and be
// positive; both arrays must cover (height - 1) * stride + minRowBytes(). Strides
// are pixel-size multiples and cover a row, except one-row arrays may use zero.
// Typed pixels require their natural alignment. Floating alpha is finite in [0,1].
// Buffers are disjoint, or exactly in-place with identical pixel size and stride;
// partially overlapping ranges are not supported. No allocation is retained.
// Invalid metadata/stride returns false before writing; successful conversion
// writes active pixels only, leaving destination row padding untouched.
[[nodiscard]] bool SkConvertPixels(
        const SkImageInfo& dstInfo,       void* dstPixels, size_t dstRowBytes,
        const SkImageInfo& srcInfo, const void* srcPixels, size_t srcRowBytes);

#endif
