/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/shaders/gradients/SkLinearGradient.h"

#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkShader.h"
#include "include/effects/SkGradient.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkTArray.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"
#include "src/shaders/SkShaderBase.h"

#include <cstdint>
#include <optional>
#include <utility>

class SkArenaAlloc;
class SkRasterPipeline;
enum class SkTileMode;

static SkMatrix pts_to_unit_matrix(const SkPoint pts[2]) {
    SkVector    vec = pts[1] - pts[0];
    const SkScalar mag = vec.length();
    const SkScalar inv = mag ? SkScalarInvert(mag) : 0;

    vec.scale(inv);
    SkMatrix matrix{};
    matrix.setSinCos(-vec.fY, vec.fX, pts[0].fX, pts[0].fY);
    matrix.postTranslate(-pts[0].fX, -pts[0].fY);
    matrix.postScale(inv, inv);
    return matrix;
}

///////////////////////////////////////////////////////////////////////////////

SkLinearGradient::SkLinearGradient(const SkPoint pts[2], const SkGradient& desc)
        : SkGradientBaseShader(desc, pts_to_unit_matrix(pts)), fStart(pts[0]), fEnd(pts[1]) {}

sk_sp<SkFlattenable> SkLinearGradient::CreateProc(SkReadBuffer& buffer) {
    SkGradientScope scope{};
    SkMatrix legacyLocalMatrix{};
    const SkMatrix* lmPtr = nullptr;
    const std::optional<SkGradient> grad = scope.unflatten(buffer, &legacyLocalMatrix);
    if (!grad) {
        return nullptr;
    }
    if (!legacyLocalMatrix.isIdentity()) {
        lmPtr = &legacyLocalMatrix;
    }
    SkPoint pts[2]{};
    pts[0] = buffer.readPoint();
    pts[1] = buffer.readPoint();
    SkGradientShaderResult made = SkShaders::LinearGradient(pts, *grad, lmPtr);
    sk_sp<SkFlattenable> shader = std::move(made.shader);
    return shader;
}

void SkLinearGradient::flatten(SkWriteBuffer& buffer) const {
    SkGradientBaseShader::flatten(buffer);
    buffer.writePoint(fStart);
    buffer.writePoint(fEnd);
}

void SkLinearGradient::appendGradientStages(SkArenaAlloc*, SkRasterPipeline*,
                                            SkRasterPipeline*) const {
    // No extra stage needed for linear gradients.
}

SkShaderBase::GradientType SkLinearGradient::asGradient(GradientInfo* info,
                                                        SkMatrix* localMatrix) const {
    if (info) {
        commonAsAGradient(info);
        (*info).fPoint[0] = fStart;
        (*info).fPoint[1] = fEnd;
    }
    if (localMatrix) {
        *localMatrix = SkMatrix::I();
    }
    return GradientType::kLinear;
}

SkGradientShaderResult SkShaders::LinearGradient(const SkPoint pts[2], const SkGradient& grad,
                                                 const SkMatrix* lm) {
    SkGradientShaderResult result{};
    if (!pts) {
        result.rejection = SkGradientRejection::kMissingPoints;
        return result;
    }
    // A non-finite point, or finite points too far apart, makes the length non-finite.
    if (!SkIsFinite((pts[1] - pts[0]).length())) {
        result.rejection = SkGradientRejection::kNonFiniteGeometry;
        return result;
    }

    const SkGradient::Colors& colors = grad.colors();
    const SkGradient::Interpolation& interp = grad.interpolation();
    const SkSpan<const SkColor4f> stops = colors.colors();
    if (!SkGradientBaseShader::ValidGradient(stops, colors.tileMode(), interp)) {
        result.rejection = SkGradientRejection::kInvalidColors;
        return result;
    }
    if (stops.size() == 1) {
        result.shader = SkShaders::Color(stops.front(), colors.colorSpace());
        return result;
    }
    if (lm && !(*lm).invert(nullptr)) {
        result.rejection = SkGradientRejection::kSingularLocalMatrix;
        return result;
    }

    if (SkScalarNearlyZero((pts[1] - pts[0]).length(),
                           SkGradientBaseShader::kDegenerateThreshold)) {
        // Degenerate gradient, the only tricky complication is when in clamp mode, the limit of
        // the gradient approaches two half planes of solid color (first and last). However, they
        // are divided by the line perpendicular to the start and end point, which becomes undefined
        // once start and end are exactly the same, so just use the end color for a stable solution.
        result.shader = SkGradientBaseShader::MakeDegenerateGradient(colors);
        return result;
    }

    sk_sp<SkShader> s = sk_make_sp<SkLinearGradient>(pts, grad);
    const SkMatrix matrix = lm ? *lm : SkMatrix::I();
    result.shader = (*s).makeWithLocalMatrix(matrix);
    return result;
}
