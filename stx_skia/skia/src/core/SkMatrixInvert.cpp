/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkMatrixInvert.h"

#include "include/private/SkFloatingPoint.h"

bool SkInvert4x4Matrix(const SkScalar (&inMatrix)[16], SkScalar (&outMatrix)[16]) {
    const double a00 = inMatrix[0];
    const double a01 = inMatrix[1];
    const double a02 = inMatrix[2];
    const double a03 = inMatrix[3];
    const double a10 = inMatrix[4];
    const double a11 = inMatrix[5];
    const double a12 = inMatrix[6];
    const double a13 = inMatrix[7];
    const double a20 = inMatrix[8];
    const double a21 = inMatrix[9];
    const double a22 = inMatrix[10];
    const double a23 = inMatrix[11];
    const double a30 = inMatrix[12];
    const double a31 = inMatrix[13];
    const double a32 = inMatrix[14];
    const double a33 = inMatrix[15];

    double b00 = a00 * a11 - a01 * a10;
    double b01 = a00 * a12 - a02 * a10;
    double b02 = a00 * a13 - a03 * a10;
    double b03 = a01 * a12 - a02 * a11;
    double b04 = a01 * a13 - a03 * a11;
    double b05 = a02 * a13 - a03 * a12;
    double b06 = a20 * a31 - a21 * a30;
    double b07 = a20 * a32 - a22 * a30;
    double b08 = a20 * a33 - a23 * a30;
    double b09 = a21 * a32 - a22 * a31;
    double b10 = a21 * a33 - a23 * a31;
    double b11 = a22 * a33 - a23 * a32;

    // Calculate the determinant. It decides invertibility in double precision: narrowed to float,
    // a determinant below about 7e-46 in magnitude became zero, so a matrix such as a 2D scale by
    // 1e-23 was refused although its inverse is finite.
    const double determinant =
            b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (determinant == 0) {
        return false;
    }
    const double invdet = sk_ieee_double_divide(1.0, determinant);
    b00 *= invdet;
    b01 *= invdet;
    b02 *= invdet;
    b03 *= invdet;
    b04 *= invdet;
    b05 *= invdet;
    b06 *= invdet;
    b07 *= invdet;
    b08 *= invdet;
    b09 *= invdet;
    b10 *= invdet;
    b11 *= invdet;

    outMatrix[0]  = static_cast<float>(a11 * b11 - a12 * b10 + a13 * b09);
    outMatrix[1]  = static_cast<float>(a02 * b10 - a01 * b11 - a03 * b09);
    outMatrix[2]  = static_cast<float>(a31 * b05 - a32 * b04 + a33 * b03);
    outMatrix[3]  = static_cast<float>(a22 * b04 - a21 * b05 - a23 * b03);
    outMatrix[4]  = static_cast<float>(a12 * b08 - a10 * b11 - a13 * b07);
    outMatrix[5]  = static_cast<float>(a00 * b11 - a02 * b08 + a03 * b07);
    outMatrix[6]  = static_cast<float>(a32 * b02 - a30 * b05 - a33 * b01);
    outMatrix[7]  = static_cast<float>(a20 * b05 - a22 * b02 + a23 * b01);
    outMatrix[8]  = static_cast<float>(a10 * b10 - a11 * b08 + a13 * b06);
    outMatrix[9]  = static_cast<float>(a01 * b08 - a00 * b10 - a03 * b06);
    outMatrix[10] = static_cast<float>(a30 * b04 - a31 * b02 + a33 * b00);
    outMatrix[11] = static_cast<float>(a21 * b02 - a20 * b04 - a23 * b00);
    outMatrix[12] = static_cast<float>(a11 * b07 - a10 * b09 - a12 * b06);
    outMatrix[13] = static_cast<float>(a00 * b09 - a01 * b07 + a02 * b06);
    outMatrix[14] = static_cast<float>(a31 * b01 - a30 * b03 - a32 * b00);
    outMatrix[15] = static_cast<float>(a20 * b03 - a21 * b01 + a22 * b00);

    // If 1/det overflows to infinity (i.e. det is denormalized), the determinant is NaN, or any
    // inverted value does not fit in a float, the matrix is not invertible.
    const bool invertible = SkIsFinite(outMatrix, 16);
    return invertible;
}
