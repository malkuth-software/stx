/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/shaders/gradients/SkRadialGradient.h"

#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkShader.h"
#include "include/effects/SkGradient.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkTArray.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpList.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"
#include "src/shaders/SkShaderBase.h"
#include "src/shaders/gradients/SkGradientBaseShader.h"

#include <cstdint>
#include <optional>
#include <utility>

class SkArenaAlloc;
enum class SkTileMode;

static SkMatrix rad_to_unit_matrix(const SkPoint& center, SkScalar radius) {
    const SkScalar inv = SkScalarInvert(radius);

    SkMatrix matrix{};
    matrix.setTranslate(-center.fX, -center.fY);
    matrix.postScale(inv, inv);
    return matrix;
}

SkRadialGradient::SkRadialGradient(const SkPoint& center, SkScalar radius, const SkGradient& desc)
        : SkGradientBaseShader(desc, rad_to_unit_matrix(center, radius))
        , fCenter(center)
        , fRadius(radius) {}

SkShaderBase::GradientType SkRadialGradient::asGradient(GradientInfo* info,
                                                        SkMatrix* localMatrix) const {
    if (info) {
        commonAsAGradient(info);
        (*info).fPoint[0] = fCenter;
        (*info).fRadius[0] = fRadius;
    }
    if (localMatrix) {
        *localMatrix = SkMatrix::I();
    }
    return GradientType::kRadial;
}

sk_sp<SkFlattenable> SkRadialGradient::CreateProc(SkReadBuffer& buffer) {
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
    const SkPoint center = buffer.readPoint();
    const SkScalar radius = buffer.readScalar();
    SkGradientShaderResult made = SkShaders::RadialGradient(center, radius, *grad, lmPtr);
    sk_sp<SkFlattenable> shader = std::move(made.shader);
    return shader;
}

void SkRadialGradient::flatten(SkWriteBuffer& buffer) const {
    SkGradientBaseShader::flatten(buffer);
    buffer.writePoint(fCenter);
    buffer.writeScalar(fRadius);
}

void SkRadialGradient::appendGradientStages(SkArenaAlloc*, SkRasterPipeline* p,
                                            SkRasterPipeline*) const {
    (*p).append(SkRasterPipelineOp::xy_to_radius);
}

SkGradientShaderResult SkShaders::RadialGradient(SkPoint center, float radius,
                                                 const SkGradient& grad, const SkMatrix* lm) {
    SkGradientShaderResult result{};
    // Upstream checked only radius < 0, so a NaN radius or a non-finite center made a shader.
    if (!SkIsFinite(center.fX, center.fY, radius)) {
        result.rejection = SkGradientRejection::kNonFiniteGeometry;
        return result;
    }
    if (radius < 0) {
        result.rejection = SkGradientRejection::kNegativeRadius;
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

    if (SkScalarNearlyZero(radius, SkGradientBaseShader::kDegenerateThreshold)) {
        // Degenerate gradient optimization, and no special logic needed for clamped radial gradient
        result.shader = SkGradientBaseShader::MakeDegenerateGradient(colors);
        return result;
    }

    sk_sp<SkShader> s = sk_make_sp<SkRadialGradient>(center, radius, grad);
    const SkMatrix matrix = lm ? *lm : SkMatrix::I();
    result.shader = (*s).makeWithLocalMatrix(matrix);
    return result;
}
