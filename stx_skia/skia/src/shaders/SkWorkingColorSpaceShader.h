/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef SkWorkingColorSpaceShader_DEFINED
#define SkWorkingColorSpaceShader_DEFINED

#include "include/core/SkColorSpace.h"
#include "include/core/SkFlattenable.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkShader.h"
#include "include/private/SkAssert.h"
#include "src/shaders/SkShaderBase.h"

#include <utility>

class SkReadBuffer;
class SkWriteBuffer;
struct SkStageRec;

class SkWorkingColorSpaceShader final : public SkShaderBase {
public:
    // The public API uses premultiplied output; shader implementations do not
    // generally support the private unpremultiplied working mode yet.
    static sk_sp<SkShader> Make(sk_sp<SkShader> shader,
                                sk_sp<SkColorSpace> inputCS,
                                sk_sp<SkColorSpace> outputCS,
                                bool workInUnpremul);

    ShaderType type() const override { return ShaderType::kWorkingColorSpace; }

    bool isOpaque() const override {
        const bool opaque = (*fShader).isOpaque();
        return opaque;
    }

    sk_sp<SkShader> shader() const { return fShader; }

    struct WorkingSpace {
        sk_sp<SkColorSpace> inputSpace{};
        sk_sp<SkColorSpace> outputSpace{};
        SkAlphaType alphaType = kPremul_SkAlphaType;
    };

    WorkingSpace workingSpace(const sk_sp<SkColorSpace>& dstCS, SkAlphaType dstAT) const {
        sk_sp<SkColorSpace> inputSpace = fInputSpace ? fInputSpace : dstCS;
        sk_sp<SkColorSpace> outputSpace = fOutputSpace ? fOutputSpace : inputSpace;
        const SkAlphaType alphaType = fWorkInUnpremul ? kUnpremul_SkAlphaType : dstAT;
        WorkingSpace result{std::move(inputSpace), std::move(outputSpace), alphaType};
        return result;
    }

private:
    SkWorkingColorSpaceShader(sk_sp<SkShader> shader,
                              sk_sp<SkColorSpace> inputCS,
                              sk_sp<SkColorSpace> outputCS,
                              bool workInUnpremul)
            : fShader(std::move(shader))
            , fInputSpace(std::move(inputCS))
            , fOutputSpace(std::move(outputCS))
            , fWorkInUnpremul(workInUnpremul) {
        SkASSERT(fShader);
        SkASSERT(fInputSpace || fOutputSpace || fWorkInUnpremul);
    }

    bool appendStages(const SkStageRec& rec, const SkShaders::MatrixRec&) const override;

    SK_FLATTENABLE_HOOKS(SkWorkingColorSpaceShader)

    void flatten(SkWriteBuffer& buffer) const override;

    sk_sp<SkShader> fShader;
    sk_sp<SkColorSpace> fInputSpace;
    sk_sp<SkColorSpace> fOutputSpace;
    bool fWorkInUnpremul;
};

#endif
