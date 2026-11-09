/*
 * Copyright 2019 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkPathTypes_DEFINED
#define SkPathTypes_DEFINED

#include "include/core/SkTypes.h"

#include <cstdint>

enum class SkPathFillType : uint8_t {
    /** Specifies that "inside" is computed by a non-zero sum of signed edge crossings */
    kWinding,
    /** Specifies that "inside" is computed by an odd number of edge crossings */
    kEvenOdd,
    /** Same as Winding, but draws outside of the path, rather than inside */
    kInverseWinding,
    /** Same as EvenOdd, but draws outside of the path, rather than inside */
    kInverseEvenOdd,

    kDefault = kWinding,
};

static inline bool SkPathFillType_IsEvenOdd(SkPathFillType ft) {
    const bool evenOdd = (static_cast<int>(ft) & 1) != 0;
    return evenOdd;
}

static inline bool SkPathFillType_IsInverse(SkPathFillType ft) {
    const bool inverse = (static_cast<int>(ft) & 2) != 0;
    return inverse;
}

static inline SkPathFillType SkPathFillType_ToggleInverse(SkPathFillType ft) {
    const SkPathFillType toggled = static_cast<SkPathFillType>(static_cast<int>(ft) ^ 2);
    return toggled;
}

static inline SkPathFillType SkPathFillType_ConvertToNonInverse(SkPathFillType ft) {
    const SkPathFillType nonInverse = static_cast<SkPathFillType>(static_cast<int>(ft) & 1);
    return nonInverse;
}

enum class SkPathDirection : uint8_t {
    /** clockwise direction for adding closed contours */
    kCW,
    /** counter-clockwise direction for adding closed contours */
    kCCW,

    kDefault = kCW,
};

enum SkPathSegmentMask {
    kLine_SkPathSegmentMask   = 1 << 0,
    kQuad_SkPathSegmentMask   = 1 << 1,
    kConic_SkPathSegmentMask  = 1 << 2,
    kCubic_SkPathSegmentMask  = 1 << 3,
};

enum class SkPathVerb : uint8_t {
    kMove,   //!< SkPath::RawIter returns 1 point
    kLine,   //!< SkPath::RawIter returns 2 points
    kQuad,   //!< SkPath::RawIter returns 3 points
    kConic,  //!< SkPath::RawIter returns 3 points + 1 weight
    kCubic,  //!< SkPath::RawIter returns 4 points
    kClose,  //!< SkPath::RawIter returns 0 points

    kLast_Verb = kClose,
};

#endif
