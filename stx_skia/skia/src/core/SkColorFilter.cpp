/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "include/core/SkColorFilter.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkFlattenable.h"
#include "include/core/SkRefCnt.h"
#include "modules/skcms/skcms.h"
#include "src/core/SkColorData.h"
#include "src/core/SkColorFilterPriv.h"
#include "src/core/SkColorSpaceXformSteps.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"

#include <cstddef>

enum class SkBlendMode;
struct SkDeserialProcs;

bool SkColorFilter::asAColorMode(SkColor* color, SkBlendMode* mode) const {
    const SkColorFilterBase& filter = *as_CFB(this);
    const bool result = filter.onAsAColorMode(color, mode);
    return result;
}

bool SkColorFilter::asAColorMatrix(float matrix[20]) const {
    const SkColorFilterBase& filter = *as_CFB(this);
    const bool result = filter.onAsAColorMatrix(matrix);
    return result;
}

bool SkColorFilter::isAlphaUnchanged() const {
    const SkColorFilterBase& filter = *as_CFB(this);
    const bool result = filter.onIsAlphaUnchanged();
    return result;
}

sk_sp<SkColorFilter> SkColorFilter::Deserialize(const void* data, size_t size,
                                                const SkDeserialProcs* procs) {
    sk_sp<SkFlattenable> decoded = SkFlattenable::Deserialize(kSkColorFilter_Type, data, size, procs);
    sk_sp<SkColorFilter> filter(static_cast<SkColorFilter*>(decoded.release()));
    return filter;
}

SkColor4f SkColorFilter::filterColor4f(const SkColor4f& origSrcColor, SkColorSpace* srcCS,
                                       SkColorSpace* dstCS) const {
    SkPMColor4f color = { origSrcColor.fR, origSrcColor.fG, origSrcColor.fB, origSrcColor.fA };
    const SkColorSpaceXformSteps conversion(srcCS, kUnpremul_SkAlphaType,
                                            dstCS, kPremul_SkAlphaType);
    conversion.apply(color.vec());
    const SkColorFilterBase& filter = *as_CFB(this);
    const SkPMColor4f filtered = filter.onFilterColor4f(color, dstCS);
    // Clamp alpha before converting back to the public unpremultiplied form.
    const SkPMColor4f pinned = filtered.pinAlpha();
    const SkColor4f result = pinned.unpremul();
    return result;
}

sk_sp<SkColorFilter> SkColorFilter::makeWithWorkingColorSpace(
        sk_sp<SkColorSpace> workingSpace) const {
    SkColorFilter* base = const_cast<SkColorFilter*>(this);
    if (!workingSpace) {
        sk_sp<SkColorFilter> retained = sk_ref_sp(base);
        return retained;
    }

    skcms_TransferFunction tf{};
    skcms_Matrix3x3 toXYZ{};
    (*workingSpace).transferFn(&tf);
    (*workingSpace).toXYZD50(&toXYZ);
    const SkAlphaType* kOriginalAlphaType = nullptr;
    sk_sp<SkColorFilter> retained = sk_ref_sp(base);
    sk_sp<SkColorFilter> filter = SkColorFilterPriv::WithWorkingFormat(
            std::move(retained), &tf, &toXYZ, kOriginalAlphaType);
    return filter;
}
