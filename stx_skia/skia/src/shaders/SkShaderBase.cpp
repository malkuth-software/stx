/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/shaders/SkShaderBase.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkColorFilter.h"
#include "src/core/SkColorSpaceXformSteps.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpList.h"
#include "src/shaders/SkLocalMatrixShader.h"

#include <utility>

class SkWriteBuffer;

namespace SkShaders {
MatrixRec::MatrixRec(const SkMatrix& ctm) : fCTM(ctm) {}

std::optional<MatrixRec> MatrixRec::apply(const SkStageRec& rec, const SkMatrix& postInv) const {
    SkMatrix total = fPendingLocalMatrix;
    if (!fCTMApplied) {
        total = SkMatrix::Concat(fCTM, total);
    }
    const std::optional<SkMatrix> inverse = total.invert();
    if (!inverse) {
        return std::nullopt;
    }
    total = SkMatrix::Concat(postInv, *inverse);
    if (!fCTMApplied) {
        // A tiled clip evaluates at tile coordinates; restore the original
        // device position before applying the captured shader CTM inverse.
        if (rec.fDeviceOffset.fX != 0 || rec.fDeviceOffset.fY != 0) {
            const SkMatrix offset = SkMatrix::Translate(rec.fDeviceOffset.fX,
                                                         rec.fDeviceOffset.fY);
            total = SkMatrix::Concat(total, offset);
        }
        (*rec.fPipeline).append(SkRasterPipelineOp::seed_shader);
    }
    // appendMatrix is a no-op if total worked out to identity.
    (*rec.fPipeline).appendMatrix(rec.fAlloc, total);
    const SkMatrix identity{};
    const MatrixRec applied{fCTM, fTotalLocalMatrix, identity, fTotalMatrixIsValid, true};
    return applied;
}

MatrixRec MatrixRec::concat(const SkMatrix& m) const {
    const SkMatrix total = SkShaderBase::ConcatLocalMatrices(fTotalLocalMatrix, m);
    const SkMatrix pending = SkShaderBase::ConcatLocalMatrices(fPendingLocalMatrix, m);
    const MatrixRec combined{fCTM, total, pending, fTotalMatrixIsValid, fCTMApplied};
    return combined;
}

}  // namespace SkShaders

///////////////////////////////////////////////////////////////////////////////////////

SkShaderBase::SkShaderBase() {}

SkShaderBase::~SkShaderBase() = default;

void SkShaderBase::flatten(SkWriteBuffer& buffer) const { INHERITED::flatten(buffer); }

bool SkShaderBase::asLuminanceColor(SkColor4f* colorPtr) const {
    SkColor4f storage{};
    if (nullptr == colorPtr) {
        colorPtr = &storage;
    }
    if (onAsLuminanceColor(colorPtr)) {
        (*colorPtr).fA = 1.0f;  // we only return opaque
        return true;
    }
    return false;
}

SkShaderBase::Context* SkShaderBase::makeContext(const ContextRec& rec, SkArenaAlloc* alloc) const {
#ifdef SK_ENABLE_LEGACY_SHADERCONTEXT
    // We always fall back to raster pipeline when perspective is present.
    const SkMatrix totalMatrix = rec.fMatrixRec.totalMatrix();
    if (totalMatrix.hasPerspective() || !totalMatrix.invert()) {
        return nullptr;
    }

    Context* context = onMakeContext(rec, alloc);
    return context;
#else
    return nullptr;
#endif
}

SkShaderBase::Context::Context(const SkShaderBase& shader, const ContextRec& rec)
        : fShader(shader), fPaintAlpha(rec.fPaintAlpha) {
    // We should never use a context with perspective.
    SkASSERT(!rec.fMatrixRec.totalMatrix().hasPerspective());

}

SkShaderBase::Context::~Context() {}

bool SkShaderBase::ContextRec::isLegacyCompatible(SkColorSpace* shaderColorSpace) const {
    // In legacy pipelines, shaders always produce premul (or opaque) and the destination is also
    // always premul (or opaque).  (And those "or opaque" caveats won't make any difference here.)
    const SkColorSpaceXformSteps conversion{shaderColorSpace, kPremul_SkAlphaType,
                                            fDstColorSpace, kPremul_SkAlphaType};
    const bool compatible = conversion.fFlags.mask() == 0;
    return compatible;
}

sk_sp<SkShader> SkShaderBase::makeAsALocalMatrixShader(SkMatrix*) const { return nullptr; }

bool SkShaderBase::appendRootStages(const SkStageRec& rec, const SkMatrix& ctm) const {
    const SkShaders::MatrixRec matrix{ctm};
    const bool appended = appendStages(rec, matrix);
    return appended;
}

sk_sp<SkShader> SkShaderBase::makeWithCTM(const SkMatrix& postM) const {
    sk_sp<SkShader> shader = sk_ref_sp(this);
    sk_sp<SkShader> wrapped = sk_make_sp<SkCTMShader>(std::move(shader), postM);
    return wrapped;
}

// need a cheap way to invert the alpha channel of a shader (i.e. 1 - a)
sk_sp<SkShader> SkShaderBase::makeInvertAlpha() const {
    sk_sp<SkColorFilter> filter = SkColorFilters::Blend(0xFFFFFFFF, SkBlendMode::kSrcOut);
    sk_sp<SkShader> inverted = makeWithColorFilter(std::move(filter));
    return inverted;
}
