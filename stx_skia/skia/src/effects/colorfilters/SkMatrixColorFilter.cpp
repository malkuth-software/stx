/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/effects/colorfilters/SkMatrixColorFilter.h"

#include "include/core/SkColorFilter.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/effects/SkColorMatrix.h"
#include "include/private/SkAssert.h"
#include "include/private/SkFloatingPoint.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkPicturePriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpList.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"

#include <array>
#include <algorithm>

static bool is_alpha_unchanged(const float matrix[20]) {
    const float* srcA = matrix + 15;

    const bool unchanged = SkScalarNearlyZero(srcA[0]) && SkScalarNearlyZero(srcA[1]) &&
                           SkScalarNearlyZero(srcA[2]) && SkScalarNearlyEqual(srcA[3], 1.0f) &&
                           SkScalarNearlyZero(srcA[4]);
    return unchanged;
}

SkMatrixColorFilter::SkMatrixColorFilter(const float array[20], Domain domain, Clamp clamp)
        : fMatrix{}, fAlphaIsUnchanged(is_alpha_unchanged(array)), fDomain(domain), fClamp(clamp) {
    std::copy_n(array, fMatrix.size(), fMatrix.begin());
}

void SkMatrixColorFilter::flatten(SkWriteBuffer& buffer) const {
    buffer.writeScalarArray(fMatrix);

    // RGBA flag
    buffer.writeBool(fDomain == Domain::kRGBA);
    buffer.writeBool(fClamp == Clamp::kYes);
}

sk_sp<SkFlattenable> SkMatrixColorFilter::CreateProc(SkReadBuffer& buffer) {
    std::array<float, 20> matrix{};
    if (!buffer.readScalarArray(matrix)) {
        return nullptr;
    }

    const bool isRGBA = buffer.readBool();
    Clamp clamp = Clamp::kYes;
    if (!buffer.isVersionLT(SkPicturePriv::kUnclampedMatrixColorFilter)) {
        clamp = buffer.readBool() ? Clamp::kYes : Clamp::kNo;
    }
    // clamp option is ignored for HSL-domain filters
    sk_sp<SkFlattenable> filter = isRGBA ? SkColorFilters::Matrix(matrix.data(), clamp)
                                          : SkColorFilters::HSLAMatrix(matrix.data());
    return filter;
}

bool SkMatrixColorFilter::onAsAColorMatrix(float matrix[20]) const {
    if (matrix) {
        std::copy_n(fMatrix.begin(), fMatrix.size(), matrix);
    }
    return true;
}

bool SkMatrixColorFilter::appendStages(const SkStageRec& rec, bool shaderIsOpaque) const {
    const bool willStayOpaque = shaderIsOpaque && fAlphaIsUnchanged;
    const bool hsla = fDomain == Domain::kHSLA;
    const bool clamp = fClamp == Clamp::kYes;
    SkRasterPipeline& pipeline = *rec.fPipeline;
    if (!shaderIsOpaque) {
        pipeline.append(SkRasterPipelineOp::unpremul);
    }
    if (hsla) {
        pipeline.append(SkRasterPipelineOp::rgb_to_hsl);
    }
    pipeline.append(SkRasterPipelineOp::matrix_4x5, fMatrix.data());
    if (hsla) {
        pipeline.append(SkRasterPipelineOp::hsl_to_rgb);
    }
    if (clamp) {
        pipeline.append(SkRasterPipelineOp::clamp_01);
    } else {
        // We still need to clamp alpha, regardless
        pipeline.append(SkRasterPipelineOp::clamp_a_01);
    }
    if (!willStayOpaque) {
        pipeline.append(SkRasterPipelineOp::premul);
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////

static sk_sp<SkColorFilter> MakeMatrix(const float array[20],
                                       SkMatrixColorFilter::Domain domain,
                                       SkColorFilters::Clamp clamp) {
    if (!SkIsFinite(array, 20)) {
        return nullptr;
    }
    sk_sp<SkColorFilter> filter = sk_make_sp<SkMatrixColorFilter>(array, domain, clamp);
    return filter;
}

sk_sp<SkColorFilter> SkColorFilters::Matrix(const float array[20], Clamp clamp) {
    sk_sp<SkColorFilter> filter = MakeMatrix(array, SkMatrixColorFilter::Domain::kRGBA, clamp);
    return filter;
}

sk_sp<SkColorFilter> SkColorFilters::Matrix(const SkColorMatrix& cm, Clamp clamp) {
    sk_sp<SkColorFilter> filter = MakeMatrix(cm.fMat.data(), SkMatrixColorFilter::Domain::kRGBA, clamp);
    return filter;
}

sk_sp<SkColorFilter> SkColorFilters::HSLAMatrix(const float array[20]) {
    sk_sp<SkColorFilter> filter = MakeMatrix(array, SkMatrixColorFilter::Domain::kHSLA, Clamp::kYes);
    return filter;
}

sk_sp<SkColorFilter> SkColorFilters::HSLAMatrix(const SkColorMatrix& cm) {
    sk_sp<SkColorFilter> filter = MakeMatrix(cm.fMat.data(), SkMatrixColorFilter::Domain::kHSLA, Clamp::kYes);
    return filter;
}
