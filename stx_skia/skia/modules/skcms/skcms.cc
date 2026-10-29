/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


// stx_skia keeps only the transfer-function and gamut arithmetic SkColorSpace and the raster
// pipeline use. The ICC parser, profile comparison, curve fitting and skcms_Transform are
// removed (owner decision, 2026-10-28; see docs/CONVERSION.md).

#include "src/skcms_public.h"  // NO_G3_REWRITE
#include "src/skcms_internals.h"  // NO_G3_REWRITE
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#if defined(__clang__) || defined(__GNUC__)
    static constexpr float INFINITY_ = __builtin_inff();
#else
    static const union {
        uint32_t bits;
        float    f;
    } inf_ = { 0x7f800000 };
    #define INFINITY_ inf_.f
#endif

static float log2f_(float x) {
    // The first approximation of log2(x) is its exponent 'e', minus 127.
    int32_t bits;
    memcpy(&bits, &x, sizeof(bits));

    float e = (float)bits * (1.0f / (1<<23));

    // If we use the mantissa too we can refine the error signficantly.
    int32_t m_bits = (bits & 0x007fffff) | 0x3f000000;
    float m;
    memcpy(&m, &m_bits, sizeof(m));

    return (e - 124.225514990f
              -   1.498030302f*m
              -   1.725879990f/(0.3520887068f + m));
}
static float logf_(float x) {
    const float ln2 = 0.69314718f;
    return ln2*log2f_(x);
}

static float exp2f_(float x) {
    if (x > 128.0f) {
        return INFINITY_;
    } else if (x < -127.0f) {
        return 0.0f;
    }
    float fract = x - floorf_(x);

    float fbits = (1.0f * (1<<23)) * (x + 121.274057500f
                                        -   1.490129070f*fract
                                        +  27.728023300f/(4.84252568f - fract));

    // Before we cast fbits to int32_t, check for out of range values to pacify UBSAN.
    // INT_MAX is not exactly representable as a float, so exclude it as effectively infinite.
    // Negative values are effectively underflow - we'll end up returning a (different) negative
    // value, which makes no sense. So clamp to zero.
    if (fbits >= (float)INT_MAX) {
        return INFINITY_;
    } else if (fbits < 0) {
        return 0;
    }

    int32_t bits = (int32_t)fbits;
    memcpy(&x, &bits, sizeof(x));
    return x;
}

// Not static, as it's used by some test tools.
float powf_(float x, float y) {
    if (x <= 0.f) {
        return 0.f;
    }
    if (x == 1.f) {
        return 1.f;
    }
    return exp2f_(log2f_(x) * y);
}

static float expf_(float x) {
    const float log2_e = 1.4426950408889634074f;
    return exp2f_(log2_e * x);
}

static bool isfinitef_(float x) { return 0 == x*0; }

// Most transfer functions we work with are sRGBish.
// For exotic HDR transfer functions, we encode them using a tf.g that makes no sense,
// and repurpose the other fields to hold the parameters of the HDR functions.
struct TF_PQish  { float A,B,C,D,E,F; };
struct TF_HLGish { float R,G,a,b,c,K_minus_1; };
// We didn't originally support a scale factor K for HLG, and instead just stored 0 in
// the unused `f` field of skcms_TransferFunction for HLGish and HLGInvish transfer functions.
// By storing f=K-1, those old unusued f=0 values now mean K=1, a noop scale factor.

static float TFKind_marker(skcms_TFType kind) {
    // We'd use different NaNs, but those aren't guaranteed to be preserved by WASM.
    return -(float)kind;
}

static skcms_TFType classify(const skcms_TransferFunction& tf, TF_PQish*   pq = nullptr
                                                             , TF_HLGish* hlg = nullptr) {
    if (tf.g < 0) {
        // Negative "g" is mapped to enum values; large negative are for sure invalid.
        if (tf.g < -128) {
            return skcms_TFType_Invalid;
        }
        int enum_g = -static_cast<int>(tf.g);
        // Non-whole "g" values are invalid as well.
        if (static_cast<float>(-enum_g) != tf.g) {
            return skcms_TFType_Invalid;
        }
        // TODO: soundness checks for PQ/HLG like we do for sRGBish?
        switch (enum_g) {
            case skcms_TFType_PQish:
                if (pq) {
                    memcpy(pq , &tf.a, sizeof(*pq ));
                }
                return skcms_TFType_PQish;
            case skcms_TFType_HLGish:
                if (hlg) {
                    memcpy(hlg, &tf.a, sizeof(*hlg));
                }
                return skcms_TFType_HLGish;
            case skcms_TFType_HLGinvish:
                if (hlg) {
                    memcpy(hlg, &tf.a, sizeof(*hlg));
                }
                return skcms_TFType_HLGinvish;
            case skcms_TFType_PQ:
                if (tf.b != 0.f || tf.c != 0.f || tf.d != 0.f || tf.e != 0.f || tf.f != 0.f) {
                    return skcms_TFType_Invalid;
                }
                return skcms_TFType_PQ;
            case skcms_TFType_HLG:
                if (tf.d != 0.f || tf.e != 0.f || tf.f != 0.f) {
                    return skcms_TFType_Invalid;
                }
                return skcms_TFType_HLG;
        }
        return skcms_TFType_Invalid;
    }

    // Basic soundness checks for sRGBish transfer functions.
    if (isfinitef_(tf.a + tf.b + tf.c + tf.d + tf.e + tf.f + tf.g)
            // a,c,d,g should be non-negative to make any sense.
            && tf.a >= 0
            && tf.c >= 0
            && tf.d >= 0
            && tf.g >= 0
            // Raising a negative value to a fractional (*tf).g produces complex numbers.
            && tf.a * tf.d + tf.b >= 0) {
        return skcms_TFType_sRGBish;
    }

    return skcms_TFType_Invalid;
}

skcms_TFType skcms_TransferFunction_getType(const skcms_TransferFunction* tf) {
    return classify(*tf);
}
bool skcms_TransferFunction_isSRGBish(const skcms_TransferFunction* tf) {
    return classify(*tf) == skcms_TFType_sRGBish;
}
bool skcms_TransferFunction_isPQish(const skcms_TransferFunction* tf) {
    return classify(*tf) == skcms_TFType_PQish;
}
bool skcms_TransferFunction_isHLGish(const skcms_TransferFunction* tf) {
    return classify(*tf) == skcms_TFType_HLGish;
}
bool skcms_TransferFunction_isPQ(const skcms_TransferFunction* tf) {
    return classify(*tf) == skcms_TFType_PQ;
}
bool skcms_TransferFunction_isHLG(const skcms_TransferFunction* tf) {
    return classify(*tf) == skcms_TFType_HLG;
}

bool skcms_TransferFunction_makePQish(skcms_TransferFunction* tf,
                                      float A, float B, float C,
                                      float D, float E, float F) {
    *tf = { TFKind_marker(skcms_TFType_PQish), A,B,C,D,E,F };
    assert(skcms_TransferFunction_isPQish(tf));
    return true;
}

bool skcms_TransferFunction_makeScaledHLGish(skcms_TransferFunction* tf,
                                             float K, float R, float G,
                                             float a, float b, float c) {
    *tf = { TFKind_marker(skcms_TFType_HLGish), R,G, a,b,c, K-1.0f };
    assert(skcms_TransferFunction_isHLGish(tf));
    return true;
}

void skcms_TransferFunction_makePQ(
    skcms_TransferFunction* tf,
    float hdr_reference_white_luminance) {
    *tf = { TFKind_marker(skcms_TFType_PQ),
            hdr_reference_white_luminance,
            0.f,0.f,0.f,0.f,0.f };
    assert(skcms_TransferFunction_isPQ(tf));
}

void skcms_TransferFunction_makeHLG(
    skcms_TransferFunction* tf,
    float hdr_reference_white_luminance,
    float peak_luminance,
    float system_gamma) {
    *tf = { TFKind_marker(skcms_TFType_HLG),
            hdr_reference_white_luminance,
            peak_luminance,
            system_gamma,
            0.f, 0.f, 0.f };
    assert(skcms_TransferFunction_isHLG(tf));
}

float skcms_TransferFunction_eval(const skcms_TransferFunction* tf, float x) {
    float sign = x < 0 ? -1.0f : 1.0f;
    x *= sign;

    TF_PQish  pq;
    TF_HLGish hlg;
    switch (classify(*tf, &pq, &hlg)) {
        case skcms_TFType_Invalid: break;

        case skcms_TFType_HLG: {
            const float a = 0.17883277f;
            const float b = 0.28466892f;
            const float c = 0.55991073f;
            return sign * (x <= 0.5f ? x*x/3.f : (expf_((x-c)/a) + b) / 12.f);
        }

        case skcms_TFType_HLGish: {
            const float K = hlg.K_minus_1 + 1.0f;
            return K * sign * (x*hlg.R <= 1 ? powf_(x*hlg.R, hlg.G)
                                            : expf_((x-hlg.c)*hlg.a) + hlg.b);
        }

        // skcms_TransferFunction_invert() inverts R, G, and a for HLGinvish so this math is fast.
        case skcms_TFType_HLGinvish: {
            const float K = hlg.K_minus_1 + 1.0f;
            x /= K;
            return sign * (x <= 1 ? hlg.R * powf_(x, hlg.G)
                                  : hlg.a * logf_(x - hlg.b) + hlg.c);
        }

        case skcms_TFType_sRGBish:
            return sign * (x < (*tf).d ?       (*tf).c * x + (*tf).f
                                     : powf_((*tf).a * x + (*tf).b, (*tf).g) + (*tf).e);

        case skcms_TFType_PQ: {
            const float c1 =  107 / 128.f;
            const float c2 = 2413 / 128.f;
            const float c3 = 2392 / 128.f;
            const float m1 = 1305 / 8192.f;
            const float m2 = 2523 / 32.f;
            const float p = powf_(x, 1.f / m2);
            return powf_((p - c1) / (c2 - c3 * p), 1.f / m1);
        }

        case skcms_TFType_PQish:
            return sign *
                   powf_((pq.A + pq.B * powf_(x, pq.C)) / (pq.D + pq.E * powf_(x, pq.C)), pq.F);
    }
    return 0;
}

// sRGB's canonical transfer function, as skcms's sRGB profile held it.
const skcms_TransferFunction* skcms_sRGB_TransferFunction() {
    static const skcms_TransferFunction sRGB =
        {2.4f, (float)(1/1.055), (float)(0.055/1.055), (float)(1/12.92), 0.04045f, 0, 0};
    return &sRGB;
}

const skcms_TransferFunction* skcms_sRGB_Inverse_TransferFunction() {
    static const skcms_TransferFunction sRGB_inv =
        {0.416666657f, 1.137283325f, -0.0f, 12.920000076f, 0.003130805f, -0.054969788f, -0.0f};
    return &sRGB_inv;
}

const skcms_TransferFunction* skcms_Identity_TransferFunction() {
    static const skcms_TransferFunction identity = {1,1,0,0,0,0,0};
    return &identity;
}

static bool is_zero_to_one(float x) {
    return 0 <= x && x <= 1;
}

typedef struct { float vals[3]; } skcms_Vector3;

static skcms_Vector3 mv_mul(const skcms_Matrix3x3* m, const skcms_Vector3* v) {
    skcms_Vector3 dst = {{0,0,0}};
    for (int row = 0; row < 3; ++row) {
        dst.vals[row] = (*m).vals[row][0] * (*v).vals[0]
                      + (*m).vals[row][1] * (*v).vals[1]
                      + (*m).vals[row][2] * (*v).vals[2];
    }
    return dst;
}

bool skcms_AdaptToXYZD50(float wx, float wy,
                         skcms_Matrix3x3* toXYZD50) {
    if (!is_zero_to_one(wx) || !is_zero_to_one(wy) ||
        !toXYZD50) {
        return false;
    }

    // Assumes that Y is 1.0f.
    skcms_Vector3 wXYZ = { { wx / wy, 1, (1 - wx - wy) / wy } };

    // Now convert toXYZ matrix to toXYZD50.
    skcms_Vector3 wXYZD50 = { { 0.96422f, 1.0f, 0.82521f } };

    // Calculate the chromatic adaptation matrix.  We will use the Bradford method, thus
    // the matrices below.  The Bradford method is used by Adobe and is widely considered
    // to be the best.
    skcms_Matrix3x3 xyz_to_lms = {{
        {  0.8951f,  0.2664f, -0.1614f },
        { -0.7502f,  1.7135f,  0.0367f },
        {  0.0389f, -0.0685f,  1.0296f },
    }};
    skcms_Matrix3x3 lms_to_xyz = {{
        {  0.9869929f, -0.1470543f, 0.1599627f },
        {  0.4323053f,  0.5183603f, 0.0492912f },
        { -0.0085287f,  0.0400428f, 0.9684867f },
    }};

    skcms_Vector3 srcCone = mv_mul(&xyz_to_lms, &wXYZ);
    skcms_Vector3 dstCone = mv_mul(&xyz_to_lms, &wXYZD50);

    *toXYZD50 = {{
        { dstCone.vals[0] / srcCone.vals[0], 0, 0 },
        { 0, dstCone.vals[1] / srcCone.vals[1], 0 },
        { 0, 0, dstCone.vals[2] / srcCone.vals[2] },
    }};
    *toXYZD50 = skcms_Matrix3x3_concat(toXYZD50, &xyz_to_lms);
    *toXYZD50 = skcms_Matrix3x3_concat(&lms_to_xyz, toXYZD50);

    return true;
}

bool skcms_PrimariesToXYZD50(float rx, float ry,
                             float gx, float gy,
                             float bx, float by,
                             float wx, float wy,
                             skcms_Matrix3x3* toXYZD50) {
    if (!is_zero_to_one(rx) || !is_zero_to_one(ry) ||
        !is_zero_to_one(gx) || !is_zero_to_one(gy) ||
        !is_zero_to_one(bx) || !is_zero_to_one(by) ||
        !is_zero_to_one(wx) || !is_zero_to_one(wy) ||
        !toXYZD50) {
        return false;
    }

    // First, we need to convert xy values (primaries) to XYZ.
    skcms_Matrix3x3 primaries = {{
        { rx, gx, bx },
        { ry, gy, by },
        { 1 - rx - ry, 1 - gx - gy, 1 - bx - by },
    }};
    skcms_Matrix3x3 primaries_inv;
    if (!skcms_Matrix3x3_invert(&primaries, &primaries_inv)) {
        return false;
    }

    // Assumes that Y is 1.0f.
    skcms_Vector3 wXYZ = { { wx / wy, 1, (1 - wx - wy) / wy } };
    skcms_Vector3 XYZ = mv_mul(&primaries_inv, &wXYZ);

    skcms_Matrix3x3 toXYZ = {{
        { XYZ.vals[0],           0,           0 },
        {           0, XYZ.vals[1],           0 },
        {           0,           0, XYZ.vals[2] },
    }};
    toXYZ = skcms_Matrix3x3_concat(&primaries, &toXYZ);

    skcms_Matrix3x3 DXtoD50;
    if (!skcms_AdaptToXYZD50(wx, wy, &DXtoD50)) {
        return false;
    }

    *toXYZD50 = skcms_Matrix3x3_concat(&DXtoD50, &toXYZ);
    return true;
}


bool skcms_Matrix3x3_invert(const skcms_Matrix3x3* src, skcms_Matrix3x3* dst) {
    double a00 = (*src).vals[0][0],
           a01 = (*src).vals[1][0],
           a02 = (*src).vals[2][0],
           a10 = (*src).vals[0][1],
           a11 = (*src).vals[1][1],
           a12 = (*src).vals[2][1],
           a20 = (*src).vals[0][2],
           a21 = (*src).vals[1][2],
           a22 = (*src).vals[2][2];

    double b0 = a00*a11 - a01*a10,
           b1 = a00*a12 - a02*a10,
           b2 = a01*a12 - a02*a11,
           b3 = a20,
           b4 = a21,
           b5 = a22;

    double determinant = b0*b5
                       - b1*b4
                       + b2*b3;

    if (determinant == 0) {
        return false;
    }

    double invdet = 1.0 / determinant;
    if (invdet > +FLT_MAX || invdet < -FLT_MAX || !isfinitef_((float)invdet)) {
        return false;
    }

    b0 *= invdet;
    b1 *= invdet;
    b2 *= invdet;
    b3 *= invdet;
    b4 *= invdet;
    b5 *= invdet;

    (*dst).vals[0][0] = (float)( a11*b5 - a12*b4 );
    (*dst).vals[1][0] = (float)( a02*b4 - a01*b5 );
    (*dst).vals[2][0] = (float)(        +     b2 );
    (*dst).vals[0][1] = (float)( a12*b3 - a10*b5 );
    (*dst).vals[1][1] = (float)( a00*b5 - a02*b3 );
    (*dst).vals[2][1] = (float)(        -     b1 );
    (*dst).vals[0][2] = (float)( a10*b4 - a11*b3 );
    (*dst).vals[1][2] = (float)( a01*b3 - a00*b4 );
    (*dst).vals[2][2] = (float)(        +     b0 );

    for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) {
        if (!isfinitef_((*dst).vals[r][c])) {
            return false;
        }
    }
    return true;
}

skcms_Matrix3x3 skcms_Matrix3x3_concat(const skcms_Matrix3x3* A, const skcms_Matrix3x3* B) {
    skcms_Matrix3x3 m = { { { 0,0,0 },{ 0,0,0 },{ 0,0,0 } } };
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            m.vals[r][c] = (*A).vals[r][0] * (*B).vals[0][c]
                         + (*A).vals[r][1] * (*B).vals[1][c]
                         + (*A).vals[r][2] * (*B).vals[2][c];
        }
    return m;
}

#if defined(__clang__)
    [[clang::no_sanitize("float-divide-by-zero")]]  // Checked for by classify() on the way out.
#endif
bool skcms_TransferFunction_invert(const skcms_TransferFunction* src, skcms_TransferFunction* dst) {
    TF_PQish  pq;
    TF_HLGish hlg;
    switch (classify(*src, &pq, &hlg)) {
        case skcms_TFType_Invalid: return false;
        case skcms_TFType_PQ:      return false;
        case skcms_TFType_HLG:     return false;
        case skcms_TFType_sRGBish: break;  // handled below

        case skcms_TFType_PQish:
            *dst = { TFKind_marker(skcms_TFType_PQish), -pq.A,  pq.D, 1.0f/pq.F
                                                      ,  pq.B, -pq.E, 1.0f/pq.C};
            return true;

        case skcms_TFType_HLGish:
            *dst = { TFKind_marker(skcms_TFType_HLGinvish), 1.0f/hlg.R, 1.0f/hlg.G
                                                          , 1.0f/hlg.a, hlg.b, hlg.c
                                                          , hlg.K_minus_1 };
            return true;

        case skcms_TFType_HLGinvish:
            *dst = { TFKind_marker(skcms_TFType_HLGish), 1.0f/hlg.R, 1.0f/hlg.G
                                                       , 1.0f/hlg.a, hlg.b, hlg.c
                                                       , hlg.K_minus_1 };
            return true;
    }

    assert (classify(*src) == skcms_TFType_sRGBish);

    // We're inverting this function, solving for x in terms of y.
    //   y = (cx + f)         x < d
    //       (ax + b)^g + e   x ≥ d
    // The inverse of this function can be expressed in the same piecewise form.
    skcms_TransferFunction inv = {0,0,0,0,0,0,0};

    // We'll start by finding the new threshold inv.d.
    // In principle we should be able to find that by solving for y at x=d from either side.
    // (If those two d values aren't the same, it's a discontinuous transfer function.)
    float d_l =       (*src).c * (*src).d + (*src).f,
          d_r = powf_((*src).a * (*src).d + (*src).b, (*src).g) + (*src).e;
    if (fabsf_(d_l - d_r) > 1/512.0f) {
        return false;
    }
    inv.d = d_l;  // TODO(mtklein): better in practice to choose d_r?

    // When d=0, the linear section collapses to a point.  We leave c,d,f all zero in that case.
    if (inv.d > 0) {
        // Inverting the linear section is pretty straightfoward:
        //        y       = cx + f
        //        y - f   = cx
        //   (1/c)y - f/c = x
        inv.c =    1.0f/(*src).c;
        inv.f = -(*src).f/(*src).c;
    }

    // The interesting part is inverting the nonlinear section:
    //         y                = (ax + b)^g + e.
    //         y - e            = (ax + b)^g
    //        (y - e)^1/g       =  ax + b
    //        (y - e)^1/g - b   =  ax
    //   (1/a)(y - e)^1/g - b/a =   x
    //
    // To make that fit our form, we need to move the (1/a) term inside the exponentiation:
    //   let k = (1/a)^g
    //   (1/a)( y -  e)^1/g - b/a = x
    //        (ky - ke)^1/g - b/a = x

    float k = powf_((*src).a, -(*src).g);  // (1/a)^g == a^-g
    inv.g = 1.0f / (*src).g;
    inv.a = k;
    inv.b = -k * (*src).e;
    inv.e = -(*src).b / (*src).a;

    // We need to enforce the same constraints here that we do when fitting a curve,
    // a >= 0 and ad+b >= 0.  These constraints are checked by classify(), so they're true
    // of the source function if we're here.

    // Just like when fitting the curve, there's really no way to rescue a < 0.
    if (inv.a < 0) {
        return false;
    }
    // On the other hand we can rescue an ad+b that's gone slightly negative here.
    if (inv.a * inv.d + inv.b < 0) {
        inv.b = -inv.a * inv.d;
    }

    // That should usually make classify(inv) == sRGBish true, but there are a couple situations
    // where we might still fail here, like non-finite parameter values.
    if (classify(inv) != skcms_TFType_sRGBish) {
        return false;
    }

    assert (inv.a >= 0);
    assert (inv.a * inv.d + inv.b >= 0);

    // Now in principle we're done.
    // But to preserve the valuable invariant inv(src(1.0f)) == 1.0f, we'll tweak
    // e or f of the inverse, depending on which segment contains src(1.0f).
    float s = skcms_TransferFunction_eval(src, 1.0f);
    if (!isfinitef_(s)) {
        return false;
    }

    float sign = s < 0 ? -1.0f : 1.0f;
    s *= sign;
    if (s < inv.d) {
        inv.f = 1.0f - sign * inv.c * s;
    } else {
        inv.e = 1.0f - sign * powf_(inv.a * s + inv.b, inv.g);
    }

    *dst = inv;
    return classify(*dst) == skcms_TFType_sRGBish;
}
