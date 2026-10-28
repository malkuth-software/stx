/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "include/core/SkShader.h"

#include "include/core/SkColorFilter.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkRefCnt.h"
#include "src/shaders/SkColorFilterShader.h"
#include "src/shaders/SkLocalMatrixShader.h"
#include "src/shaders/SkShaderBase.h"
#include "src/shaders/SkWorkingColorSpaceShader.h"

#include <utility>

class SkImage;
enum class SkTileMode;

SkImage* SkShader::isAImage(SkMatrix* localMatrix, SkTileMode xy[2]) const & {
    const SkShaderBase& shader = *as_SB(this);
    SkImage* image = shader.onIsAImage(localMatrix, xy);
    return image;
}

sk_sp<SkShader> SkShader::makeWithLocalMatrix(const SkMatrix& localMatrix) const {
    SkMatrix combinedMatrix = localMatrix;
    sk_sp<SkShader> baseShader{};
    const SkShaderBase& shader = *as_SB(this);
    SkMatrix otherLocalMatrix{};
    sk_sp<SkShader> proxy = shader.makeAsALocalMatrixShader(&otherLocalMatrix);
    if (proxy) {
        combinedMatrix = SkShaderBase::ConcatLocalMatrices(localMatrix, otherLocalMatrix);
        baseShader = std::move(proxy);
    } else {
        baseShader = sk_ref_sp(const_cast<SkShader*>(this));
    }

    sk_sp<SkShader> result = sk_make_sp<SkLocalMatrixShader>(std::move(baseShader), combinedMatrix);
    return result;
}

sk_sp<SkShader> SkShader::makeWithColorFilter(sk_sp<SkColorFilter> filter) const {
    sk_sp<SkShader> shader = sk_ref_sp(this);
    sk_sp<SkShader> result = SkColorFilterShader::Make(std::move(shader), 1.0f, std::move(filter));
    return result;
}

sk_sp<SkShader> SkShader::makeWithWorkingColorSpace(sk_sp<SkColorSpace> inputCS,
                                                    sk_sp<SkColorSpace> outputCS) const {
    sk_sp<SkShader> shader = sk_ref_sp(this);
    sk_sp<SkShader> result = SkWorkingColorSpaceShader::Make(
            std::move(shader), std::move(inputCS), std::move(outputCS), false);
    return result;
}
