/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/shaders/gradients/SkSweepGradient.h"

#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkShader.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkGradient.h"
#include "include/private/SkAssert.h"
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

SkSweepGradient::SkSweepGradient(const SkPoint& center,
                                 SkScalar t0,
                                 SkScalar t1,
                                 const SkGradient& desc)
        : SkGradientBaseShader(desc, SkMatrix::Translate(-center.x(), -center.y()))
        , fCenter(center)
        , fTBias(-t0)
        , fTScale(1 / (t1 - t0)) {
    SkASSERT(t0 < t1);
}

SkShaderBase::GradientType SkSweepGradient::asGradient(GradientInfo* info,
                                                       SkMatrix* localMatrix) const {
    if (info) {
        commonAsAGradient(info);
        (*info).fPoint[0] = fCenter;
        (*info).fPoint[1].fX = fTScale;
        (*info).fPoint[1].fY = fTBias;
    }
    if (localMatrix) {
        *localMatrix = SkMatrix::I();
    }
    return GradientType::kSweep;
}

struct SweepAngles {
    SkScalar start = 0;
    SkScalar end = 0;
};

static SweepAngles angles_from_t_coeff(SkScalar tBias, SkScalar tScale) {
    const SweepAngles angles{.start = -tBias * 360,
                             .end = (sk_ieee_float_divide(1, tScale) - tBias) * 360};
    return angles;
}

sk_sp<SkFlattenable> SkSweepGradient::CreateProc(SkReadBuffer& buffer) {
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

    const SkScalar tBias = buffer.readScalar();
    const SkScalar tScale = buffer.readScalar();
    const SweepAngles angles = angles_from_t_coeff(tBias, tScale);
    sk_sp<SkFlattenable> shader = SkShaders::SweepGradient(center, angles.start, angles.end,
                                                         *grad, lmPtr);
    return shader;
}

void SkSweepGradient::flatten(SkWriteBuffer& buffer) const {
    SkGradientBaseShader::flatten(buffer);
    buffer.writePoint(fCenter);
    buffer.writeScalar(fTBias);
    buffer.writeScalar(fTScale);
}

void SkSweepGradient::appendGradientStages(SkArenaAlloc* alloc, SkRasterPipeline* p,
                                           SkRasterPipeline*) const {
    (*p).append(SkRasterPipelineOp::xy_to_unit_angle);
    const SkMatrix scale = SkMatrix::Scale(fTScale, 1);
    const SkMatrix offset = SkMatrix::Translate(fTBias, 0);
    const SkMatrix matrix = scale * offset;
    (*p).appendMatrix(alloc, matrix);
}

sk_sp<SkShader> SkShaders::SweepGradient(SkPoint center, float startAngle, float endAngle,
                                         const SkGradient& grad, const SkMatrix* lm) {
    if (!SkIsFinite(startAngle, endAngle) || startAngle > endAngle) {
        return nullptr;
    }

    const SkGradient::Colors& colors = grad.colors();
    const SkGradient::Interpolation& interp = grad.interpolation();
    const SkSpan<const SkColor4f> stops = colors.colors();
    if (!SkGradientBaseShader::ValidGradient(stops, colors.tileMode(), interp)) {
        return nullptr;
    }
    if (stops.size() == 1) {
        sk_sp<SkShader> solid = SkShaders::Color(stops.front(), colors.colorSpace());
        return solid;
    }
    if (lm && !(*lm).invert(nullptr)) {
        return nullptr;
    }

    SkTileMode mode = colors.tileMode();

    if (SkScalarNearlyEqual(startAngle, endAngle, SkGradientBaseShader::kDegenerateThreshold)) {
        // Degenerate gradient, which should follow default degenerate behavior unless it is
        // clamped and the angle is greater than 0.
        if (mode == SkTileMode::kClamp && endAngle > SkGradientBaseShader::kDegenerateThreshold) {
            // In this case, the first color is repeated from 0 to the angle, then a hardstop
            // switches to the last color (all other colors are compressed to the infinitely thin
            // interpolation region).
            static constexpr float clampPos[3] = {0, 1, 1};
            const SkSpan<const SkColor4f> srcColors = colors.colors();
            const SkColor4f reColors[3] = {
                srcColors.front(), srcColors.front(), srcColors.back()
            };
            const SkGradient::Colors newColors = {
                reColors, clampPos, colors.tileMode(), colors.colorSpace()
            };
            const SkGradient replacement(newColors, interp);
            sk_sp<SkShader> shader = SkShaders::SweepGradient(center, 0, endAngle, replacement, lm);
            return shader;
        } else {
            sk_sp<SkShader> shader = SkGradientBaseShader::MakeDegenerateGradient(colors);
            return shader;
        }
    }

    if (startAngle <= 0 && endAngle >= 360) {
        // If the t-range includes [0,1], then we can always use clamping (presumably faster).
        mode = SkTileMode::kClamp;
    }

    const SkGradient newGrad {
        {colors.colors(), colors.positions(), mode, colors.colorSpace()},
        interp
    };

    const float t0 = startAngle / 360;
    const float t1 = endAngle / 360;

    sk_sp<SkShader> s = sk_make_sp<SkSweepGradient>(center, t0, t1, newGrad);
    const SkMatrix matrix = lm ? *lm : SkMatrix::I();
    sk_sp<SkShader> wrapped = (*s).makeWithLocalMatrix(matrix);
    return wrapped;
}
