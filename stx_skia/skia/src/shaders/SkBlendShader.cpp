/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/shaders/SkBlendShader.h"

#include "include/core/SkBlendMode.h"
#include "include/core/SkBlender.h"
#include "include/core/SkFlattenable.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkBlendModePriv.h"
#include "src/core/SkBlenderBase.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpContexts.h"
#include "src/core/SkRasterPipelineOpList.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"
#include "src/shaders/SkShaderBase.h"

#include <optional>

namespace {
// The pipeline borrows these arrays from its arena until execution completes.
// Each store stage initializes exactly the active vector lanes before a load.
struct BlendStorage {
    float coordinates[2 * SkRasterPipelineContexts::kMaxStride];
    float firstResult[4 * SkRasterPipelineContexts::kMaxStride];
};

bool coefficient_is_opaque(SkBlendModeCoeff coefficient, bool sourceOpaque, bool destinationOpaque) {
    const bool sourceAlpha = coefficient == SkBlendModeCoeff::kSA || coefficient == SkBlendModeCoeff::kSC;
    const bool destinationAlpha = coefficient == SkBlendModeCoeff::kDA || coefficient == SkBlendModeCoeff::kDC;
    const bool opaque = coefficient == SkBlendModeCoeff::kOne ||
                        (sourceAlpha && sourceOpaque) || (destinationAlpha && destinationOpaque);
    return opaque;
}
}

sk_sp<SkFlattenable> SkBlendShader::CreateProc(SkReadBuffer& buffer) {
    sk_sp<SkShader> destination = buffer.readShader();
    sk_sp<SkShader> source = buffer.readShader();
    const bool haveChildren = destination && source;
    if (!buffer.validate(haveChildren)) return nullptr;

    const unsigned mode = buffer.read32();
    if (mode == kCustom_SkBlendMode) {
        sk_sp<SkBlender> blender = buffer.readBlender();
        if (!buffer.validate(blender != nullptr)) return nullptr;
        sk_sp<SkShader> result = SkShaders::Blend(std::move(blender), std::move(destination), std::move(source));
        return result;
    }
    const bool validMode = mode <= static_cast<unsigned>(SkBlendMode::kLastMode);
    if (!buffer.validate(validMode)) return nullptr;
    sk_sp<SkShader> result = SkShaders::Blend(static_cast<SkBlendMode>(mode), std::move(destination), std::move(source));
    return result;
}

bool SkBlendShader::isOpaque() const {
    SkBlendModeCoeff sourceCoefficient = SkBlendModeCoeff::kZero;
    SkBlendModeCoeff destinationCoefficient = SkBlendModeCoeff::kZero;
    if (!SkBlendMode_AsCoeff(fMode, &sourceCoefficient, &destinationCoefficient)) return false;
    const bool sourceOpaque = (*fSrc).isOpaque();
    const bool destinationOpaque = (*fDst).isOpaque();
    const bool sourceTermOpaque = sourceOpaque && coefficient_is_opaque(sourceCoefficient, sourceOpaque, destinationOpaque);
    const bool destinationTermOpaque = destinationOpaque && coefficient_is_opaque(destinationCoefficient, sourceOpaque, destinationOpaque);
    const bool opaque = sourceTermOpaque || destinationTermOpaque;
    return opaque;
}

void SkBlendShader::flatten(SkWriteBuffer& buffer) const {
    buffer.writeFlattenable(fDst.get());
    buffer.writeFlattenable(fSrc.get());
    buffer.write32((int)fMode);
}

// Appends the first shader, preserves its output, then appends the second.
// Failure leaves a partial pipeline; callers discard it rather than execute it.
static float* append_two_shaders(const SkStageRec& record,
                                 const SkShaders::MatrixRec& matrix,
                                 SkShader& first,
                                 SkShader& second) {
    SkRasterPipeline& pipeline = *record.fPipeline;
    BlendStorage& storage = *(*record.fAlloc).make<BlendStorage>();
    const bool preserveCoordinates = matrix.rasterPipelineCoordsAreSeeded();
    // Do not apply the matrix early: child local matrices retain their ordering.
    if (preserveCoordinates) pipeline.append(SkRasterPipelineOp::store_src_rg, storage.coordinates);
    const bool firstAppended = (*as_SB(&first)).appendStages(record, matrix);
    if (!firstAppended) return nullptr;
    pipeline.append(SkRasterPipelineOp::store_src, storage.firstResult);
    if (preserveCoordinates) pipeline.append(SkRasterPipelineOp::load_src_rg, storage.coordinates);
    const bool secondAppended = (*as_SB(&second)).appendStages(record, matrix);
    if (!secondAppended) return nullptr;
    return storage.firstResult;
}

bool SkBlendShader::appendStages(const SkStageRec& record, const SkShaders::MatrixRec& matrix) const {
    float* destination = append_two_shaders(record, matrix, *fDst, *fSrc);
    if (destination == nullptr) return false;
    (*record.fPipeline).append(SkRasterPipelineOp::load_dst, destination);
    SkBlendMode_AppendStages(fMode, record.fPipeline);
    return true;
}

sk_sp<SkShader> SkShaders::Blend(SkBlendMode mode, sk_sp<SkShader> dst, sk_sp<SkShader> src) {
    const bool validMode = static_cast<unsigned>(mode) <= static_cast<unsigned>(SkBlendMode::kLastMode);
    if (!src || !dst || !validMode) return nullptr;
    switch (mode) {
        case SkBlendMode::kClear:
        {
            sk_sp<SkShader> clear = Color(0);
            return clear;
        }
        case SkBlendMode::kDst:
            return dst;
        case SkBlendMode::kSrc:
            return src;
        default:
            break;
    }
    sk_sp<SkShader> result(new SkBlendShader(mode, std::move(dst), std::move(src)));
    return result;
}

sk_sp<SkShader> SkShaders::Blend(sk_sp<SkBlender> blender,
                                 sk_sp<SkShader> dst,
                                 sk_sp<SkShader> src) {
    if (!src || !dst) {
        return nullptr;
    }
    if (!blender) {
        sk_sp<SkShader> result = SkShaders::Blend(SkBlendMode::kSrcOver, std::move(dst), std::move(src));
        return result;
    }
    const std::optional<SkBlendMode> mode = (*as_BB(blender)).asBlendMode();
    if (mode) {
        sk_sp<SkShader> result = SkShaders::Blend(*mode, std::move(dst), std::move(src));
        return result;
    }

    // Custom runtime blenders are outside the CPU raster contract.
    return nullptr;
}
