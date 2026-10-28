/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/shaders/SkWorkingColorSpaceShader.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkColorSpaceXformSteps.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkPicturePriv.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"
#include "src/shaders/SkShaderBase.h"

#include <utility>

sk_sp<SkShader> SkWorkingColorSpaceShader::Make(sk_sp<SkShader> shader,
                                                sk_sp<SkColorSpace> inputCS,
                                                sk_sp<SkColorSpace> outputCS,
                                                bool workInUnpremul) {
    if (!shader) {
        return nullptr;
    }

    if (!inputCS && !outputCS && !workInUnpremul) {
        // A null input is the final dst CS, and a null output is the input CS, so if both are null
        // then there's no additional conversion for children and no additional conversion
        // applied to the shader's output.
        return shader;
    }
    sk_sp<SkShader> wrapped(new SkWorkingColorSpaceShader(
            std::move(shader), std::move(inputCS), std::move(outputCS), workInUnpremul));
    return wrapped;
}

bool SkWorkingColorSpaceShader::appendStages(const SkStageRec& rec,
                                             const SkShaders::MatrixRec& mRec) const {
    sk_sp<SkColorSpace> dstCS = sk_ref_sp(rec.fDstCS);
    if (!dstCS) {
        dstCS = SkColorSpace::MakeSRGB();
    }

    // TODO(b/431253455): Should get the dstAT from `rec`
    const SkAlphaType dstAT = kPremul_SkAlphaType;
    const WorkingSpace spaces = workingSpace(dstCS, dstAT);
    const SkColorInfo dst = {rec.fDstColorType, dstAT, dstCS};
    const SkColorInfo input = {rec.fDstColorType, spaces.alphaType, spaces.inputSpace};
    const SkColorInfo output = {rec.fDstColorType, spaces.alphaType, spaces.outputSpace};

    SkArenaAlloc& arena = *rec.fAlloc;
    const SkColorSpaceXformSteps* dstToInput = arena.make<SkColorSpaceXformSteps>(dst, input);
    const SkColorSpaceXformSteps* outputToDst = arena.make<SkColorSpaceXformSteps>(output, dst);
    // NOTE: There is no inputToOutput steps to apply because it is assumed that the child shader
    // is responsible for such conversion (or input == output and it's a no-op).

    // Alpha-only image shaders reference the paint color, which is already in the destination
    // color space. We need to transform it to the working space for consistency.
    SkColor4f paintColorInWorkingSpace = rec.fPaintColor.makeOpaque();
    (*dstToInput).apply(paintColorInWorkingSpace.vec());

    // Preserve bounds, tile placement and future context fields; only the
    // color-space-dependent inputs change for the child. Surface props stay borrowed.
    SkStageRec workingRec = rec;
    workingRec.fDstCS = spaces.inputSpace.get();
    workingRec.fPaintColor = paintColorInWorkingSpace;

    const SkShaderBase& shader = *as_SB(fShader);
    if (!shader.appendStages(workingRec, mRec)) {
        return false;
    }

    (*outputToDst).apply(rec.fPipeline);
    return true;
}

void SkWorkingColorSpaceShader::flatten(SkWriteBuffer& buffer) const {
    buffer.writeFlattenable(fShader.get());
    buffer.writeBool(fWorkInUnpremul);

    buffer.writeBool(SkToBool(fInputSpace));
    if (fInputSpace) {
        const sk_sp<SkData> data = (*fInputSpace).serialize();
        buffer.writeDataAsByteArray(data.get());
    }

    buffer.writeBool(SkToBool(fOutputSpace));
    if (fOutputSpace) {
        const sk_sp<SkData> data = (*fOutputSpace).serialize();
        buffer.writeDataAsByteArray(data.get());
    }
}

sk_sp<SkFlattenable> SkWorkingColorSpaceShader::CreateProc(SkReadBuffer& buffer) {
    sk_sp<SkShader> shader(buffer.readShader());

    // If true, will not work in unpremul and assume inputSpace will be non-null and the outputSpace
    // will be null.
    const bool legacyWorkingCS = buffer.isVersionLT(SkPicturePriv::kWorkingColorSpaceOutput);

    const bool workInUnpremul = !legacyWorkingCS && buffer.readBool();

    // The input/output spaces are allowed to be null, but if we think we have a non-null CS, then
    // it better be deserializable.
    sk_sp<SkColorSpace> inputSpace{};
    if (legacyWorkingCS || buffer.readBool()) {
        sk_sp<SkData> data = buffer.readByteArrayAsData();
        if (!buffer.validate(data != nullptr)) {
            return nullptr;
        }
        inputSpace = SkColorSpace::Deserialize((*data).data(), (*data).size());
        if (!buffer.validate(inputSpace != nullptr)) {
            return nullptr;
        }
    }

    sk_sp<SkColorSpace> outputSpace{};
    if (!legacyWorkingCS && buffer.readBool()) {
        sk_sp<SkData> data = buffer.readByteArrayAsData();
        if (!buffer.validate(data != nullptr)) {
            return nullptr;
        }
        outputSpace = SkColorSpace::Deserialize((*data).data(), (*data).size());
        if (!buffer.validate(outputSpace != nullptr)) {
            return nullptr;
        }
    }

    sk_sp<SkFlattenable> wrapped = Make(std::move(shader), std::move(inputSpace),
                                         std::move(outputSpace), workInUnpremul);
    return wrapped;
}
