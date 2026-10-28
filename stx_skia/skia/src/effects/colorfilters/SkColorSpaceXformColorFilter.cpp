/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/effects/colorfilters/SkColorSpaceXformColorFilter.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkRefCnt.h"
#include "src/core/SkColorFilterPriv.h"
#include "src/core/SkColorSpaceXformSteps.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkGraphicsLifetime.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpList.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"

#include <array>
#include <cstdint>
#include <utility>

SkColorSpaceXformColorFilter::SkColorSpaceXformColorFilter(sk_sp<SkColorSpace> src,
                                                           sk_sp<SkColorSpace> dst)
        : fSrc(std::move(src))
        , fDst(std::move(dst))
        , fSteps(  // We handle premul/unpremul separately, so here just always upm->upm.
                  fSrc.get(),
                  kUnpremul_SkAlphaType,
                  fDst.get(),
                  kUnpremul_SkAlphaType) {}

bool SkColorSpaceXformColorFilter::appendStages(const SkStageRec& rec, bool shaderIsOpaque) const {
    SkRasterPipeline& pipeline = *rec.fPipeline;
    if (!shaderIsOpaque) {
        pipeline.append(SkRasterPipelineOp::unpremul);
    }

    fSteps.apply(rec.fPipeline);

    if (!shaderIsOpaque) {
        pipeline.append(SkRasterPipelineOp::premul);
    }
    return true;
}

void SkColorSpaceXformColorFilter::flatten(SkWriteBuffer& buffer) const {
    const sk_sp<SkData> source = (*fSrc).serialize();
    buffer.writeDataAsByteArray(source.get());
    const sk_sp<SkData> destination = (*fDst).serialize();
    buffer.writeDataAsByteArray(destination.get());
}

sk_sp<SkFlattenable> SkColorSpaceXformColorFilter::LegacyGammaOnlyCreateProc(SkReadBuffer& buffer) {
    const uint32_t dir = buffer.read32();
    if (!buffer.validate(dir <= 1)) {
        return nullptr;
    }
    sk_sp<SkFlattenable> filter = dir == 0 ? SkColorFilters::LinearToSRGBGamma()
                                          : SkColorFilters::SRGBToLinearGamma();
    return filter;
}

sk_sp<SkFlattenable> SkColorSpaceXformColorFilter::CreateProc(SkReadBuffer& buffer) {
    std::array<sk_sp<SkColorSpace>, 2> colorSpaces{};
    for (int i = 0; i < 2; ++i) {
        sk_sp<SkData> data = buffer.readByteArrayAsData();
        if (!buffer.validate(data != nullptr)) {
            return nullptr;
        }
        colorSpaces[i] = SkColorSpace::Deserialize((*data).data(), (*data).size());
        if (!buffer.validate(colorSpaces[i] != nullptr)) {
            return nullptr;
        }
    }
    sk_sp<SkFlattenable> filter(
            new SkColorSpaceXformColorFilter(std::move(colorSpaces[0]), std::move(colorSpaces[1])));
    return filter;
}

// The two shared gamma filters, made on first use. Each holds the process's reference until
// SkGraphics::Shutdown() drops it; a paint that still shares one keeps it alive until it goes.
static SkColorSpaceXformColorFilter*& linear_to_srgb_slot() {
    static SkColorSpaceXformColorFilter* gSingleton =
            new SkColorSpaceXformColorFilter(SkColorSpace::MakeSRGBLinear(),
                                             SkColorSpace::MakeSRGB());
    return gSingleton;
}

static SkColorSpaceXformColorFilter*& srgb_to_linear_slot() {
    static SkColorSpaceXformColorFilter* gSingleton =
            new SkColorSpaceXformColorFilter(SkColorSpace::MakeSRGB(),
                                             SkColorSpace::MakeSRGBLinear());
    return gSingleton;
}

sk_sp<SkColorFilter> SkColorFilters::LinearToSRGBGamma() {
    SkGraphicsCheckRunning();
    sk_sp<SkColorFilter> filter = sk_ref_sp(linear_to_srgb_slot());
    return filter;
}

sk_sp<SkColorFilter> SkColorFilters::SRGBToLinearGamma() {
    SkGraphicsCheckRunning();
    sk_sp<SkColorFilter> filter = sk_ref_sp(srgb_to_linear_slot());
    return filter;
}

void SkColorFiltersReleaseGamma() {
    SkColorSpaceXformColorFilter*& toSRGB = linear_to_srgb_slot();
    SkSafeUnref(toSRGB);
    toSRGB = nullptr;
    SkColorSpaceXformColorFilter*& toLinear = srgb_to_linear_slot();
    SkSafeUnref(toLinear);
    toLinear = nullptr;
}

sk_sp<SkColorFilter> SkColorFilterPriv::MakeColorSpaceXform(sk_sp<SkColorSpace> src,
                                                            sk_sp<SkColorSpace> dst) {
    sk_sp<SkColorFilter> filter = sk_make_sp<SkColorSpaceXformColorFilter>(std::move(src), std::move(dst));
    return filter;
}
