/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkM44.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkRect.h"
#include "include/private/SkFloatingPoint.h"
#include "src/core/SkMatrixInvert.h"
#include "src/core/SkMatrixPriv.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkVx.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

// Elementwise IEEE equality: a matrix holding NaN equals no matrix, itself included.
bool SkM44::operator==(const SkM44& other) const {
    const skvx::float4 a0 = skvx::float4::Load(fMat +  0);
    const skvx::float4 a1 = skvx::float4::Load(fMat +  4);
    const skvx::float4 a2 = skvx::float4::Load(fMat +  8);
    const skvx::float4 a3 = skvx::float4::Load(fMat + 12);

    const skvx::float4 b0 = skvx::float4::Load(other.fMat +  0);
    const skvx::float4 b1 = skvx::float4::Load(other.fMat +  4);
    const skvx::float4 b2 = skvx::float4::Load(other.fMat +  8);
    const skvx::float4 b3 = skvx::float4::Load(other.fMat + 12);

    const skvx::int4 eq = (a0 == b0) & (a1 == b1) & (a2 == b2) & (a3 == b3);
    return (eq[0] & eq[1] & eq[2] & eq[3]) == ~0;
}

// One column of a * b: the columns of a weighted by the entries of b's column r.
static skvx::float4 concat_column(skvx::float4 c0, skvx::float4 c1, skvx::float4 c2,
                                  skvx::float4 c3, skvx::float4 r) {
    return c0*r[0] + (c1*r[1] + (c2*r[2] + c3*r[3]));
}

SkM44& SkM44::setConcat(const SkM44& a, const SkM44& b) {
    const skvx::float4 c0 = skvx::float4::Load(a.fMat +  0);
    const skvx::float4 c1 = skvx::float4::Load(a.fMat +  4);
    const skvx::float4 c2 = skvx::float4::Load(a.fMat +  8);
    const skvx::float4 c3 = skvx::float4::Load(a.fMat + 12);

    const skvx::float4 m0 = concat_column(c0, c1, c2, c3, skvx::float4::Load(b.fMat +  0));
    const skvx::float4 m1 = concat_column(c0, c1, c2, c3, skvx::float4::Load(b.fMat +  4));
    const skvx::float4 m2 = concat_column(c0, c1, c2, c3, skvx::float4::Load(b.fMat +  8));
    const skvx::float4 m3 = concat_column(c0, c1, c2, c3, skvx::float4::Load(b.fMat + 12));

    m0.store(fMat +  0);
    m1.store(fMat +  4);
    m2.store(fMat +  8);
    m3.store(fMat + 12);
    return *this;
}

// One column of this * b for a 3x3 b, whose missing z row and column are identity.
static skvx::float4 concat_3x3_column(skvx::float4 c0, skvx::float4 c1, skvx::float4 c3,
                                      float r0, float r1, float r3) {
    return (c0*r0 + (c1*r1 + c3*r3));
}

SkM44& SkM44::preConcat(const SkMatrix& b) {
    const skvx::float4 c0 = skvx::float4::Load(fMat +  0);
    const skvx::float4 c1 = skvx::float4::Load(fMat +  4);
    const skvx::float4 c3 = skvx::float4::Load(fMat + 12);

    const skvx::float4 m0 = concat_3x3_column(c0, c1, c3, b[0], b[3], b[6]);
    const skvx::float4 m1 = concat_3x3_column(c0, c1, c3, b[1], b[4], b[7]);
    const skvx::float4 m3 = concat_3x3_column(c0, c1, c3, b[2], b[5], b[8]);

    m0.store(fMat +  0);
    m1.store(fMat +  4);
    m3.store(fMat + 12);
    return *this;
}

SkM44& SkM44::preTranslate(SkScalar x, SkScalar y, SkScalar z) {
    const skvx::float4 c0 = skvx::float4::Load(fMat +  0);
    const skvx::float4 c1 = skvx::float4::Load(fMat +  4);
    const skvx::float4 c2 = skvx::float4::Load(fMat +  8);
    const skvx::float4 c3 = skvx::float4::Load(fMat + 12);

    // only need to update the last column
    (c0*x + (c1*y + (c2*z + c3))).store(fMat + 12);
    return *this;
}

SkM44& SkM44::postTranslate(SkScalar x, SkScalar y, SkScalar z) {
    const skvx::float4 t = { x, y, z, 0 };
    (t * fMat[ 3] + skvx::float4::Load(fMat +  0)).store(fMat +  0);
    (t * fMat[ 7] + skvx::float4::Load(fMat +  4)).store(fMat +  4);
    (t * fMat[11] + skvx::float4::Load(fMat +  8)).store(fMat +  8);
    (t * fMat[15] + skvx::float4::Load(fMat + 12)).store(fMat + 12);
    return *this;
}

SkM44& SkM44::preScale(SkScalar x, SkScalar y) {
    const skvx::float4 c0 = skvx::float4::Load(fMat +  0);
    const skvx::float4 c1 = skvx::float4::Load(fMat +  4);

    (c0 * x).store(fMat + 0);
    (c1 * y).store(fMat + 4);
    return *this;
}

SkM44& SkM44::preScale(SkScalar x, SkScalar y, SkScalar z) {
    const skvx::float4 c0 = skvx::float4::Load(fMat +  0);
    const skvx::float4 c1 = skvx::float4::Load(fMat +  4);
    const skvx::float4 c2 = skvx::float4::Load(fMat +  8);

    (c0 * x).store(fMat + 0);
    (c1 * y).store(fMat + 4);
    (c2 * z).store(fMat + 8);
    return *this;
}

SkV4 SkM44::map(float x, float y, float z, float w) const {
    const skvx::float4 c0 = skvx::float4::Load(fMat +  0);
    const skvx::float4 c1 = skvx::float4::Load(fMat +  4);
    const skvx::float4 c2 = skvx::float4::Load(fMat +  8);
    const skvx::float4 c3 = skvx::float4::Load(fMat + 12);

    SkV4 v{};
    (c0*x + (c1*y + (c2*z + c3*w))).store(&v.x);
    return v;
}

static SkRect map_rect_affine(const SkRect& src, const float mat[16]) {
    // When multiplied against vectors of the form <x,y,x,y>, 'flip' allows a single min()
    // to compute both the min and "negated" max between the xy coordinates. Once finished, another
    // multiplication produces the original max.
    const skvx::float4 flip{1.f, 1.f, -1.f, -1.f};

    // Since z = 0 and it's assumed there's no perspective, only load the upper 2x2 and (tx,ty) in c3
    const skvx::float4 c0 = skvx::shuffle<0,1,0,1>(skvx::float2::Load(mat + 0)) * flip;
    const skvx::float4 c1 = skvx::shuffle<0,1,0,1>(skvx::float2::Load(mat + 4)) * flip;
    const skvx::float4 c3 = skvx::shuffle<0,1,0,1>(skvx::float2::Load(mat + 12));

    // Compute the min and max of the four transformed corners pre-translation; then translate once
    // at the end.
    const skvx::float4 minMax = c3 + flip * skvx::min(skvx::min(c0 * src.fLeft  + c1 * src.fTop,
                                                                c0 * src.fRight + c1 * src.fTop),
                                                      skvx::min(c0 * src.fLeft  + c1 * src.fBottom,
                                                                c0 * src.fRight + c1 * src.fBottom));

    // minMax holds (min x, min y, max x, max y) so can be copied into an SkRect expecting l,t,r,b
    SkRect r{};
    minMax.store(&r);
    return r;
}

// Clips the edge from p0 (behind the w = kW0PlaneDistance plane) to p at that plane and projects
// the crossing to 2D as (x, y, -x, -y). When p is behind the plane too, the edge has no visible
// part and the result is infinite, so a later min() ignores it.
static skvx::float4 clip_edge_to_w0_plane(const skvx::float4& p0, float w0, const skvx::float4& p,
                                          const skvx::float4& flip) {
    const float w = p[3];
    if (w >= SkPathPriv::kW0PlaneDistance) {
        const float t = (SkPathPriv::kW0PlaneDistance - w0) / (w - w0);
        const skvx::float2 c = (t * skvx::shuffle<0,1>(p) + (1.f - t) * skvx::shuffle<0,1>(p0)) /
                                     SkPathPriv::kW0PlaneDistance;

        return flip * skvx::shuffle<0,1,0,1>(c);
    } else {
        return skvx::float4(SK_ScalarInfinity);
    }
}

// Projects corner p0 to 2D as (x, y, -x, -y). A corner behind the w = kW0PlaneDistance plane is
// replaced by the clipped crossings of its two edges, to p1 and to p2.
static skvx::float4 project_corner(const skvx::float4& p0, const skvx::float4& p1,
                                   const skvx::float4& p2, const skvx::float4& flip) {
    const float w0 = p0[3];
    if (w0 >= SkPathPriv::kW0PlaneDistance) {
        // Unclipped, just divide by w
        return flip * skvx::shuffle<0,1,0,1>(p0) / w0;
    } else {
        // Clip both edges leaving p0, and return the min/max of the two clipped points
        // (since clipping returns infinity when both p0 and 2nd vertex have w<0, it'll
        // automatically be ignored).
        return skvx::min(clip_edge_to_w0_plane(p0, w0, p1, flip),
                         clip_edge_to_w0_plane(p0, w0, p2, flip));
    }
}

static SkRect map_rect_perspective(const SkRect& src, const float mat[16]) {
    // Like map_rect_affine, z = 0 so we can skip the 3rd column, but we do need to compute w's
    // for each corner of the src rect.
    const skvx::float4 c0 = skvx::float4::Load(mat + 0);
    const skvx::float4 c1 = skvx::float4::Load(mat + 4);
    const skvx::float4 c3 = skvx::float4::Load(mat + 12);

    // Unlike map_rect_affine, we do not defer the 4th column since we may need to homogeneous
    // coordinates to clip against the w=0 plane
    const skvx::float4 tl = c0 * src.fLeft  + c1 * src.fTop    + c3;
    const skvx::float4 tr = c0 * src.fRight + c1 * src.fTop    + c3;
    const skvx::float4 bl = c0 * src.fLeft  + c1 * src.fBottom + c3;
    const skvx::float4 br = c0 * src.fRight + c1 * src.fBottom + c3;

    // After clipping to w>0 and projecting to 2d, project_corner employs the same negation trick
    // to compute min and max at the same time.
    const skvx::float4 flip{1.f, 1.f, -1.f, -1.f};

    // Project all 4 corners, and pass in their adjacent vertices for clipping if it has w < 0,
    // then accumulate the min and max xy's.
    const skvx::float4 minMax = flip * skvx::min(skvx::min(project_corner(tl, tr, bl, flip),
                                                           project_corner(tr, br, tl, flip)),
                                                 skvx::min(project_corner(br, bl, tr, flip),
                                                           project_corner(bl, tl, br, flip)));

    SkRect r{};
    minMax.store(&r);
    return r;
}

SkRect SkMatrixPriv::MapRect(const SkM44& m, const SkRect& src) {
    const bool hasPerspective =
            m.fMat[3] != 0 || m.fMat[7] != 0 || m.fMat[11] != 0 || m.fMat[15] != 1;
    if (hasPerspective) {
        return map_rect_perspective(src, m.fMat);
    } else {
        return map_rect_affine(src, m.fMat);
    }
}

void SkM44::normalizePerspective() {
    // If the bottom row of the matrix is [0, 0, 0, not_one], we will treat the matrix as if it
    // is in perspective, even though it stills behaves like its affine. If we divide everything
    // by the not_one value, then it will behave the same, but will be treated as affine,
    // and therefore faster (e.g. clients can forward-difference calculations).
    //
    // Like SkMatrix, each entry is scaled in double precision and rounded once. Scaling in float
    // rounded the reciprocal and then the product, and for |X| below about 2.9e-39 the float
    // reciprocal overflowed, so zero entries became NaN. A scaled entry that does not fit in a
    // float leaves the matrix unchanged; it still maps points correctly as a perspective matrix.
    // So does a non-finite X: an infinite one made every finite entry zero, so a non-finite
    // matrix became a finite, degenerate one.
    if (fMat[15] != 1 && fMat[15] != 0 && SkIsFinite(fMat[15]) &&
        fMat[3] == 0 && fMat[7] == 0 && fMat[11] == 0) {
        const double inv = 1.0 / fMat[15];
        SkScalar scaled[16] = {};
        for (int i = 0; i < 15; ++i) {
            scaled[i] = static_cast<float>(fMat[i] * inv);
        }
        scaled[15] = 1.0f;
        if (SkIsFinite(scaled, 16)) {
            memcpy(fMat, scaled, sizeof(scaled));
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

/** We always perform the calculation in doubles, to avoid prematurely losing
    precision along the way. This relies on the compiler automatically
    promoting our SkScalar values to double (if needed).
 */
bool SkM44::invert(SkM44* inverse) const {
    // The inverse is computed even without storage, so that invertibility always means a finite
    // inverse; scratch storage keeps a refused inverse out of the caller's matrix.
    SkScalar tmp[16] = {};
    if (!SkInvert4x4Matrix(fMat, tmp)) {
        return false;
    }
    if (inverse != nullptr) {
        memcpy((*inverse).fMat, tmp, sizeof(tmp));
    }
    return true;
}

SkM44& SkM44::setRotateUnitSinCos(SkV3 axis, SkScalar sinAngle, SkScalar cosAngle) {
    // Taken from "Essential Mathematics for Games and Interactive Applications"
    //             James M. Van Verth and Lars M. Bishop -- third edition
    const SkScalar x = axis.x;
    const SkScalar y = axis.y;
    const SkScalar z = axis.z;
    const SkScalar c = cosAngle;
    const SkScalar s = sinAngle;
    const SkScalar t = 1 - c;

    *this = { t*x*x + c,   t*x*y - s*z, t*x*z + s*y, 0,
              t*x*y + s*z, t*y*y + c,   t*y*z - s*x, 0,
              t*x*z - s*y, t*y*z + s*x, t*z*z + c,   0,
              0,           0,           0,           1 };
    return *this;
}

SkM44& SkM44::setRotate(SkV3 axis, SkScalar radians) {
    if (!SkIsFinite(axis.x, axis.y, axis.z)) {
        return setIdentity();
    }
    // When the squared length is a normal float, its square root and reciprocal are accurate.
    // Otherwise it overflowed, underflowed to zero, or lost precision as a denormal; dividing by
    // the largest component first puts the length between 1 and sqrt(3).
    const SkScalar lengthSquared = axis.lengthSquared();
    if (lengthSquared >= std::numeric_limits<float>::min() && SkIsFinite(lengthSquared)) {
        const SkScalar len = SkScalarSqrt(lengthSquared);
        return setRotateUnit(axis * (SK_Scalar1 / len), radians);
    }
    const SkScalar largest = std::max(std::fabs(axis.x), std::max(std::fabs(axis.y),
                                                                  std::fabs(axis.z)));
    if (largest == 0) {
        return setIdentity();
    }
    const SkV3 scaled{axis.x / largest, axis.y / largest, axis.z / largest};
    const SkScalar scaledLength = scaled.length();
    return setRotateUnit(scaled * (SK_Scalar1 / scaledLength), radians);
}
