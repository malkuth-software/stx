/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkBlendMode.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkTypes.h"
#include "include/effects/SkColorMatrix.h"
#include "include/private/SkCPUTypes.h"

static SkScalar byte_to_unit_float(U8CPU byte) {
    if (0xFF == byte) {
        // want to get this exact
        return 1.0f;
    }
    const SkScalar unit = static_cast<SkScalar>(byte) * 0.00392156862745f;
    return unit;
}

sk_sp<SkColorFilter> SkColorFilters::Lighting(SkColor mul, SkColor add) {
    const SkColor opaqueAlphaMask = SK_ColorBLACK;
    // omit the alpha and compare only the RGB values
    if (0 == (add & ~opaqueAlphaMask)) {
        const SkColor opaqueMultiplier = mul | opaqueAlphaMask;
        sk_sp<SkColorFilter> filter = SkColorFilters::Blend(opaqueMultiplier, SkBlendMode::kModulate);
        return filter;
    }

    SkColorMatrix matrix{};
    matrix.setScale(byte_to_unit_float(SkColorGetR(mul)),
                    byte_to_unit_float(SkColorGetG(mul)),
                    byte_to_unit_float(SkColorGetB(mul)),
                    1.0f);
    matrix.postTranslate(byte_to_unit_float(SkColorGetR(add)),
                         byte_to_unit_float(SkColorGetG(add)),
                         byte_to_unit_float(SkColorGetB(add)),
                         0.0f);
    sk_sp<SkColorFilter> filter = SkColorFilters::Matrix(matrix);
    return filter;
}
