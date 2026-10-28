/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/effects/colorfilters/SkWorkingFormatColorFilter.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/private/SkAssert.h"
#include "modules/skcms/skcms.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkColorFilterPriv.h"
#include "src/core/SkColorSpaceXformSteps.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"

#include <array>
#include <utility>

SkWorkingFormatColorFilter::SkWorkingFormatColorFilter(sk_sp<SkColorFilter> child,
                                                       const skcms_TransferFunction* tf,
                                                       const skcms_Matrix3x3* gamut,
                                                       const SkAlphaType* at)
        : fChild(std::move(child)), fWorkingFormatCalculator(tf, gamut, at) {
    SkASSERT(fChild);
}

sk_sp<SkColorSpace> SkWorkingFormatColorFilter::workingFormat(const sk_sp<SkColorSpace>& dstCS,
                                                              SkAlphaType* outAT) const {
    sk_sp<SkColorSpace> colorSpace = fWorkingFormatCalculator.workingFormat(dstCS, outAT);
    return colorSpace;
}

bool SkWorkingFormatColorFilter::appendStages(const SkStageRec& rec, bool shaderIsOpaque) const {
    sk_sp<SkColorSpace> dstCS = sk_ref_sp(rec.fDstCS);

    if (!dstCS) {
        dstCS = SkColorSpace::MakeSRGB();
    }

    SkAlphaType workingAT = kPremul_SkAlphaType;
    sk_sp<SkColorSpace> workingCS = workingFormat(dstCS, &workingAT);

    const SkColorInfo dst = {rec.fDstColorType, kPremul_SkAlphaType, dstCS};
    const SkColorInfo working = {rec.fDstColorType, workingAT, workingCS};

    // Stages borrow conversion coefficients, so their owners live in the arena
    // through execution. Color-space owners are needed only to build the steps.
    SkArenaAlloc& arena = *rec.fAlloc;
    const SkColorSpaceXformSteps* dstToWorking = arena.make<SkColorSpaceXformSteps>(dst, working);
    const SkColorSpaceXformSteps* workingToDst = arena.make<SkColorSpaceXformSteps>(working, dst);

    // Retained color filters do not reach alpha-only image tinting. Its paint
    // color therefore stays in the destination space and is not consumed here.

    const SkStageRec workingRec = {rec.fPipeline,
                             rec.fAlloc,
                             rec.fDstColorType,
                             workingCS.get(),
                             rec.fPaintColor,
                             rec.fSurfaceProps,
                             rec.fDstBounds};

    (*dstToWorking).apply(rec.fPipeline);
    const SkColorFilterBase& child = *as_CFB(fChild);
    if (!child.appendStages(workingRec, shaderIsOpaque)) {
        return false;
    }
    (*workingToDst).apply(rec.fPipeline);
    return true;
}

SkPMColor4f SkWorkingFormatColorFilter::onFilterColor4f(const SkPMColor4f& origColor,
                                                        SkColorSpace* rawDstCS) const {
    sk_sp<SkColorSpace> dstCS = sk_ref_sp(rawDstCS);
    if (!dstCS) {
        dstCS = SkColorSpace::MakeSRGB();
    }

    SkAlphaType workingAT = kPremul_SkAlphaType;
    sk_sp<SkColorSpace> workingCS = workingFormat(dstCS, &workingAT);

    const SkColorInfo dst = {kUnknown_SkColorType, kPremul_SkAlphaType, dstCS};
    const SkColorInfo working = {kUnknown_SkColorType, workingAT, workingCS};
    const SkColorSpaceXformSteps dstToWorking(dst, working);
    const SkColorSpaceXformSteps workingToDst(working, dst);
    const SkColorFilterBase& child = *as_CFB(fChild);
    SkPMColor4f color = origColor;
    dstToWorking.apply(color.vec());
    color = child.onFilterColor4f(color, working.colorSpace());
    workingToDst.apply(color.vec());
    return color;
}

bool SkWorkingFormatColorFilter::onIsAlphaUnchanged() const {
    const bool unchanged = (*fChild).isAlphaUnchanged();
    return unchanged;
}

bool SkWorkingFormatColorFilter::onAsAColorMode(SkColor* color, SkBlendMode* mode) const {
    const bool represented = (*fChild).asAColorMode(color, mode);
    return represented;
}

bool SkWorkingFormatColorFilter::onAsAColorMatrix(float matrix[20]) const {
    const bool represented = (*fChild).asAColorMatrix(matrix);
    return represented;
}

void SkWorkingFormatColorFilter::flatten(SkWriteBuffer& buffer) const {
    buffer.writeFlattenable(fChild.get());
    fWorkingFormatCalculator.flatten(buffer);
}

sk_sp<SkFlattenable> SkWorkingFormatColorFilter::CreateProc(SkReadBuffer& buffer) {
    sk_sp<SkColorFilter> child = buffer.readColorFilter();
    const bool useDstTF = buffer.readBool();
    const bool useDstGamut = buffer.readBool();
    const bool useDstAT = buffer.readBool();
    skcms_TransferFunction tf{};
    skcms_Matrix3x3 gamut{};
    SkAlphaType at = kPremul_SkAlphaType;

    // The wire arrays contain seven named transfer coefficients and a row-major
    // 3x3 gamut. Do not traverse struct members or separate rows as one C array.
    if (!useDstTF) {
        std::array<SkScalar, 7> coefficients{};
        if (!buffer.readScalarArray(coefficients)) return nullptr;
        tf = {coefficients[0], coefficients[1], coefficients[2], coefficients[3],
              coefficients[4], coefficients[5], coefficients[6]};
    }
    if (!useDstGamut) {
        std::array<SkScalar, 9> coefficients{};
        if (!buffer.readScalarArray(coefficients)) return nullptr;
        for (size_t row = 0; row < 3; ++row) {
            for (size_t column = 0; column < 3; ++column) {
                gamut.vals[row][column] = coefficients[row * 3 + column];
            }
        }
    }
    if (!useDstAT) {
        at = buffer.read32LE(kLastEnum_SkAlphaType);
    }

    if (!buffer.isValid()) return nullptr;
    sk_sp<SkFlattenable> filter = SkColorFilterPriv::WithWorkingFormat(
            std::move(child), useDstTF ? nullptr : &tf,
            useDstGamut ? nullptr : &gamut, useDstAT ? nullptr : &at);
    return filter;
}

sk_sp<SkColorFilter> SkColorFilterPriv::WithWorkingFormat(sk_sp<SkColorFilter> child,
                                                          const skcms_TransferFunction* tf,
                                                          const skcms_Matrix3x3* gamut,
                                                          const SkAlphaType* at) {
    if (!child) {
        // This color filter applies a conversion from the 'dst' color space to the working format,
        // invokes the child, and then converts back to 'dst'. If `child` is null, it is the
        // identity color filter, so the conversion from 'dst' to working format and back to 'dst'
        // is also the identity.
        return nullptr;
    }
    sk_sp<SkColorFilter> filter = sk_make_sp<SkWorkingFormatColorFilter>(std::move(child), tf, gamut, at);
    return filter;
}


SkWorkingFormatCalculator::SkWorkingFormatCalculator(const skcms_TransferFunction* tf,
                                                     const skcms_Matrix3x3* gamut,
                                                     const SkAlphaType* at) {
    if (tf) {
        fTF = *tf;
        fUseDstTF = false;
    }
    if (gamut) {
        fGamut = *gamut;
        fUseDstGamut = false;
    }
    if (at) {
        fAT = *at;
        fUseDstAT = false;
    }
}

sk_sp<SkColorSpace> SkWorkingFormatCalculator::workingFormat(const sk_sp<SkColorSpace>& dstCS,
                                                             SkAlphaType* outAT) const {
    skcms_TransferFunction tf{};
    skcms_Matrix3x3 gamut{};

    if (fUseDstTF) {
        SkAssertResult((*dstCS).isNumericalTransferFn(&tf));
    } else {
        tf = fTF;
    }
    if (fUseDstGamut) {
        SkAssertResult((*dstCS).toXYZD50(&gamut));
    } else {
        gamut = fGamut;
    }
    *outAT = fUseDstAT ? kPremul_SkAlphaType : fAT;

    sk_sp<SkColorSpace> colorSpace = SkColorSpace::MakeRGB(tf, gamut);
    return colorSpace;
}

void SkWorkingFormatCalculator::flatten(SkWriteBuffer& buffer) const {
    buffer.writeBool(fUseDstTF);
    buffer.writeBool(fUseDstGamut);
    buffer.writeBool(fUseDstAT);
    if (!fUseDstTF) {
        const std::array<SkScalar, 7> coefficients{
            fTF.g, fTF.a, fTF.b, fTF.c, fTF.d, fTF.e, fTF.f};
        buffer.writeScalarArray(coefficients);
    }
    if (!fUseDstGamut) {
        std::array<SkScalar, 9> coefficients{};
        for (size_t row = 0; row < 3; ++row) {
            for (size_t column = 0; column < 3; ++column) {
                coefficients[row * 3 + column] = fGamut.vals[row][column];
            }
        }
        buffer.writeScalarArray(coefficients);
    }
    if (!fUseDstAT) {
        buffer.writeInt(fAT);
    }
}
