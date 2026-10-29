/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

// skcms_public.h contains the entire public API for skcms.

// Modeled after the FOX example in https://gcc.gnu.org/wiki/Visibility
#if !defined(SKCMS_API)
    // Client has opted in to building skcms as part of a DLL/shared object file.
    #if defined(SKCMS_DLL)
        // If on Windows (MSVC or Clang pretending to be MSVC)
        #if defined(_MSC_VER)
            // Client needs to set this when compiling the skcms files (but *not* when compiling
            // other files which use the headers). In GN, this is handled with non-public configs.
            // With Bazel, this can be implemented with local_defines.
            #if defined(SKCMS_IMPLEMENTATION)
                // If SKCMS is being built as a standalone DLL, it must be explicitly exported
                // https://learn.microsoft.com/en-us/cpp/cpp/dllexport-dllimport?view=msvc-170
                #define SKCMS_API __declspec(dllexport)
            #else
                // If a client is compiling and using the skcms header, this is an optimization
                // to skip some indirection when resolving the function call.
                // https://learn.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-6.0/aa271769(v=vs.60)
                #define SKCMS_API __declspec(dllimport)
            #endif
        #else
            // On non-Windows platforms, we make sure the symbols are visible outside the
            // shared object file.
            #define SKCMS_API __attribute__((visibility("default")))
        #endif
    #else
        #define SKCMS_API
    #endif
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// A row-major 3x3 matrix (ie vals[row][col])
typedef struct skcms_Matrix3x3 {
    float vals[3][3];
} skcms_Matrix3x3;

// It is _not_ safe to alias the pointers to invert in-place.
SKCMS_API bool            skcms_Matrix3x3_invert(const skcms_Matrix3x3*, skcms_Matrix3x3*);
SKCMS_API skcms_Matrix3x3 skcms_Matrix3x3_concat(const skcms_Matrix3x3*, const skcms_Matrix3x3*);

// A transfer function mapping encoded values to linear values,
// represented by this 7-parameter piecewise function:
//
//   linear = sign(encoded) *  (c*|encoded| + f)       , 0 <= |encoded| < d
//          = sign(encoded) * ((a*|encoded| + b)^g + e), d <= |encoded|
//
// (A simple gamma transfer function sets g to gamma and a to 1.)
typedef struct skcms_TransferFunction {
    float g, a,b,c,d,e,f;
} skcms_TransferFunction;

SKCMS_API float skcms_TransferFunction_eval  (const skcms_TransferFunction*, float);
SKCMS_API bool  skcms_TransferFunction_invert(const skcms_TransferFunction*,
                                              skcms_TransferFunction*);

typedef enum skcms_TFType {
    skcms_TFType_Invalid,
    skcms_TFType_sRGBish,
    skcms_TFType_PQish,
    skcms_TFType_HLGish,
    skcms_TFType_HLGinvish,
    skcms_TFType_PQ,
    skcms_TFType_HLG,
} skcms_TFType;

// Identify which kind of transfer function is encoded in an skcms_TransferFunction
SKCMS_API skcms_TFType skcms_TransferFunction_getType(const skcms_TransferFunction*);

// We can jam a couple alternate transfer function forms into skcms_TransferFunction,
// including those matching the general forms of the SMPTE ST 2084 PQ function or HLG.
//
// PQish:
//                              max(A + B|encoded|^C, 0)
//    linear = sign(encoded) * (------------------------) ^ F
//                                  D + E|encoded|^C
SKCMS_API bool skcms_TransferFunction_makePQish(skcms_TransferFunction*,
                                                float A, float B, float C,
                                                float D, float E, float F);
// HLGish:
//            { K * sign(encoded) * ( (R|encoded|)^G )          when 0   <= |encoded| <= 1/R
//   linear = { K * sign(encoded) * ( e^(a(|encoded|-c)) + b )  when 1/R <  |encoded|
SKCMS_API bool skcms_TransferFunction_makeScaledHLGish(skcms_TransferFunction*,
                                                       float K, float R, float G,
                                                       float a, float b, float c);

// Compatibility shim with K=1 for old callers.
static inline bool skcms_TransferFunction_makeHLGish(skcms_TransferFunction* fn,
                                                     float R, float G,
                                                     float a, float b, float c) {
    return skcms_TransferFunction_makeScaledHLGish(fn, 1.0f, R,G, a,b,c);
}

// The PQ transfer function. The function skcms_TransferFunction_eval will always evaluate to the
// unit PQ EOTF, which maps [0, 1] to [0, 1], regardless of the other parameters.
// This is stored differently from PQish transfer functions. In particular:
//   - the constant -5 is stored in g
//   - the hdr_reference_white_luminance parameter is stored in a
//   - all other parameters are set to 0
// When this is used as an SkColorSpace, the transformation to XYZD50 will be as follows:
//   1. Apply the unit PQ EOTF to each channel
//   2. Multiply by 10,000 nits
//   3. Divide by hdr_reference_white_luminance nits (default is 203)
//   4. Transform primaries to XYZD50
SKCMS_API void skcms_TransferFunction_makePQ(
    skcms_TransferFunction*,
    float hdr_reference_white_luminance);

// The HLG transfer function. The function skcms_TransferFunction_eval will always evaluate to the
// HLG inverse OETF, which maps [0, 1] to [0, 1], regardless of the other parameters.
// This is stored differently from PQish transfer functions. In particular:
//   - the constant -6 is stored in g
//   - the hdr_reference_white_luminance parameter is stored in a
//   - the peak_white_luminance parameter is stored in b
//   - the system_gamma parameter is stored in c
//   - all other parameters are set to 0
// When this is used as an SkColorSpace, the transformation to XYZD50 will be as follows:
//   1. Apply the HLG inverse OETF to each channel
//   2. Transform primaries to Rec2020
//   3. Apply the channel-mixing HLG OOTF using system_gamma (default is 1.2)
//   4. Multiply by peak_luminance nits (default is 1,000)
//   5. Divide by hdr_reference_white nits (default is 203)
//   6. Transform primaries to XYZD50
SKCMS_API void skcms_TransferFunction_makeHLG(
    skcms_TransferFunction*,
    float hdr_reference_white_luminance,
    float peak_luminance,
    float system_gamma);

// Is this an ordinary sRGB-ish transfer function, or one of the HDR forms we support?
SKCMS_API bool skcms_TransferFunction_isSRGBish(const skcms_TransferFunction*);
SKCMS_API bool skcms_TransferFunction_isPQish  (const skcms_TransferFunction*);
SKCMS_API bool skcms_TransferFunction_isHLGish (const skcms_TransferFunction*);
SKCMS_API bool skcms_TransferFunction_isPQ     (const skcms_TransferFunction*);
SKCMS_API bool skcms_TransferFunction_isHLG    (const skcms_TransferFunction*);

SKCMS_API const skcms_TransferFunction* skcms_sRGB_TransferFunction(void);
SKCMS_API const skcms_TransferFunction* skcms_sRGB_Inverse_TransferFunction(void);
SKCMS_API const skcms_TransferFunction* skcms_Identity_TransferFunction(void);

// Returns a matrix to adapt XYZ color from given the whitepoint to D50.
SKCMS_API bool skcms_AdaptToXYZD50(float wx, float wy,
                                   skcms_Matrix3x3* toXYZD50);

// Returns a matrix to convert RGB color into XYZ adapted to D50, given the
// primaries and whitepoint of the RGB model.
SKCMS_API bool skcms_PrimariesToXYZD50(float rx, float ry,
                                       float gx, float gy,
                                       float bx, float by,
                                       float wx, float wy,
                                       skcms_Matrix3x3* toXYZD50);

#ifdef __cplusplus
}
#endif
