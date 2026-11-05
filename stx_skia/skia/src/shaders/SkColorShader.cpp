/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/shaders/SkColorShader.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkFlattenable.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkShader.h"
#include "include/private/SkFloatingPoint.h"
#include "src/core/SkColorSpacePriv.h"
#include "src/core/SkColorSpaceXformSteps.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkPicturePriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"
#include "src/shaders/SkShaderBase.h"

#include <utility>

sk_sp<SkFlattenable> SkColorShader::CreateProc(SkReadBuffer& buffer) {
    if (buffer.isVersionLT(SkPicturePriv::Version::kCombineColorShaders)) {
        // Stored an 8-bit color only.
        const SkColor color = buffer.readColor();
        sk_sp<SkFlattenable> shader = SkShaders::Color(color);
        return shader;
    }
    // Stores a floating-point color in sRGB.
    SkColor4f color{};
    buffer.readColor4f(&color);
    sk_sp<SkColorSpace> srgb = SkColorSpace::MakeSRGB();
    sk_sp<SkFlattenable> shader = SkShaders::Color(color, std::move(srgb));
    return shader;
}

void SkColorShader::flatten(SkWriteBuffer& buffer) const {
    buffer.writeColor4f(fColor);
}

bool SkColorShader::appendStages(const SkStageRec& rec, const SkShaders::MatrixRec&) const {
    SkColor4f color = fColor;
    const SkColorSpaceXformSteps conversion(sk_srgb_singleton(), kUnpremul_SkAlphaType,
                                            rec.fDstCS, kPremul_SkAlphaType);
    conversion.apply(color.vec());
    (*rec.fPipeline).appendConstantColor(rec.fAlloc, color.vec());
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkShaders {
sk_sp<SkShader> Color(SkColor color) {
    const SkColor4f unpacked = SkColor4f::FromColor(color);
    sk_sp<SkColorSpace> srgb = SkColorSpace::MakeSRGB();
    sk_sp<SkShader> shader = Color(unpacked, std::move(srgb));
    return shader;
}

sk_sp<SkShader> Color(const SkColor4f& color, sk_sp<SkColorSpace> space) {
    if (!SkIsFinite(color.vec(), 4)) {
        return nullptr;
    }

    // Convert to sRGB to simplify what must be stored, remaining unpremul until the final dst
    // color space is known during actual shading. Also pin the alpha to [0,1].
    SkColor4f srgb = color.pinAlpha();
    const SkColorSpaceXformSteps conversion(space.get(), kUnpremul_SkAlphaType,
                                            sk_srgb_singleton(), kUnpremul_SkAlphaType);
    conversion.apply(srgb.vec());
    sk_sp<SkShader> shader = sk_make_sp<SkColorShader>(srgb);
    return shader;
}
}  // namespace SkShaders
