/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/effects/SkColorMatrix.h"
#include "src/core/SkYUVMath.h"

enum SkYUVColorSpace : int;

SkColorMatrix SkColorMatrix::RGBtoYUV(SkYUVColorSpace cs) {
    SkColorMatrix m{};
    SkColorMatrix_RGB2YUV(cs, m.fMat.data());
    return m;
}

SkColorMatrix SkColorMatrix::YUVtoRGB(SkYUVColorSpace cs) {
    SkColorMatrix m{};
    SkColorMatrix_YUV2RGB(cs, m.fMat.data());
    return m;
}

enum {
    kR_Scale = 0,
    kG_Scale = 6,
    kB_Scale = 12,
    kA_Scale = 18,

    kR_Trans = 4,
    kG_Trans = 9,
    kB_Trans = 14,
    kA_Trans = 19,
};

static void set_concat(float result[20], const float outer[20], const float inner[20]) {
    // Exact input/output aliasing is supported; use scratch until every input
    // coefficient has been read. Arrays hold four RGBA rows of five coefficients.
    std::array<float, 20> scratch{};
    float* target = result;
    if (outer == result || inner == result) {
        target = scratch.data();
    }

    int index = 0;
    for (int j = 0; j < 20; j += 5) {
        for (int i = 0; i < 4; i++) {
            target[index] =   outer[j + 0] * inner[i + 0] +
                                outer[j + 1] * inner[i + 5] +
                                outer[j + 2] * inner[i + 10] +
                                outer[j + 3] * inner[i + 15];
            ++index;
        }
        target[index] =   outer[j + 0] * inner[4] +
                            outer[j + 1] * inner[9] +
                            outer[j + 2] * inner[14] +
                            outer[j + 3] * inner[19] +
                            outer[j + 4];
        ++index;
    }

    if (target != result) {
        std::copy_n(target, 20, result);
    }
}

void SkColorMatrix::setIdentity() {
    fMat.fill(0.0f);
    fMat[kR_Scale] = 1.0f;
    fMat[kG_Scale] = 1.0f;
    fMat[kB_Scale] = 1.0f;
    fMat[kA_Scale] = 1.0f;
}

void SkColorMatrix::setScale(float rScale, float gScale, float bScale, float aScale) {
    fMat.fill(0.0f);
    fMat[kR_Scale] = rScale;
    fMat[kG_Scale] = gScale;
    fMat[kB_Scale] = bScale;
    fMat[kA_Scale] = aScale;
}

void SkColorMatrix::postTranslate(float dr, float dg, float db, float da) {
    fMat[kR_Trans] += dr;
    fMat[kG_Trans] += dg;
    fMat[kB_Trans] += db;
    fMat[kA_Trans] += da;
}

///////////////////////////////////////////////////////////////////////////////

void SkColorMatrix::setConcat(const SkColorMatrix& matA, const SkColorMatrix& matB) {
    set_concat(fMat.data(), matA.fMat.data(), matB.fMat.data());
}

///////////////////////////////////////////////////////////////////////////////

// Sets the red, green and blue coefficients of one 5-column row.
static void setrow(std::array<float, 20>* mat, size_t row, float r, float g, float b) {
    const size_t first = row * 5;
    (*mat)[first] = r;
    (*mat)[first + 1] = g;
    (*mat)[first + 2] = b;
}

static const float kHueR = 0.213f;
static const float kHueG = 0.715f;
static const float kHueB = 0.072f;

void SkColorMatrix::setSaturation(float sat) {
    fMat.fill(0.0f);

    const float R = kHueR * (1.0f - sat);
    const float G = kHueG * (1.0f - sat);
    const float B = kHueB * (1.0f - sat);

    setrow(&fMat, 0, R + sat, G, B);
    setrow(&fMat, 1, R, G + sat, B);
    setrow(&fMat, 2, R, G, B + sat);
    fMat[kA_Scale] = 1.0f;
}
