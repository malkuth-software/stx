/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/shaders/SkLocalMatrixShader.h"

#include "src/core/SkEffectPriv.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"

class SkImage;
enum class SkTileMode;
struct SkStageRec;

bool SkLocalMatrixShader::isConstant(SkColor4f* color) const {
    const SkShaderBase& wrapped = *as_SB(fWrappedShader);
    const bool constant = wrapped.isConstant(color);
    return constant;
}

SkShaderBase::GradientType SkLocalMatrixShader::asGradient(GradientInfo* info,
                                                           SkMatrix* localMatrix) const {
    const SkShaderBase& wrapped = *as_SB(fWrappedShader);
    const GradientType type = wrapped.asGradient(info, localMatrix);
    if (type != SkShaderBase::GradientType::kNone && localMatrix) {
        *localMatrix = ConcatLocalMatrices(fLocalMatrix, *localMatrix);
    }
    return type;
}

sk_sp<SkFlattenable> SkLocalMatrixShader::CreateProc(SkReadBuffer& buffer) {
    SkMatrix lm{};
    buffer.readMatrix(&lm);
    sk_sp<SkShader> baseShader = buffer.readShader();
    if (!baseShader) {
        return nullptr;
    }
    sk_sp<SkFlattenable> result = (*baseShader).makeWithLocalMatrix(lm);
    return result;
}

void SkLocalMatrixShader::flatten(SkWriteBuffer& buffer) const {
    buffer.writeMatrix(fLocalMatrix);
    buffer.writeFlattenable(fWrappedShader.get());
}

#ifdef SK_ENABLE_LEGACY_SHADERCONTEXT
SkShaderBase::Context* SkLocalMatrixShader::onMakeContext(const ContextRec& rec,
                                                          SkArenaAlloc* alloc) const {
    const SkShaderBase& wrapped = *as_SB(fWrappedShader);
    const ContextRec localRec = ContextRec::Concat(rec, fLocalMatrix);
    Context* context = wrapped.makeContext(localRec, alloc);
    return context;
}
#endif

SkImage* SkLocalMatrixShader::onIsAImage(SkMatrix* outMatrix, SkTileMode* mode) const {
    SkMatrix imageMatrix{};
    SkImage* image = (*fWrappedShader).isAImage(&imageMatrix, mode);
    if (image && outMatrix) {
        *outMatrix = ConcatLocalMatrices(fLocalMatrix, imageMatrix);
    }

    return image;
}

bool SkLocalMatrixShader::onAsLuminanceColor(SkColor4f* color) const {
    const SkShaderBase& wrapped = *as_SB(fWrappedShader);
    const bool hasLuminance = wrapped.asLuminanceColor(color);
    return hasLuminance;
}

bool SkLocalMatrixShader::appendStages(const SkStageRec& rec,
                                       const SkShaders::MatrixRec& mRec) const {
    const SkShaderBase& wrapped = *as_SB(fWrappedShader);
    const SkShaders::MatrixRec localRec = mRec.concat(fLocalMatrix);
    const bool appended = wrapped.appendStages(rec, localRec);
    return appended;
}

////////////////////////////////////////////////////////////////////

SkCTMShader::SkCTMShader(sk_sp<SkShader> proxy, const SkMatrix& ctm,
                         SkPoint deviceOffset)
        : fProxyShader(std::move(proxy)), fCTM(ctm), fDeviceOffset(deviceOffset) {}

bool SkCTMShader::isConstant(SkColor4f* color) const {
    const SkShaderBase& proxy = *as_SB(fProxyShader);
    const bool constant = proxy.isConstant(color);
    return constant;
}

SkShaderBase::GradientType SkCTMShader::asGradient(GradientInfo* info,
                                                   SkMatrix* localMatrix) const {
    const SkShaderBase& proxy = *as_SB(fProxyShader);
    const GradientType type = proxy.asGradient(info, localMatrix);
    return type;
}

bool SkCTMShader::appendStages(const SkStageRec& rec, const SkShaders::MatrixRec&) const {
    SkStageRec translated = rec;
    translated.fDeviceOffset.fX += fDeviceOffset.fX;
    translated.fDeviceOffset.fY += fDeviceOffset.fY;
    const SkShaderBase& proxy = *as_SB(fProxyShader);
    const bool appended = proxy.appendRootStages(translated, fCTM);
    return appended;
}

sk_sp<SkFlattenable> SkCTMShader::CreateProc(SkReadBuffer& buffer) {
    SkASSERT(false);
    return nullptr;
}
