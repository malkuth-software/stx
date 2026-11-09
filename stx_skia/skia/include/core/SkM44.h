/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkM44_DEFINED
#define SkM44_DEFINED

#include "include/core/SkMatrix.h"
#include "include/core/SkScalar.h"
#include "include/core/SkTypes.h"

#include <cstring>

struct SkRect;

struct SK_API SkV3 {
    float x = 0;
    float y = 0;
    float z = 0;

    bool operator==(const SkV3& v) const {
        return x == v.x && y == v.y && z == v.z;
    }
    bool operator!=(const SkV3& v) const { return !(*this == v); }

    static SkScalar Dot(const SkV3& a, const SkV3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
    static SkV3   Cross(const SkV3& a, const SkV3& b) {
        return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
    }
    static SkV3 Normalize(const SkV3& v) { return v * (1.0f / v.length()); }

    SkV3 operator-() const { return {-x, -y, -z}; }
    SkV3 operator+(const SkV3& v) const { return { x + v.x, y + v.y, z + v.z }; }
    SkV3 operator-(const SkV3& v) const { return { x - v.x, y - v.y, z - v.z }; }

    SkV3 operator*(const SkV3& v) const {
        return { x*v.x, y*v.y, z*v.z };
    }
    friend SkV3 operator*(const SkV3& v, SkScalar s) {
        return { v.x*s, v.y*s, v.z*s };
    }
    friend SkV3 operator*(SkScalar s, const SkV3& v) { return v*s; }

    void operator+=(SkV3 v) { *this = *this + v; }
    void operator-=(SkV3 v) { *this = *this - v; }
    void operator*=(SkV3 v) { *this = *this * v; }
    void operator*=(SkScalar s) { *this = *this * s; }

    SkScalar lengthSquared() const { return Dot(*this, *this); }
    SkScalar length() const { return SkScalarSqrt(Dot(*this, *this)); }

    SkScalar dot(const SkV3& v) const { return Dot(*this, v); }
    SkV3   cross(const SkV3& v) const { return Cross(*this, v); }
    SkV3 normalize()            const { return Normalize(*this); }

    const float* ptr() const { return &x; }
    float* ptr() { return &x; }
};

struct SK_API SkV4 {
    float x = 0;
    float y = 0;
    float z = 0;
    float w = 0;

    bool operator==(const SkV4& v) const {
        return x == v.x && y == v.y && z == v.z && w == v.w;
    }
    bool operator!=(const SkV4& v) const { return !(*this == v); }

    static SkScalar Dot(const SkV4& a, const SkV4& b) {
        return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    }
    static SkV4 Normalize(const SkV4& v) { return v * (1.0f / v.length()); }

    SkV4 operator-() const { return {-x, -y, -z, -w}; }
    SkV4 operator+(const SkV4& v) const { return { x + v.x, y + v.y, z + v.z, w + v.w }; }
    SkV4 operator-(const SkV4& v) const { return { x - v.x, y - v.y, z - v.z, w - v.w }; }

    SkV4 operator*(const SkV4& v) const {
        return { x*v.x, y*v.y, z*v.z, w*v.w };
    }
    friend SkV4 operator*(const SkV4& v, SkScalar s) {
        return { v.x*s, v.y*s, v.z*s, v.w*s };
    }
    friend SkV4 operator*(SkScalar s, const SkV4& v) { return v*s; }

    SkScalar lengthSquared() const { return Dot(*this, *this); }
    SkScalar length() const { return SkScalarSqrt(Dot(*this, *this)); }

    SkScalar dot(const SkV4& v) const { return Dot(*this, v); }
    SkV4 normalize()            const { return Normalize(*this); }

    const float* ptr() const { return &x; }
    float* ptr() { return &x; }

    // Indices outside 0 to 3 stop the program in every build.
    float operator[](int i) const {
        SkASSERT_RELEASE(i >= 0 && i < 4);
        switch (i) {
            case 0: return x;
            case 1: return y;
            case 2: return z;
            default: return w;
        }
    }
    float& operator[](int i) {
        SkASSERT_RELEASE(i >= 0 && i < 4);
        switch (i) {
            case 0: return x;
            case 1: return y;
            case 2: return z;
            default: return w;
        }
    }
};

/**
 *  4x4 matrix used by SkCanvas and other parts of Skia.
 *
 *  Skia assumes a right-handed coordinate system:
 *      +X goes to the right
 *      +Y goes down
 *      +Z goes into the screen (away from the viewer)
 */
class SK_API SkM44 {
public:
    SkM44(const SkM44& src) = default;
    SkM44& operator=(const SkM44& src) = default;

    constexpr SkM44() = default;

    SkM44(const SkM44& a, const SkM44& b) {
        setConcat(a, b);
    }

    /**
     *  The constructor parameters are in row-major order.
     */
    constexpr SkM44(SkScalar m0, SkScalar m4, SkScalar m8,  SkScalar m12,
                    SkScalar m1, SkScalar m5, SkScalar m9,  SkScalar m13,
                    SkScalar m2, SkScalar m6, SkScalar m10, SkScalar m14,
                    SkScalar m3, SkScalar m7, SkScalar m11, SkScalar m15)
        // fMat is column-major order in memory.
        : fMat{m0,  m1,  m2,  m3,
               m4,  m5,  m6,  m7,
               m8,  m9,  m10, m11,
               m12, m13, m14, m15}
    {}

    static SkM44 ColMajor(const SkScalar c[16]) {
        return SkM44(c[0], c[4], c[ 8], c[12],
                     c[1], c[5], c[ 9], c[13],
                     c[2], c[6], c[10], c[14],
                     c[3], c[7], c[11], c[15]);
    }

    static SkM44 Translate(SkScalar x, SkScalar y, SkScalar z = 0) {
        return SkM44(1, 0, 0, x,
                     0, 1, 0, y,
                     0, 0, 1, z,
                     0, 0, 0, 1);
    }

    static SkM44 Scale(SkScalar x, SkScalar y, SkScalar z = 1) {
        return SkM44(x, 0, 0, 0,
                     0, y, 0, 0,
                     0, 0, z, 0,
                     0, 0, 0, 1);
    }

    static SkM44 Rotate(SkV3 axis, SkScalar radians) {
        SkM44 m;
        m.setRotate(axis, radians);
        return m;
    }

    bool operator==(const SkM44& other) const;
    bool operator!=(const SkM44& other) const {
        return !(other == *this);
    }

    void getColMajor(SkScalar v[]) const {
        memcpy(v, fMat, sizeof(fMat));
    }

    // Rows and columns outside 0 to 3 stop the program in every build.
    SkScalar rc(int r, int c) const {
        SkASSERT_RELEASE(r >= 0 && r <= 3);
        SkASSERT_RELEASE(c >= 0 && c <= 3);
        return fMat[c*4 + r];
    }
    void setRC(int r, int c, SkScalar value) {
        SkASSERT_RELEASE(r >= 0 && r <= 3);
        SkASSERT_RELEASE(c >= 0 && c <= 3);
        fMat[c*4 + r] = value;
    }

    SkM44& setIdentity() {
        *this = { 1, 0, 0, 0,
                  0, 1, 0, 0,
                  0, 0, 1, 0,
                  0, 0, 0, 1 };
        return *this;
    }

    SkM44& setTranslate(SkScalar x, SkScalar y, SkScalar z = 0) {
        *this = { 1, 0, 0, x,
                  0, 1, 0, y,
                  0, 0, 1, z,
                  0, 0, 0, 1 };
        return *this;
    }

    SkM44& setScale(SkScalar x, SkScalar y, SkScalar z = 1) {
        *this = { x, 0, 0, 0,
                  0, y, 0, 0,
                  0, 0, z, 0,
                  0, 0, 0, 1 };
        return *this;
    }

    /**
     *  Set this matrix to rotate about the specified unit-length axis vector,
     *  by an angle specified by its sin() and cos().
     *
     *  This does not attempt to verify that axis.length() == 1 or that the sin,cos values
     *  are correct.
     */
    SkM44& setRotateUnitSinCos(SkV3 axis, SkScalar sinAngle, SkScalar cosAngle);

    /**
     *  Set this matrix to rotate about the specified unit-length axis vector,
     *  by an angle specified in radians.
     *
     *  This does not attempt to verify that axis.length() == 1.
     */
    SkM44& setRotateUnit(SkV3 axis, SkScalar radians) {
        return setRotateUnitSinCos(axis, SkScalarSin(radians), SkScalarCos(radians));
    }

    /**
     *  Set this matrix to rotate about the specified axis vector,
     *  by an angle specified in radians.
     *
     *  Note: axis is not assumed to be unit-length, so it will be normalized internally,
     *        whatever its finite, nonzero length. A zero or non-finite axis sets the identity.
     *        If axis is already unit-length, call setRotateUnit() instead.
     */
    SkM44& setRotate(SkV3 axis, SkScalar radians);

    SkM44& setConcat(const SkM44& a, const SkM44& b);

    friend SkM44 operator*(const SkM44& a, const SkM44& b) {
        return SkM44(a, b);
    }

    SkM44& preConcat(const SkM44& m) {
        return setConcat(*this, m);
    }

    SkM44& postConcat(const SkM44& m) {
        return setConcat(m, *this);
    }

    /**
     *  A matrix is categorized as 'perspective' if the bottom row is not [0, 0, 0, 1].
     *  For most uses, a bottom row of [0, 0, 0, X] behaves like a non-perspective matrix, though
     *  it will be categorized as perspective. Calling normalizePerspective() will change the
     *  matrix such that, if its bottom row was [0, 0, 0, X], it will be changed to [0, 0, 0, 1]
     *  by scaling the rest of the matrix by 1/X.
     *
     *  | A B C D |    | A/X B/X C/X D/X |
     *  | E F G H | -> | E/X F/X G/X H/X |   for X != 0
     *  | I J K L |    | I/X J/X K/X L/X |
     *  | 0 0 0 X |    |  0   0   0   1  |
     *
     *  Each entry is scaled by 1/X in double precision and rounded once, as
     *  SkMatrix::normalizePerspective() does. If X or any scaled entry is not finite, the matrix
     *  is left unchanged.
     */
    void normalizePerspective();

    /** Returns true if all elements of the matrix are finite. Returns false if any
        element is infinity, or NaN.

        @return  true if matrix has only finite elements
    */
    bool isFinite() const { return SkIsFinite(fMat, 16); }

    /** If this is invertible, return that in inverse and return true. If it is
     *  not invertible, return false and leave the inverse parameter unchanged.
     *  inverse may be null to test invertibility only.
     */
    [[nodiscard]] bool invert(SkM44* inverse) const;

    ////////////

    SkV4 map(float x, float y, float z, float w) const;
    SkV4 operator*(const SkV4& v) const {
        return map(v.x, v.y, v.z, v.w);
    }
    SkV3 operator*(SkV3 v) const {
        const SkV4 mapped = map(v.x, v.y, v.z, 0);
        return {mapped.x, mapped.y, mapped.z};
    }
    ////////////////////// Converting to/from SkMatrix

    /* When converting from SkM44 to SkMatrix, the third row and
     * column is dropped.  When converting from SkMatrix to SkM44
     * the third row and column remain as identity:
     * [ a b c ]      [ a b 0 c ]
     * [ d e f ]  ->  [ d e 0 f ]
     * [ g h i ]      [ 0 0 1 0 ]
     *                [ g h 0 i ]
     */
    SkMatrix asM33() const {
        return SkMatrix::MakeAll(fMat[0], fMat[4], fMat[12],
                                 fMat[1], fMat[5], fMat[13],
                                 fMat[3], fMat[7], fMat[15]);
    }

    explicit SkM44(const SkMatrix& src)
    : SkM44(src[SkMatrix::kMScaleX], src[SkMatrix::kMSkewX],  0, src[SkMatrix::kMTransX],
            src[SkMatrix::kMSkewY],  src[SkMatrix::kMScaleY], 0, src[SkMatrix::kMTransY],
            0,                       0,                       1, 0,
            src[SkMatrix::kMPersp0], src[SkMatrix::kMPersp1], 0, src[SkMatrix::kMPersp2])
    {}

    SkM44& preTranslate(SkScalar x, SkScalar y, SkScalar z = 0);
    SkM44& postTranslate(SkScalar x, SkScalar y, SkScalar z = 0);

    SkM44& preScale(SkScalar x, SkScalar y);
    SkM44& preScale(SkScalar x, SkScalar y, SkScalar z);
    SkM44& preConcat(const SkMatrix&);

private:
    /* Stored in column-major.
     *  Indices
     *  0  4  8  12        1 0 0 trans_x
     *  1  5  9  13  e.g.  0 1 0 trans_y
     *  2  6 10  14        0 0 1 trans_z
     *  3  7 11  15        0 0 0 1
     */
    SkScalar fMat[16] = {1, 0, 0, 0,
                         0, 1, 0, 0,
                         0, 0, 1, 0,
                         0, 0, 0, 1};

    friend class SkMatrixPriv;
};

#endif
