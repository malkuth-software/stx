/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkMatrix.h"

#include "include/core/SkPathBuilder.h"
#include "include/core/SkPoint3.h"
#include "include/core/SkRSXform.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSize.h"
#include "include/core/SkString.h"
#include "include/private/SkDebug.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkMath.h"
#include "include/private/SkTo.h"
#include "src/core/SkFloatBits.h"
#include "src/core/SkMatrixPriv.h"
#include "src/core/SkMatrixUtils.h"
#include "src/core/SkSamplingPriv.h"
#include "src/core/SkVx.h"

#include <algorithm>
#include <cmath>

// Point mapping processes the shorter of the two spans. Spans are bounded by
// SkPath and vertex limits, so the count fits in int.
template <typename S, typename T> int min_count(SkSpan<S> a, SkSpan<T> b) {
    const int count = SkToInt(std::min(a.size(), b.size()));
    return count;
}

void SkMatrix::doNormalizePerspective() {
    // If the bottom row of the matrix is [0, 0, not_one], we will treat the matrix as if it
    // is in perspective, even though it stills behaves like its affine. If we divide everything
    // by the not_one value, then it will behave the same, but will be treated as affine,
    // and therefore faster (e.g. clients can forward-difference calculations).
    //
    if (0 == fMat[SkMatrix::kMPersp0] && 0 == fMat[SkMatrix::kMPersp1]) {
        const SkScalar p2 = fMat[SkMatrix::kMPersp2];
        if (p2 != 0 && p2 != 1) {
            const double inv = 1.0 / p2;
            for (int i = 0; i < 6; ++i) {
                fMat[i] = SkDoubleToScalar(fMat[i] * inv);
            }
            fMat[SkMatrix::kMPersp2] = 1;
        }
        setTypeMask(kUnknown_Mask);
    }
}

SkMatrix& SkMatrix::reset() {
    *this = SkMatrix();
    return *this;
}

SkMatrix& SkMatrix::set9(const SkScalar buffer[9]) {
    // buffer may be this matrix's own values; copying an element onto itself is harmless.
    for (int i = 0; i < 9; ++i) {
        fMat[i] = buffer[i];
    }
    setTypeMask(kUnknown_Mask);
    return *this;
}

SkMatrix& SkMatrix::setAffine(const SkScalar buffer[6]) {
    fMat[kMScaleX] = buffer[kAScaleX];
    fMat[kMSkewX]  = buffer[kASkewX];
    fMat[kMTransX] = buffer[kATransX];
    fMat[kMSkewY]  = buffer[kASkewY];
    fMat[kMScaleY] = buffer[kAScaleY];
    fMat[kMTransY] = buffer[kATransY];
    fMat[kMPersp0] = 0;
    fMat[kMPersp1] = 0;
    fMat[kMPersp2] = 1;
    setTypeMask(kUnknown_Mask);
    return *this;
}

// this aligns with the masks, so we can compute a mask from a variable 0/1
enum {
    kTranslate_Shift,
    kScale_Shift,
    kAffine_Shift,
    kPerspective_Shift,
    kRectStaysRect_Shift
};

static const int32_t kScalar1Int = 0x3f800000;

uint8_t SkMatrix::computePerspectiveTypeMask() const {
    // Benchmarking suggests that replacing this set of SkScalarAs2sCompliment
    // is a win, but replacing those below is not. We don't yet understand
    // that result.
    if (fMat[kMPersp0] != 0 || fMat[kMPersp1] != 0 || fMat[kMPersp2] != 1) {
        // If this is a perspective transform, we return true for all other
        // transform flags - this does not disable any optimizations, respects
        // the rule that the type mask must be conservative, and speeds up
        // type mask computation.
        return SkToU8(kORableMasks);
    }

    return SkToU8(kOnlyPerspectiveValid_Mask | kUnknown_Mask);
}

uint8_t SkMatrix::computeTypeMask() const {
    unsigned mask = 0;

    if (fMat[kMPersp0] != 0 || fMat[kMPersp1] != 0 || fMat[kMPersp2] != 1) {
        // Once it is determined that that this is a perspective transform,
        // all other flags are moot as far as optimizations are concerned.
        return SkToU8(kORableMasks);
    }

    if (fMat[kMTransX] != 0 || fMat[kMTransY] != 0) {
        mask |= kTranslate_Mask;
    }

    // Two's-complement bit patterns make -0 compare equal to +0, so a signed zero
    // diagonal or skew is treated as zero.
    int m00 = SkScalarAs2sCompliment(fMat[SkMatrix::kMScaleX]);
    int m01 = SkScalarAs2sCompliment(fMat[SkMatrix::kMSkewX]);
    int m10 = SkScalarAs2sCompliment(fMat[SkMatrix::kMSkewY]);
    int m11 = SkScalarAs2sCompliment(fMat[SkMatrix::kMScaleY]);

    if (m01 | m10) {
        // The skew components may be scale-inducing, unless we are dealing
        // with a pure rotation.  Testing for a pure rotation is expensive,
        // so we opt for being conservative by always setting the scale bit.
        // along with affine.
        // By doing this, we are also ensuring that matrices have the same
        // type masks as their inverses.
        mask |= kAffine_Mask | kScale_Mask;

        // For rectStaysRect, in the affine case, we only need check that
        // the primary diagonal is all zeros and that the secondary diagonal
        // is all non-zero.

        // map non-zero to 1
        m01 = m01 != 0;
        m10 = m10 != 0;

        const int dp0 = 0 == (m00 | m11);  // true if both are 0
        const int ds1 = m01 & m10;         // true if both are 1

        mask |= (dp0 & ds1) << kRectStaysRect_Shift;
    } else {
        // Only test for scale explicitly if not affine, since affine sets the
        // scale bit.
        if ((m00 ^ kScalar1Int) | (m11 ^ kScalar1Int)) {
            mask |= kScale_Mask;
        }

        // Not affine, therefore we already know secondary diagonal is
        // all zeros, so we just need to check that primary diagonal is
        // all non-zero.

        // map non-zero to 1
        m00 = m00 != 0;
        m11 = m11 != 0;

        // record if the (p)rimary diagonal is all non-zero
        mask |= (m00 & m11) << kRectStaysRect_Shift;
    }

    return SkToU8(mask);
}

///////////////////////////////////////////////////////////////////////////////

// Exact comparison of the nine values: -0 equals +0 and NaN is never equal.
// a and b may be the same matrix.
bool operator==(const SkMatrix& a, const SkMatrix& b) {
    for (int i = 0; i < 9; ++i) {
        if (!(a.fMat[i] == b.fMat[i])) {
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////

// helper function to determine if upper-left 2x2 of matrix is degenerate
static inline bool is_degenerate_2x2(SkScalar scaleX, SkScalar skewX,
                                     SkScalar skewY,  SkScalar scaleY) {
    const SkScalar perp_dot = scaleX*scaleY - skewX*skewY;
    const bool degenerate = SkScalarNearlyZero(perp_dot, SK_ScalarNearlyZero*SK_ScalarNearlyZero);
    return degenerate;
}

///////////////////////////////////////////////////////////////////////////////

bool SkMatrix::isSimilarity(SkScalar tol) const {
    // if identity or translate matrix
    const TypeMask mask = getType();
    if (mask <= kTranslate_Mask) {
        return true;
    }
    if (mask & kPerspective_Mask) {
        return false;
    }

    const SkScalar mx = fMat[kMScaleX];
    const SkScalar my = fMat[kMScaleY];
    // if no skew, can just compare scale factors
    if (!(mask & kAffine_Mask)) {
        const bool uniform = !SkScalarNearlyZero(mx) &&
                             SkScalarNearlyEqual(SkScalarAbs(mx), SkScalarAbs(my));
        return uniform;
    }
    const SkScalar sx = fMat[kMSkewX];
    const SkScalar sy = fMat[kMSkewY];

    if (is_degenerate_2x2(mx, sx, sy, my)) {
        return false;
    }

    // upper 2x2 is rotation/reflection + uniform scale if basis vectors
    // are 90 degree rotations of each other
    const bool similar = (SkScalarNearlyEqual(mx, my, tol) && SkScalarNearlyEqual(sx, -sy, tol))
                      || (SkScalarNearlyEqual(mx, -my, tol) && SkScalarNearlyEqual(sx, sy, tol));
    return similar;
}

bool SkMatrix::preservesRightAngles(SkScalar tol) const {
    const TypeMask mask = getType();

    if (mask <= kTranslate_Mask) {
        // identity, translate and/or scale
        return true;
    }
    if (mask & kPerspective_Mask) {
        return false;
    }

    SkASSERT(mask & (kAffine_Mask | kScale_Mask));

    const SkScalar mx = fMat[kMScaleX];
    const SkScalar my = fMat[kMScaleY];
    const SkScalar sx = fMat[kMSkewX];
    const SkScalar sy = fMat[kMSkewY];

    if (is_degenerate_2x2(mx, sx, sy, my)) {
        return false;
    }

    // upper 2x2 is scale + rotation/reflection if basis vectors are orthogonal
    const SkVector xBasis{mx, sy};
    const SkVector yBasis{sx, my};
    const bool orthogonal = SkScalarNearlyZero(xBasis.dot(yBasis), SkScalarSquare(tol));
    return orthogonal;
}

///////////////////////////////////////////////////////////////////////////////

SkMatrix& SkMatrix::setTranslate(SkScalar dx, SkScalar dy) {
    *this = SkMatrix(1, 0, dx,
                     0, 1, dy,
                     0, 0, 1,
                     (dx != 0 || dy != 0) ? kTranslate_Mask | kRectStaysRect_Mask
                                          : kIdentity_Mask  | kRectStaysRect_Mask);
    return *this;
}

SkMatrix& SkMatrix::preTranslate(SkScalar dx, SkScalar dy) {
    const unsigned mask = getType();

    if (mask <= kTranslate_Mask) {
        fMat[kMTransX] += dx;
        fMat[kMTransY] += dy;
    } else if (mask & kPerspective_Mask) {
        const SkMatrix m = SkMatrix::Translate(dx, dy);
        return preConcat(m);
    } else {
        fMat[kMTransX] += fMat[kMScaleX] * dx + fMat[kMSkewX] * dy;
        fMat[kMTransY] += fMat[kMSkewY] * dx + fMat[kMScaleY] * dy;
    }
    updateTranslateMask();
    return *this;
}

SkMatrix& SkMatrix::postTranslate(SkScalar dx, SkScalar dy) {
    if (hasPerspective()) {
        const SkMatrix m = SkMatrix::Translate(dx, dy);
        postConcat(m);
    } else {
        fMat[kMTransX] += dx;
        fMat[kMTransY] += dy;
        updateTranslateMask();
    }
    return *this;
}

///////////////////////////////////////////////////////////////////////////////

SkMatrix& SkMatrix::setScale(SkScalar sx, SkScalar sy, SkScalar px, SkScalar py) {
    if (1 == sx && 1 == sy) {
        reset();
    } else {
        setScaleTranslate(sx, sy, px - sx * px, py - sy * py);
    }
    return *this;
}

SkMatrix& SkMatrix::setScale(SkScalar sx, SkScalar sy) {
    const int rectMask = (sx == 0 || sy == 0) ? 0 : kRectStaysRect_Mask;
    *this = SkMatrix(sx, 0,  0,
                     0,  sy, 0,
                     0,  0,  1,
                     (sx == 1 && sy == 1) ? kIdentity_Mask | rectMask
                                          : kScale_Mask    | rectMask);
    return *this;
}

SkMatrix& SkMatrix::preScale(SkScalar sx, SkScalar sy, SkScalar px, SkScalar py) {
    if (1 == sx && 1 == sy) {
        return *this;
    }

    SkMatrix m{};
    m.setScale(sx, sy, px, py);
    return preConcat(m);
}

SkMatrix& SkMatrix::preScale(SkScalar sx, SkScalar sy) {
    if (1 == sx && 1 == sy) {
        return *this;
    }

    // the assumption is that these multiplies are very cheap, and that
    // a full concat and/or just computing the matrix type is more expensive.
    // Also, the fixed-point case checks for overflow, but the float doesn't,
    // so we can get away with these blind multiplies.

    fMat[kMScaleX] *= sx;
    fMat[kMSkewY]  *= sx;
    fMat[kMPersp0] *= sx;

    fMat[kMSkewX]  *= sy;
    fMat[kMScaleY] *= sy;
    fMat[kMPersp1] *= sy;

    // Attempt to simplify our type when applying an inverse scale.
    // TODO: The persp/affine preconditions are in place to keep the mask consistent with
    //       what computeTypeMask() would produce (persp/skew always implies kScale).
    //       We should investigate whether these flag dependencies are truly needed.
    if (fMat[kMScaleX] == 1 && fMat[kMScaleY] == 1
        && !(fTypeMask & (kPerspective_Mask | kAffine_Mask))) {
        clearTypeMask(kScale_Mask);
    } else {
        orTypeMask(kScale_Mask);
        // Remove kRectStaysRect if the preScale factors were 0
        if (!sx || !sy) {
            clearTypeMask(kRectStaysRect_Mask);
        }
    }
    return *this;
}

SkMatrix& SkMatrix::postScale(SkScalar sx, SkScalar sy, SkScalar px, SkScalar py) {
    if (1 == sx && 1 == sy) {
        return *this;
    }
    SkMatrix m{};
    m.setScale(sx, sy, px, py);
    return postConcat(m);
}

SkMatrix& SkMatrix::postScale(SkScalar sx, SkScalar sy) {
    if (1 == sx && 1 == sy) {
        return *this;
    }
    const SkMatrix m = SkMatrix::Scale(sx, sy);
    return postConcat(m);
}

// this perhaps can go away, if we have a fract/high-precision way to
// scale matrices
bool SkMatrix::postIDiv(int divx, int divy) {
    if (divx == 0 || divy == 0) {
        return false;
    }

    const float invX = 1.f / divx;
    const float invY = 1.f / divy;

    fMat[kMScaleX] *= invX;
    fMat[kMSkewX]  *= invX;
    fMat[kMTransX] *= invX;

    fMat[kMScaleY] *= invY;
    fMat[kMSkewY]  *= invY;
    fMat[kMTransY] *= invY;

    setTypeMask(kUnknown_Mask);
    return true;
}

////////////////////////////////////////////////////////////////////////////////////

SkMatrix& SkMatrix::setSinCos(SkScalar sinV, SkScalar cosV, SkScalar px, SkScalar py) {
    const SkScalar oneMinusCosV = 1 - cosV;

    fMat[kMScaleX]  = cosV;
    fMat[kMSkewX]   = -sinV;
    fMat[kMTransX]  = sinV * py + oneMinusCosV * px;

    fMat[kMSkewY]   = sinV;
    fMat[kMScaleY]  = cosV;
    fMat[kMTransY]  = -sinV * px + oneMinusCosV * py;

    fMat[kMPersp0] = 0;
    fMat[kMPersp1] = 0;
    fMat[kMPersp2] = 1;

    setTypeMask(kUnknown_Mask | kOnlyPerspectiveValid_Mask);
    return *this;
}

SkMatrix& SkMatrix::setRSXform(const SkRSXform& xform) {
    fMat[kMScaleX]  = xform.fSCos;
    fMat[kMSkewX]   = -xform.fSSin;
    fMat[kMTransX]  = xform.fTx;

    fMat[kMSkewY]   = xform.fSSin;
    fMat[kMScaleY]  = xform.fSCos;
    fMat[kMTransY]  = xform.fTy;

    fMat[kMPersp0] = 0;
    fMat[kMPersp1] = 0;
    fMat[kMPersp2] = 1;

    setTypeMask(kUnknown_Mask | kOnlyPerspectiveValid_Mask);
    return *this;
}

SkMatrix& SkMatrix::setSinCos(SkScalar sinV, SkScalar cosV) {
    fMat[kMScaleX]  = cosV;
    fMat[kMSkewX]   = -sinV;
    fMat[kMTransX]  = 0;

    fMat[kMSkewY]   = sinV;
    fMat[kMScaleY]  = cosV;
    fMat[kMTransY]  = 0;

    fMat[kMPersp0] = 0;
    fMat[kMPersp1] = 0;
    fMat[kMPersp2] = 1;

    setTypeMask(kUnknown_Mask | kOnlyPerspectiveValid_Mask);
    return *this;
}

SkMatrix& SkMatrix::setRotate(SkScalar degrees, SkScalar px, SkScalar py) {
    const SkScalar rad = SkDegreesToRadians(degrees);
    return setSinCos(SkScalarSinSnapToZero(rad), SkScalarCosSnapToZero(rad), px, py);
}

SkMatrix& SkMatrix::setRotate(SkScalar degrees) {
    const SkScalar rad = SkDegreesToRadians(degrees);
    return setSinCos(SkScalarSinSnapToZero(rad), SkScalarCosSnapToZero(rad));
}

SkMatrix& SkMatrix::preRotate(SkScalar degrees, SkScalar px, SkScalar py) {
    const SkMatrix m = SkMatrix::RotateDeg(degrees, {px, py});
    return preConcat(m);
}

SkMatrix& SkMatrix::preRotate(SkScalar degrees) {
    const SkMatrix m = SkMatrix::RotateDeg(degrees);
    return preConcat(m);
}

SkMatrix& SkMatrix::postRotate(SkScalar degrees, SkScalar px, SkScalar py) {
    const SkMatrix m = SkMatrix::RotateDeg(degrees, {px, py});
    return postConcat(m);
}

SkMatrix& SkMatrix::postRotate(SkScalar degrees) {
    const SkMatrix m = SkMatrix::RotateDeg(degrees);
    return postConcat(m);
}

////////////////////////////////////////////////////////////////////////////////////

SkMatrix& SkMatrix::setSkew(SkScalar sx, SkScalar sy, SkScalar px, SkScalar py) {
    *this = SkMatrix(1,  sx, -sx * py,
                     sy, 1,  -sy * px,
                     0,  0,  1,
                     kUnknown_Mask | kOnlyPerspectiveValid_Mask);
    return *this;
}

SkMatrix& SkMatrix::setSkew(SkScalar sx, SkScalar sy) {
    fMat[kMScaleX]  = 1;
    fMat[kMSkewX]   = sx;
    fMat[kMTransX]  = 0;

    fMat[kMSkewY]   = sy;
    fMat[kMScaleY]  = 1;
    fMat[kMTransY]  = 0;

    fMat[kMPersp0] = 0;
    fMat[kMPersp1] = 0;
    fMat[kMPersp2] = 1;

    setTypeMask(kUnknown_Mask | kOnlyPerspectiveValid_Mask);
    return *this;
}

SkMatrix& SkMatrix::preSkew(SkScalar sx, SkScalar sy, SkScalar px, SkScalar py) {
    SkMatrix m{};
    m.setSkew(sx, sy, px, py);
    return preConcat(m);
}

SkMatrix& SkMatrix::preSkew(SkScalar sx, SkScalar sy) {
    const SkMatrix m = SkMatrix::Skew(sx, sy);
    return preConcat(m);
}

SkMatrix& SkMatrix::postSkew(SkScalar sx, SkScalar sy, SkScalar px, SkScalar py) {
    SkMatrix m{};
    m.setSkew(sx, sy, px, py);
    return postConcat(m);
}

SkMatrix& SkMatrix::postSkew(SkScalar sx, SkScalar sy) {
    const SkMatrix m = SkMatrix::Skew(sx, sy);
    return postConcat(m);
}

///////////////////////////////////////////////////////////////////////////////

SkMatrix SkMatrix::ScaleTranslate(float sx, float sy, float tx, float ty) {
    uint8_t mask = 0;
    if (sx != 1 || sy != 1) {
        mask |= SkMatrix::kScale_Mask;
    }
    if (tx != 0.0f || ty != 0.0f) {
        mask |= SkMatrix::kTranslate_Mask;
    }
    if (sx != 0 && sy != 0) {
        mask |= SkMatrix::kRectStaysRect_Mask;
    }
    const SkMatrix matrix(sx,  0, tx,
                           0, sy, ty,
                           0,  0,  1,
                           mask);
    return matrix;
}

std::optional<SkMatrix> SkMatrix::Rect2Rect(const SkRect& src, const SkRect& dst, ScaleToFit stf) {
    if (src.isEmpty()) {
        return {};
    }

    float sx = sk_ieee_float_divide(dst.width(), src.width());
    float sy = sk_ieee_float_divide(dst.height(), src.height());
    bool xLarger = false;

    if (stf != kFill_ScaleToFit) {
        if (sx > sy) {
            xLarger = true;
            sx = sy;
        } else {
            sy = sx;
        }
    }

    float tx = dst.fLeft - src.fLeft * sx;
    float ty = dst.fTop - src.fTop * sy;
    if (stf == kCenter_ScaleToFit || stf == kEnd_ScaleToFit) {
        float diff = 0;

        if (xLarger) {
            diff = dst.width() - src.width() * sy;
        } else {
            diff = dst.height() - src.height() * sy;
        }

        if (stf == kCenter_ScaleToFit) {
            diff = diff / 2.f;
        }

        if (xLarger) {
            tx += diff;
        } else {
            ty += diff;
        }
    }
    const SkMatrix mapping = ScaleTranslate(sx, sy, tx, ty);
    return mapping;
}

///////////////////////////////////////////////////////////////////////////////

// a * b + c * d, accumulated in double precision and rounded once.
static inline float muladdmul(float a, float b, float c, float d) {
    const float sum = sk_double_to_float(static_cast<double>(a) * b + static_cast<double>(c) * d);
    return sum;
}

// Row of a 3x3 row-major matrix times a column of another: col advances by a row stride of 3.
static inline float rowcol3(const float row[], const float col[]) {
    const float product = row[0] * col[0] + row[1] * col[3] + row[2] * col[6];
    return product;
}

static bool only_scale_and_translate(unsigned mask) {
    const bool scaleTranslate =
            0 == (mask & (SkMatrix::kAffine_Mask | SkMatrix::kPerspective_Mask));
    return scaleTranslate;
}

// a or b may be this matrix: the result is formed in a temporary or copied whole.
SkMatrix& SkMatrix::setConcat(const SkMatrix& a, const SkMatrix& b) {
    const TypeMask aType = a.getType();
    const TypeMask bType = b.getType();

    if (a.isTriviallyIdentity()) {
        *this = b;
    } else if (b.isTriviallyIdentity()) {
        *this = a;
    } else if (only_scale_and_translate(aType | bType)) {
        setScaleTranslate(a.fMat[kMScaleX] * b.fMat[kMScaleX],
                          a.fMat[kMScaleY] * b.fMat[kMScaleY],
                          a.fMat[kMScaleX] * b.fMat[kMTransX] + a.fMat[kMTransX],
                          a.fMat[kMScaleY] * b.fMat[kMTransY] + a.fMat[kMTransY]);
    } else {
        SkMatrix tmp{};

        if ((aType | bType) & kPerspective_Mask) {
            tmp.fMat[kMScaleX] = rowcol3(&a.fMat[0], &b.fMat[0]);
            tmp.fMat[kMSkewX]  = rowcol3(&a.fMat[0], &b.fMat[1]);
            tmp.fMat[kMTransX] = rowcol3(&a.fMat[0], &b.fMat[2]);
            tmp.fMat[kMSkewY]  = rowcol3(&a.fMat[3], &b.fMat[0]);
            tmp.fMat[kMScaleY] = rowcol3(&a.fMat[3], &b.fMat[1]);
            tmp.fMat[kMTransY] = rowcol3(&a.fMat[3], &b.fMat[2]);
            tmp.fMat[kMPersp0] = rowcol3(&a.fMat[6], &b.fMat[0]);
            tmp.fMat[kMPersp1] = rowcol3(&a.fMat[6], &b.fMat[1]);
            tmp.fMat[kMPersp2] = rowcol3(&a.fMat[6], &b.fMat[2]);

            tmp.setTypeMask(kUnknown_Mask);
        } else {
            tmp.fMat[kMScaleX] = muladdmul(a.fMat[kMScaleX],
                                           b.fMat[kMScaleX],
                                           a.fMat[kMSkewX],
                                           b.fMat[kMSkewY]);

            tmp.fMat[kMSkewX]  = muladdmul(a.fMat[kMScaleX],
                                           b.fMat[kMSkewX],
                                           a.fMat[kMSkewX],
                                           b.fMat[kMScaleY]);

            tmp.fMat[kMTransX] = muladdmul(a.fMat[kMScaleX],
                                           b.fMat[kMTransX],
                                           a.fMat[kMSkewX],
                                           b.fMat[kMTransY]) + a.fMat[kMTransX];

            tmp.fMat[kMSkewY]  = muladdmul(a.fMat[kMSkewY],
                                           b.fMat[kMScaleX],
                                           a.fMat[kMScaleY],
                                           b.fMat[kMSkewY]);

            tmp.fMat[kMScaleY] = muladdmul(a.fMat[kMSkewY],
                                           b.fMat[kMSkewX],
                                           a.fMat[kMScaleY],
                                           b.fMat[kMScaleY]);

            tmp.fMat[kMTransY] = muladdmul(a.fMat[kMSkewY],
                                           b.fMat[kMTransX],
                                           a.fMat[kMScaleY],
                                           b.fMat[kMTransY]) + a.fMat[kMTransY];

            tmp.fMat[kMPersp0] = 0;
            tmp.fMat[kMPersp1] = 0;
            tmp.fMat[kMPersp2] = 1;
            tmp.setTypeMask(kUnknown_Mask | kOnlyPerspectiveValid_Mask);
        }
        *this = tmp;
    }
    return *this;
}

SkMatrix& SkMatrix::preConcat(const SkMatrix& mat) {
    // check for identity first, so we don't do a needless copy of ourselves
    // to ourselves inside setConcat()
    if (!mat.isIdentity()) {
        setConcat(*this, mat);
    }
    return *this;
}

SkMatrix& SkMatrix::postConcat(const SkMatrix& mat) {
    // check for identity first, so we don't do a needless copy of ourselves
    // to ourselves inside setConcat()
    if (!mat.isIdentity()) {
        setConcat(mat, *this);
    }
    return *this;
}

///////////////////////////////////////////////////////////////////////////////

/*  Matrix inversion is very expensive, but also the place where keeping
    precision may be most important (here and matrix concat). Hence to avoid
    bitmap blitting artifacts when walking the inverse, we use doubles for
    the intermediate math, even though we know that is more expensive.
 */

// (a * b - c * d) in single precision, then scaled and rounded once from double.
static inline SkScalar scross_dscale(SkScalar a, SkScalar b,
                                     SkScalar c, SkScalar d, double scale) {
    const float cross = a * b - c * d;
    const SkScalar scaled = SkDoubleToScalar(cross * scale);
    return scaled;
}

// (a * b - c * d) and the scale in double precision, rounded once.
static inline SkScalar dcross_dscale(double a, double b,
                                     double c, double d, double scale) {
    const double cross = a * b - c * d;
    const SkScalar scaled = SkDoubleToScalar(cross * scale);
    return scaled;
}

// Cofactor expansion along the first row; each 2x2 minor is computed in double precision.
static double sk_determinant(const float mat[9], bool isPerspective) {
    if (isPerspective) {
        const double minor0 = static_cast<double>(mat[SkMatrix::kMScaleY]) * mat[SkMatrix::kMPersp2]
                            - static_cast<double>(mat[SkMatrix::kMTransY]) * mat[SkMatrix::kMPersp1];
        const double minor1 = static_cast<double>(mat[SkMatrix::kMTransY]) * mat[SkMatrix::kMPersp0]
                            - static_cast<double>(mat[SkMatrix::kMSkewY]) * mat[SkMatrix::kMPersp2];
        const double minor2 = static_cast<double>(mat[SkMatrix::kMSkewY]) * mat[SkMatrix::kMPersp1]
                            - static_cast<double>(mat[SkMatrix::kMScaleY]) * mat[SkMatrix::kMPersp0];
        const double determinant = mat[SkMatrix::kMScaleX] * minor0
                                 + mat[SkMatrix::kMSkewX]  * minor1
                                 + mat[SkMatrix::kMTransX] * minor2;
        return determinant;
    }
    const double determinant =
            static_cast<double>(mat[SkMatrix::kMScaleX]) * mat[SkMatrix::kMScaleY]
          - static_cast<double>(mat[SkMatrix::kMSkewX]) * mat[SkMatrix::kMSkewY];
    return determinant;
}

static double sk_inv_determinant(const float mat[9], bool isPerspective) {
    const double det = sk_determinant(mat, isPerspective);

    // Since the determinant is on the order of the cube of the matrix members,
    // compare to the cube of the default nearly-zero constant (although an
    // estimate of the condition number would be better if it wasn't so expensive).
    if (SkScalarNearlyZero(sk_double_to_float(det),
                           SK_ScalarNearlyZero * SK_ScalarNearlyZero * SK_ScalarNearlyZero)) {
        return 0;
    }
    const double inverse = 1.0 / det;
    return inverse;
}

void SkMatrix::SetAffineIdentity(SkScalar affine[6]) {
    affine[kAScaleX] = 1;
    affine[kASkewY] = 0;
    affine[kASkewX] = 0;
    affine[kAScaleY] = 1;
    affine[kATransX] = 0;
    affine[kATransY] = 0;
}

bool SkMatrix::asAffine(SkScalar affine[6]) const {
    if (hasPerspective()) {
        return false;
    }
    if (affine) {
        affine[kAScaleX] = fMat[kMScaleX];
        affine[kASkewY] = fMat[kMSkewY];
        affine[kASkewX] = fMat[kMSkewX];
        affine[kAScaleY] = fMat[kMScaleY];
        affine[kATransX] = fMat[kMTransX];
        affine[kATransY] = fMat[kMTransY];
    }
    return true;
}

// dst and src may be the same points (in place); partial overlap is not supported.
void SkMatrix::mapPoints(SkSpan<SkPoint> dst, SkSpan<const SkPoint> src) const {
    const int count = min_count(dst, src);
    const MapPtsProc proc = getMapPtsProc();
    proc(*this, dst.data(), src.data(), count);
}

void SkMatrix::ComputeInv(SkScalar dst[9], const SkScalar src[9], double invDet, bool isPersp) {
    SkASSERT(src != dst);
    SkASSERT(src && dst);

    if (isPersp) {
        dst[kMScaleX] = scross_dscale(src[kMScaleY], src[kMPersp2], src[kMTransY], src[kMPersp1], invDet);
        dst[kMSkewX]  = scross_dscale(src[kMTransX], src[kMPersp1], src[kMSkewX],  src[kMPersp2], invDet);
        dst[kMTransX] = scross_dscale(src[kMSkewX],  src[kMTransY], src[kMTransX], src[kMScaleY], invDet);

        dst[kMSkewY]  = scross_dscale(src[kMTransY], src[kMPersp0], src[kMSkewY],  src[kMPersp2], invDet);
        dst[kMScaleY] = scross_dscale(src[kMScaleX], src[kMPersp2], src[kMTransX], src[kMPersp0], invDet);
        dst[kMTransY] = scross_dscale(src[kMTransX], src[kMSkewY],  src[kMScaleX], src[kMTransY], invDet);

        dst[kMPersp0] = scross_dscale(src[kMSkewY],  src[kMPersp1], src[kMScaleY], src[kMPersp0], invDet);
        dst[kMPersp1] = scross_dscale(src[kMSkewX],  src[kMPersp0], src[kMScaleX], src[kMPersp1], invDet);
        dst[kMPersp2] = scross_dscale(src[kMScaleX], src[kMScaleY], src[kMSkewX],  src[kMSkewY],  invDet);
    } else {   // not perspective
        dst[kMScaleX] = SkDoubleToScalar(src[kMScaleY] * invDet);
        dst[kMSkewX]  = SkDoubleToScalar(-src[kMSkewX] * invDet);
        dst[kMTransX] = dcross_dscale(src[kMSkewX], src[kMTransY], src[kMScaleY], src[kMTransX], invDet);

        dst[kMSkewY]  = SkDoubleToScalar(-src[kMSkewY] * invDet);
        dst[kMScaleY] = SkDoubleToScalar(src[kMScaleX] * invDet);
        dst[kMTransY] = dcross_dscale(src[kMSkewY], src[kMTransX], src[kMScaleX], src[kMTransY], invDet);

        dst[kMPersp0] = 0;
        dst[kMPersp1] = 0;
        dst[kMPersp2] = 1;
    }
}

std::optional<SkMatrix> SkMatrix::invert() const {
    const TypeMask mask = getType();

    if (mask == kIdentity_Mask) {
        return *this;
    }

    // Optimized invert for only scale and/or translation matrices.
    if (0 == (mask & ~(kScale_Mask | kTranslate_Mask))) {
        if (mask & kScale_Mask) {
            // Scale + (optional) Translate
            const SkScalar invSX = sk_ieee_float_divide(1.f, fMat[kMScaleX]);
            const SkScalar invSY = sk_ieee_float_divide(1.f, fMat[kMScaleY]);
            // Denormalized (non-zero) scale factors will overflow when inverted, in which case
            // the inverse matrix would not be finite, so return false.
            if (!SkIsFinite(invSX, invSY)) {
                return {};
            }
            const SkScalar invTX = -fMat[kMTransX] * invSX;
            const SkScalar invTY = -fMat[kMTransY] * invSY;
            // Make sure inverse translation didn't overflow/underflow after dividing by scale.
            // Also catches cases where the original matrix's translation values are not finite.
            if (!SkIsFinite(invTX, invTY)) {
                return {};
            }

            SkMatrix inv{};
            inv.fMat[kMSkewX] = 0;
            inv.fMat[kMSkewY] = 0;
            inv.fMat[kMPersp0] = 0;
            inv.fMat[kMPersp1] = 0;

            inv.fMat[kMScaleX] = invSX;
            inv.fMat[kMScaleY] = invSY;
            inv.fMat[kMPersp2] = 1;
            inv.fMat[kMTransX] = invTX;
            inv.fMat[kMTransY] = invTY;

            inv.setTypeMask(mask | kRectStaysRect_Mask);
            return inv;
        }

        // Translate-only
        if (!SkIsFinite(fMat[kMTransX], fMat[kMTransY])) {
            // Translation components aren't finite, so inverse isn't possible
            return {};
        }

        const SkMatrix inverse = SkMatrix::Translate(-fMat[kMTransX], -fMat[kMTransY]);
        return inverse;
    }

    const bool isPersp = (mask & kPerspective_Mask) != 0;
    const double invDet = sk_inv_determinant(fMat, isPersp);

    if (invDet == 0) { // underflow
        return {};
    }

    SkMatrix inv{};
    ComputeInv(inv.fMat, fMat, invDet, isPersp);
    if (!inv.isFinite()) {
        return {};
    }
    inv.setTypeMask(fTypeMask);
    return inv;
}

// A point mapped to w == 0 keeps its unprojected x and y.
SkPoint SkMatrix::mapPointPerspective(SkPoint p) const {
    const SkScalar x = (p.fX * fMat[kMScaleX] + p.fY * fMat[kMSkewX])  + fMat[kMTransX];
    const SkScalar y = (p.fX * fMat[kMSkewY]  + p.fY * fMat[kMScaleY]) + fMat[kMTransY];
    SkScalar z = (p.fX * fMat[kMPersp0] + p.fY * fMat[kMPersp1]) + fMat[kMPersp2];
    if (z) {
        z = 1 / z;
    }
    const SkPoint mapped{x * z, y * z};
    return mapped;
}

///////////////////////////////////////////////////////////////////////////////
// Point-mapping kernels, chosen once per call from the type mask. Each reads and writes
// count points; dst may equal src, and partial overlap is not supported. The vector
// kernels load and store pairs of points as four floats.

void SkMatrix::Identity_pts(const SkMatrix& m, SkPoint dst[], const SkPoint src[], int count) {
    SkASSERT(m.getType() == 0);

    if (dst != src && count > 0) {
        std::memcpy(dst, src, static_cast<size_t>(count) * sizeof(SkPoint));
    }
}

void SkMatrix::Trans_pts(const SkMatrix& m, SkPoint dst[], const SkPoint src[], int count) {
    SkASSERT(m.getType() <= SkMatrix::kTranslate_Mask);
    if (count > 0) {
        const SkScalar tx = m.getTranslateX();
        const SkScalar ty = m.getTranslateY();
        if (count & 1) {
            (*dst).fX = (*src).fX + tx;
            (*dst).fY = (*src).fY + ty;
            src += 1;
            dst += 1;
        }
        const skvx::float4 trans4(tx, ty, tx, ty);
        count >>= 1;
        if (count & 1) {
            (skvx::float4::Load(src) + trans4).store(dst);
            src += 2;
            dst += 2;
        }
        count >>= 1;
        for (int i = 0; i < count; ++i) {
            (skvx::float4::Load(src+0) + trans4).store(dst+0);
            (skvx::float4::Load(src+2) + trans4).store(dst+2);
            src += 4;
            dst += 4;
        }
    }
}

void SkMatrix::Scale_pts(const SkMatrix& m, SkPoint dst[], const SkPoint src[], int count) {
    SkASSERT(m.getType() <= (SkMatrix::kScale_Mask | SkMatrix::kTranslate_Mask));
    if (count > 0) {
        const SkScalar tx = m.getTranslateX();
        const SkScalar ty = m.getTranslateY();
        const SkScalar sx = m.getScaleX();
        const SkScalar sy = m.getScaleY();
        const skvx::float4 trans4(tx, ty, tx, ty);
        const skvx::float4 scale4(sx, sy, sx, sy);
        if (count & 1) {
            const skvx::float4 p((*src).fX, (*src).fY, 0, 0);
            const skvx::float4 mapped = p * scale4 + trans4;
            (*dst).fX = mapped[0];
            (*dst).fY = mapped[1];
            src += 1;
            dst += 1;
        }
        count >>= 1;
        if (count & 1) {
            (skvx::float4::Load(src) * scale4 + trans4).store(dst);
            src += 2;
            dst += 2;
        }
        count >>= 1;
        for (int i = 0; i < count; ++i) {
            (skvx::float4::Load(src+0) * scale4 + trans4).store(dst+0);
            (skvx::float4::Load(src+2) * scale4 + trans4).store(dst+2);
            src += 4;
            dst += 4;
        }
    }
}

void SkMatrix::Persp_pts(const SkMatrix& m, SkPoint dst[],
                         const SkPoint src[], int count) {
    SkASSERT(m.hasPerspective());

    if (count > 0) {
        do {
            const SkScalar sy = (*src).fY;
            const SkScalar sx = (*src).fX;
            src += 1;

            const SkScalar x = (sx * m.fMat[kMScaleX] + sy * m.fMat[kMSkewX])  + m.fMat[kMTransX];
            const SkScalar y = (sx * m.fMat[kMSkewY]  + sy * m.fMat[kMScaleY]) + m.fMat[kMTransY];
            SkScalar z = (sx * m.fMat[kMPersp0] + sy * m.fMat[kMPersp1]) + m.fMat[kMPersp2];
            if (z) {
                z = 1 / z;
            }

            (*dst).fY = y * z;
            (*dst).fX = x * z;
            dst += 1;
        } while (--count);
    }
}

void SkMatrix::Affine_vpts(const SkMatrix& m, SkPoint dst[], const SkPoint src[], int count) {
    SkASSERT(m.getType() != SkMatrix::kPerspective_Mask);
    if (count > 0) {
        const SkScalar tx = m.getTranslateX();
        const SkScalar ty = m.getTranslateY();
        const SkScalar sx = m.getScaleX();
        const SkScalar sy = m.getScaleY();
        const SkScalar kx = m.getSkewX();
        const SkScalar ky = m.getSkewY();
        const skvx::float4 trans4(tx, ty, tx, ty);
        const skvx::float4 scale4(sx, sy, sx, sy);
        const skvx::float4  skew4(kx, ky, kx, ky);    // applied to swizzle of src4
        const bool trailingElement = (count & 1);
        const int pairCount = count >> 1;
        // The upper lanes of a trailing single point keep the previous pair's values,
        // or zero when there is no pair; they are computed and discarded.
        skvx::float4 src4(0.0f);
        for (int i = 0; i < pairCount; ++i) {
            src4 = skvx::float4::Load(src);
            const skvx::float4 swz4 = skvx::shuffle<1,0,3,2>(src4);  // y0 x0, y1 x1
            const skvx::float4 mapped = src4 * scale4 + swz4 * skew4 + trans4;
            mapped.store(dst);
            src += 2;
            dst += 2;
        }
        if (trailingElement) {
            // We use the same logic here to ensure that the math stays consistent throughout, even
            // though the high float2 is ignored.
            src4.lo = skvx::float2::Load(src);
            const skvx::float4 swz4 = skvx::shuffle<1,0,3,2>(src4);  // y0 x0, y1 x1
            const skvx::float4 mapped = src4 * scale4 + swz4 * skew4 + trans4;
            mapped.lo.store(dst);
        }
    }
}

const SkMatrix::MapPtsProc SkMatrix::gMapPtsProcs[] = {
    SkMatrix::Identity_pts, SkMatrix::Trans_pts,
    SkMatrix::Scale_pts,    SkMatrix::Scale_pts,
    SkMatrix::Affine_vpts,  SkMatrix::Affine_vpts,
    SkMatrix::Affine_vpts,  SkMatrix::Affine_vpts,
    // repeat the persp proc 8 times
    SkMatrix::Persp_pts,    SkMatrix::Persp_pts,
    SkMatrix::Persp_pts,    SkMatrix::Persp_pts,
    SkMatrix::Persp_pts,    SkMatrix::Persp_pts,
    SkMatrix::Persp_pts,    SkMatrix::Persp_pts
};

///////////////////////////////////////////////////////////////////////////////

// dst and src may be the same points (in place); partial overlap is not supported.
void SkMatrix::mapHomogeneousPoints(SkSpan<SkPoint3> dst, SkSpan<const SkPoint3> src) const {
    const int count = min_count(dst, src);
    if (isIdentity()) {
        for (int i = 0; i < count; ++i) {
            dst[i] = src[i];
        }
        return;
    }
    for (int i = 0; i < count; ++i) {
        const SkScalar sx = src[i].fX;
        const SkScalar sy = src[i].fY;
        const SkScalar sw = src[i].fZ;
        const SkScalar x = sx * fMat[kMScaleX] + sy * fMat[kMSkewX]  + sw * fMat[kMTransX];
        const SkScalar y = sx * fMat[kMSkewY]  + sy * fMat[kMScaleY] + sw * fMat[kMTransY];
        const SkScalar w = sx * fMat[kMPersp0] + sy * fMat[kMPersp1] + sw * fMat[kMPersp2];
        dst[i].set(x, y, w);
    }
}

void SkMatrix::mapPointsToHomogeneous(SkSpan<SkPoint3> dst, SkSpan<const SkPoint> src) const {
    const int count = min_count(dst, src);

    if (isIdentity()) {
        for (int i = 0; i < count; ++i) {
            dst[i] = { src[i].fX, src[i].fY, 1 };
        }
    } else if (hasPerspective()) {
        for (int i = 0; i < count; ++i) {
            dst[i] = {
                fMat[0] * src[i].fX + fMat[1] * src[i].fY + fMat[2],
                fMat[3] * src[i].fX + fMat[4] * src[i].fY + fMat[5],
                fMat[6] * src[i].fX + fMat[7] * src[i].fY + fMat[8],
            };
        }
    } else {    // affine
        for (int i = 0; i < count; ++i) {
            dst[i] = {
                fMat[0] * src[i].fX + fMat[1] * src[i].fY + fMat[2],
                fMat[3] * src[i].fX + fMat[4] * src[i].fY + fMat[5],
                1,
            };
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

void SkMatrix::mapVectors(SkSpan<SkPoint> dst, SkSpan<const SkPoint> src) const {
    if (hasPerspective()) {
        const SkPoint origin = mapPointPerspective({0, 0});

        for (int i = min_count(dst, src) - 1; i >= 0; --i) {
            dst[i] = mapPointPerspective(src[i]) - origin;
        }
    } else {
        SkMatrix tmp = *this;

        tmp.fMat[kMTransX] = 0;
        tmp.fMat[kMTransY] = 0;
        tmp.clearTypeMask(kTranslate_Mask);
        tmp.mapPoints(dst, src);
    }
}

static skvx::float4 sort_as_rect(const skvx::float4& ltrb) {
    const skvx::float4 rblt(ltrb[2], ltrb[3], ltrb[0], ltrb[1]);
    const skvx::float4 min = skvx::min(ltrb, rblt);
    const skvx::float4 max = skvx::max(ltrb, rblt);
    // We can extract either pair [0,1] or [2,3] from min and max and be correct, but on
    // ARM this sequence generates the fastest (a single instruction).
    const skvx::float4 sorted(min[2], min[3], max[0], max[1]);
    return sorted;
}

static skvx::float4 load_ltrb(const SkRect& rect) {
    const skvx::float4 edges(rect.fLeft, rect.fTop, rect.fRight, rect.fBottom);
    return edges;
}

static void store_ltrb(const skvx::float4& edges, SkRect& rect) {
    rect.setLTRB(edges[0], edges[1], edges[2], edges[3]);
}

void SkMatrix::mapRectScaleTranslate(SkRect* dst, const SkRect& src) const {
    SkASSERT(dst);
    SkASSERT(isScaleTranslate());

    const SkScalar sx = fMat[kMScaleX];
    const SkScalar sy = fMat[kMScaleY];
    const SkScalar tx = fMat[kMTransX];
    const SkScalar ty = fMat[kMTransY];
    const skvx::float4 scale(sx, sy, sx, sy);
    const skvx::float4 trans(tx, ty, tx, ty);
    const skvx::float4 mapped = load_ltrb(src) * scale + trans;
    store_ltrb(sort_as_rect(mapped), *dst);
}

bool SkMatrix::mapRect(SkRect* dst, const SkRect& src) const {
    SkASSERT(dst);

    // dst may be &src: src is read completely before dst is written.
    if (getType() <= kTranslate_Mask) {
        const SkScalar tx = fMat[kMTransX];
        const SkScalar ty = fMat[kMTransY];
        const skvx::float4 trans(tx, ty, tx, ty);
        const skvx::float4 mapped = load_ltrb(src) + trans;
        store_ltrb(sort_as_rect(mapped), *dst);
        return true;
    }
    if (isScaleTranslate()) {
        mapRectScaleTranslate(dst, src);
        return true;
    }
    if (hasPerspective()) {
        // Perspective bounds are those of the clipped, projected rectangle contour.
        SkPathBuilder builder{};
        builder.addRect(src);
        builder.transform(*this);
        *dst = builder.computeBounds();
        return false;
    }
    std::array<SkPoint, 4> quad = src.toQuad();
    mapPoints(quad);
    (*dst).setBoundsNoCheck(quad);
    const bool staysRect = rectStaysRect();   // might still return true if rotated by 90, etc.
    return staysRect;
}

SkScalar SkMatrix::mapRadius(SkScalar radius) const {
    SkVector vec[2] = {{radius, 0}, {0, radius}};
    mapVectors(vec);

    const SkScalar d0 = vec[0].length();
    const SkScalar d1 = vec[1].length();

    // return geometric mean
    const SkScalar mean = SkScalarSqrt(d0 * d1);
    return mean;
}

///////////////////////////////////////////////////////////////////////////////

// True when x is zero or so small that its square underflows to zero.
static inline bool checkForZero(float x) {
    const bool negligible = x*x == 0;
    return negligible;
}

bool SkMatrix::Poly2Proc(const SkPoint srcPt[], SkMatrix* dst) {
    (*dst).fMat[kMScaleX] = srcPt[1].fY - srcPt[0].fY;
    (*dst).fMat[kMSkewY]  = srcPt[0].fX - srcPt[1].fX;
    (*dst).fMat[kMPersp0] = 0;

    (*dst).fMat[kMSkewX]  = srcPt[1].fX - srcPt[0].fX;
    (*dst).fMat[kMScaleY] = srcPt[1].fY - srcPt[0].fY;
    (*dst).fMat[kMPersp1] = 0;

    (*dst).fMat[kMTransX] = srcPt[0].fX;
    (*dst).fMat[kMTransY] = srcPt[0].fY;
    (*dst).fMat[kMPersp2] = 1;
    (*dst).setTypeMask(kUnknown_Mask);
    return true;
}

bool SkMatrix::Poly3Proc(const SkPoint srcPt[], SkMatrix* dst) {
    (*dst).fMat[kMScaleX] = srcPt[2].fX - srcPt[0].fX;
    (*dst).fMat[kMSkewY]  = srcPt[2].fY - srcPt[0].fY;
    (*dst).fMat[kMPersp0] = 0;

    (*dst).fMat[kMSkewX]  = srcPt[1].fX - srcPt[0].fX;
    (*dst).fMat[kMScaleY] = srcPt[1].fY - srcPt[0].fY;
    (*dst).fMat[kMPersp1] = 0;

    (*dst).fMat[kMTransX] = srcPt[0].fX;
    (*dst).fMat[kMTransY] = srcPt[0].fY;
    (*dst).fMat[kMPersp2] = 1;
    (*dst).setTypeMask(kUnknown_Mask);
    return true;
}

bool SkMatrix::Poly4Proc(const SkPoint srcPt[], SkMatrix* dst) {
    float a1 = 0;
    float a2 = 0;

    const float x0 = srcPt[2].fX - srcPt[0].fX;
    const float y0 = srcPt[2].fY - srcPt[0].fY;
    const float x1 = srcPt[2].fX - srcPt[1].fX;
    const float y1 = srcPt[2].fY - srcPt[1].fY;
    const float x2 = srcPt[2].fX - srcPt[3].fX;
    const float y2 = srcPt[2].fY - srcPt[3].fY;

    /* check if abs(x2) > abs(y2) */
    if ( x2 > 0 ? y2 > 0 ? x2 > y2 : x2 > -y2 : y2 > 0 ? -x2 > y2 : x2 < y2) {
        const float denom = sk_ieee_float_divide(x1 * y2, x2) - y1;
        if (checkForZero(denom)) {
            return false;
        }
        a1 = (((x0 - x1) * y2 / x2) - y0 + y1) / denom;
    } else {
        const float denom = x1 - sk_ieee_float_divide(y1 * x2, y2);
        if (checkForZero(denom)) {
            return false;
        }
        a1 = (x0 - x1 - sk_ieee_float_divide((y0 - y1) * x2, y2)) / denom;
    }

    /* check if abs(x1) > abs(y1) */
    if ( x1 > 0 ? y1 > 0 ? x1 > y1 : x1 > -y1 : y1 > 0 ? -x1 > y1 : x1 < y1) {
        const float denom = y2 - sk_ieee_float_divide(x2 * y1, x1);
        if (checkForZero(denom)) {
            return false;
        }
        a2 = (y0 - y2 - sk_ieee_float_divide((x0 - x2) * y1, x1)) / denom;
    } else {
        const float denom = sk_ieee_float_divide(y2 * x1, y1) - x2;
        if (checkForZero(denom)) {
            return false;
        }
        a2 = (sk_ieee_float_divide((y0 - y2) * x1, y1) - x0 + x2) / denom;
    }

    (*dst).fMat[kMScaleX] = a2 * srcPt[3].fX + srcPt[3].fX - srcPt[0].fX;
    (*dst).fMat[kMSkewY]  = a2 * srcPt[3].fY + srcPt[3].fY - srcPt[0].fY;
    (*dst).fMat[kMPersp0] = a2;

    (*dst).fMat[kMSkewX]  = a1 * srcPt[1].fX + srcPt[1].fX - srcPt[0].fX;
    (*dst).fMat[kMScaleY] = a1 * srcPt[1].fY + srcPt[1].fY - srcPt[0].fY;
    (*dst).fMat[kMPersp1] = a1;

    (*dst).fMat[kMTransX] = srcPt[0].fX;
    (*dst).fMat[kMTransY] = srcPt[0].fY;
    (*dst).fMat[kMPersp2] = 1;
    (*dst).setTypeMask(kUnknown_Mask);
    return true;
}

using PolyMapProc = bool (*)(const SkPoint[], SkMatrix*);

/*  Originally adapted from Rob Johnson's original sample code in QuickDraw GX
*/
std::optional<SkMatrix> SkMatrix::PolyToPoly(SkSpan<const SkPoint> src, SkSpan<const SkPoint> dst) {
    if (src.size() != dst.size() || src.size() > 4) {
        return {};
    }

    PolyMapProc proc = nullptr;
    switch (src.size()) {
        case 0:
            return SkMatrix::I();
        case 1: {
            const SkMatrix translation = SkMatrix::Translate(dst[0] - src[0]);
            return translation;
        }
        case 2:
            proc = SkMatrix::Poly2Proc;
            break;
        case 3:
            proc = SkMatrix::Poly3Proc;
            break;
        case 4:
            proc = SkMatrix::Poly4Proc;
            break;
        default:
            // Larger counts were rejected above.
            return {};
    }

    // Map the unit square (or triangle, or segment) to each polygon; the result
    // maps src to dst through it.
    SkMatrix tempMap{};
    if (!proc(src.data(), &tempMap)) {
        return {};
    }
    const std::optional<SkMatrix> inverse = tempMap.invert();
    if (!inverse.has_value()) {
        return {};
    }
    if (!proc(dst.data(), &tempMap)) {
        return {};
    }
    const SkMatrix mapping = tempMap * inverse.value();
    return mapping;
}

///////////////////////////////////////////////////////////////////////////////

enum MinMaxOrBoth {
    kMin_MinMaxOrBoth,
    kMax_MinMaxOrBoth,
    kBoth_MinMaxOrBoth
};

// Writes results[0] (and results[1] for kBoth); results is unchanged when this returns false
// before computing, and may hold a non-finite value when it returns false afterward.
template <MinMaxOrBoth MIN_MAX_OR_BOTH> bool get_scale_factor(SkMatrix::TypeMask typeMask,
                                                              const SkScalar m[9],
                                                              SkScalar results[/*1 or 2*/]) {
    if (typeMask & SkMatrix::kPerspective_Mask) {
        return false;
    }
    if (SkMatrix::kIdentity_Mask == typeMask) {
        results[0] = SK_Scalar1;
        if (kBoth_MinMaxOrBoth == MIN_MAX_OR_BOTH) {
            results[1] = SK_Scalar1;
        }
        return true;
    }
    if (!(typeMask & SkMatrix::kAffine_Mask)) {
        if (kMin_MinMaxOrBoth == MIN_MAX_OR_BOTH) {
             results[0] = std::min(SkScalarAbs(m[SkMatrix::kMScaleX]),
                                   SkScalarAbs(m[SkMatrix::kMScaleY]));
        } else if (kMax_MinMaxOrBoth == MIN_MAX_OR_BOTH) {
             results[0] = std::max(SkScalarAbs(m[SkMatrix::kMScaleX]),
                                   SkScalarAbs(m[SkMatrix::kMScaleY]));
        } else {
            results[0] = SkScalarAbs(m[SkMatrix::kMScaleX]);
            results[1] = SkScalarAbs(m[SkMatrix::kMScaleY]);
             if (results[0] > results[1]) {
                 using std::swap;
                 swap(results[0], results[1]);
             }
        }
        return true;
    }
    // ignore the translation part of the matrix, just look at 2x2 portion.
    // compute singular values, take largest or smallest abs value.
    // [a b; b c] = A^T*A
    const float a = m[SkMatrix::kMScaleX] * m[SkMatrix::kMScaleX] +
                    m[SkMatrix::kMSkewY]  * m[SkMatrix::kMSkewY];
    const float b = m[SkMatrix::kMScaleX] * m[SkMatrix::kMSkewX] +
                    m[SkMatrix::kMScaleY] * m[SkMatrix::kMSkewY];
    const float c = m[SkMatrix::kMSkewX]  * m[SkMatrix::kMSkewX] +
                    m[SkMatrix::kMScaleY] * m[SkMatrix::kMScaleY];
    // eigenvalues of A^T*A are the squared singular values of A.
    // characteristic equation is det((A^T*A) - l*I) = 0
    // l^2 - (a + c)l + (ac-b^2)
    // solve using quadratic equation (divisor is non-zero since l^2 has 1 coeff
    // and roots are guaranteed to be pos and real).
    const float bSqd = b * b;
    // if upper left 2x2 is orthogonal save some math
    if (bSqd <= SK_ScalarNearlyZero*SK_ScalarNearlyZero) {
        if (kMin_MinMaxOrBoth == MIN_MAX_OR_BOTH) {
            results[0] = std::min(a, c);
        } else if (kMax_MinMaxOrBoth == MIN_MAX_OR_BOTH) {
            results[0] = std::max(a, c);
        } else {
            results[0] = a;
            results[1] = c;
            if (results[0] > results[1]) {
                using std::swap;
                swap(results[0], results[1]);
            }
        }
    } else {
        const float aminusc = a - c;
        const float apluscdiv2 = (a + c) / 2.f;
        const float x = std::sqrt(aminusc * aminusc + 4 * bSqd) / 2.f;
        if (kMin_MinMaxOrBoth == MIN_MAX_OR_BOTH) {
            results[0] = apluscdiv2 - x;
        } else if (kMax_MinMaxOrBoth == MIN_MAX_OR_BOTH) {
            results[0] = apluscdiv2 + x;
        } else {
            results[0] = apluscdiv2 - x;
            results[1] = apluscdiv2 + x;
        }
    }
    if (!SkIsFinite(results[0])) {
        return false;
    }
    // Due to the floating point inaccuracy, there might be an error in a, b, c
    // calculated above, further deepened by subsequent arithmetic operations
    // on them. Therefore, we allow and cap the nearly-zero negative values.
    if (results[0] < 0) {
        results[0] = 0;
    }
    results[0] = SkScalarSqrt(results[0]);
    if (kBoth_MinMaxOrBoth == MIN_MAX_OR_BOTH) {
        if (!SkIsFinite(results[1])) {
            return false;
        }
        if (results[1] < 0) {
            results[1] = 0;
        }
        results[1] = SkScalarSqrt(results[1]);
    }
    return true;
}

SkScalar SkMatrix::getMinScale() const {
    SkScalar factor = 0;
    if (!get_scale_factor<kMin_MinMaxOrBoth>(getType(), fMat, &factor)) {
        return -1;
    }
    return factor;
}

SkScalar SkMatrix::getMaxScale() const {
    SkScalar factor = 0;
    if (!get_scale_factor<kMax_MinMaxOrBoth>(getType(), fMat, &factor)) {
        return -1;
    }
    return factor;
}

bool SkMatrix::getMinMaxScales(SkScalar scaleFactors[2]) const {
    const bool valid = get_scale_factor<kBoth_MinMaxOrBoth>(getType(), fMat, scaleFactors);
    return valid;
}

const SkMatrix& SkMatrix::I() {
    static constexpr SkMatrix identity;
    SkASSERT(identity.isIdentity());
    return identity;
}

const SkMatrix& SkMatrix::InvalidMatrix() {
    static constexpr SkMatrix invalid(SK_ScalarMax, SK_ScalarMax, SK_ScalarMax,
                                      SK_ScalarMax, SK_ScalarMax, SK_ScalarMax,
                                      SK_ScalarMax, SK_ScalarMax, SK_ScalarMax,
                                      kTranslate_Mask | kScale_Mask |
                                      kAffine_Mask | kPerspective_Mask);
    return invalid;
}

bool SkMatrix::decomposeScale(SkSize* scale, SkMatrix* remaining) const {
    if (hasPerspective()) {
        return false;
    }

    const SkScalar sx = SkVector::Length(getScaleX(), getSkewY());
    const SkScalar sy = SkVector::Length(getSkewX(), getScaleY());
    if (!SkIsFinite(sx, sy) ||
        SkScalarNearlyZero(sx) || SkScalarNearlyZero(sy)) {
        return false;
    }

    if (scale) {
        (*scale).set(sx, sy);
    }
    if (remaining) {
        *remaining = *this;
        (*remaining).preScale(SkScalarInvert(sx), SkScalarInvert(sy));
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////

// The serialized form is the nine values in row-major order; the type mask is derived.
static constexpr size_t kMatrixSizeInMemory = 9 * sizeof(SkScalar);

size_t SkMatrix::writeToMemory(void* buffer) const {
    // A null buffer queries the size.
    if (buffer) {
        std::memcpy(buffer, fMat, kMatrixSizeInMemory);
    }
    return kMatrixSizeInMemory;
}

size_t SkMatrix::readFromMemory(const void* buffer, size_t length) {
    if (length < kMatrixSizeInMemory) {
        return 0;
    }
    std::memcpy(fMat, buffer, kMatrixSizeInMemory);
    setTypeMask(kUnknown_Mask);
    // Figure out the type now so that we're thread-safe
    (void)getType();
    return kMatrixSizeInMemory;
}

void SkMatrix::dump() const {
    SkString str{};
    str.appendf("[%8.4f %8.4f %8.4f][%8.4f %8.4f %8.4f][%8.4f %8.4f %8.4f]",
             fMat[0], fMat[1], fMat[2], fMat[3], fMat[4], fMat[5],
             fMat[6], fMat[7], fMat[8]);
    SkDebugf("%s\n", str.c_str());
}

///////////////////////////////////////////////////////////////////////////////

// True when x is an integer that fits in int. Larger magnitudes, infinities and NaN are not
// treated as whole-pixel translations.
static bool is_int_translation(float x) {
    if (!(x >= -2147483648.0f && x < 2147483648.0f)) {
        return false;
    }
    const bool integral = x == static_cast<float>(static_cast<int>(x));
    return integral;
}

bool SkTreatAsSprite(const SkMatrix& mat, const SkISize& size, const SkSamplingOptions& sampling,
                     bool isAntiAlias) {
    if (!SkSamplingPriv::NoChangeWithIdentityMatrix(sampling)) {
        return false;
    }

    // Our path aa is 2-bits, and our rect aa is 8, so we could use 8,
    // but in practice 4 seems enough (still looks smooth) and allows
    // more slightly fractional cases to fall into the fast (sprite) case.
    static constexpr unsigned kAntiAliasSubpixelBits = 4;

    const unsigned subpixelBits = isAntiAlias ? kAntiAliasSubpixelBits : 0;

    // quick reject on affine or perspective
    if (mat.getType() & ~(SkMatrix::kScale_Mask | SkMatrix::kTranslate_Mask)) {
        return false;
    }

    // We don't want to snap to pixels if we're asking for linear filtering with
    // a subpixel translation. (b/41322892).
    // This mirrors `tweak_sampling` in SkImageShader.cpp
    if (sampling.filter == SkFilterMode::kLinear &&
        (!is_int_translation(mat.getTranslateX()) || !is_int_translation(mat.getTranslateY()))) {
        return false;
    }

    // quick success check
    if (!subpixelBits && !(mat.getType() & ~SkMatrix::kTranslate_Mask)) {
        return true;
    }

    // mapRect supports negative scales, so we eliminate those first
    if (mat.getScaleX() < 0 || mat.getScaleY() < 0) {
        return false;
    }

    SkIRect isrc = SkIRect::MakeSize(size);
    SkRect src{};
    src.set(isrc);
    SkRect dst{};
    mat.mapRect(&dst, src);

    // just apply the translate to isrc
    isrc.offset(SkScalarRoundToInt(mat.getTranslateX()),
                SkScalarRoundToInt(mat.getTranslateY()));

    if (subpixelBits) {
        isrc.fLeft = SkLeftShift(isrc.fLeft, subpixelBits);
        isrc.fTop = SkLeftShift(isrc.fTop, subpixelBits);
        isrc.fRight = SkLeftShift(isrc.fRight, subpixelBits);
        isrc.fBottom = SkLeftShift(isrc.fBottom, subpixelBits);

        const float scale = 1 << subpixelBits;
        dst.fLeft *= scale;
        dst.fTop *= scale;
        dst.fRight *= scale;
        dst.fBottom *= scale;
    }

    SkIRect idst{};
    dst.round(&idst);
    const bool sprite = isrc == idst;
    return sprite;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

SkScalar SkMatrixPriv::ComputeResScaleForStroking(const SkMatrix& matrix) {
    // Not sure how to handle perspective differently, so we just don't try (yet)
    const SkScalar sx = SkPoint::Length(matrix[SkMatrix::kMScaleX], matrix[SkMatrix::kMSkewY]);
    const SkScalar sy = SkPoint::Length(matrix[SkMatrix::kMSkewX],  matrix[SkMatrix::kMScaleY]);
    if (SkIsFinite(sx, sy)) {
        const SkScalar scale = std::max(sx, sy);
        if (scale > 0) {
            return scale;
        }
    }
    return 1;
}
