/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef SkWorkingFormatColorFilter_DEFINED
#define SkWorkingFormatColorFilter_DEFINED

#include "include/core/SkAlphaType.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkFlattenable.h"
#include "include/core/SkRefCnt.h"
#include "modules/skcms/skcms.h"
#include "src/core/SkColorData.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"

class SkColorSpace;
class SkReadBuffer;
class SkWriteBuffer;
enum SkAlphaType : int;
enum class SkBlendMode;
struct SkStageRec;

class SkWorkingFormatCalculator {
public:
    SkWorkingFormatCalculator(const skcms_TransferFunction* tf,
                              const skcms_Matrix3x3* gamut,
                              const SkAlphaType* at);

    sk_sp<SkColorSpace> workingFormat(const sk_sp<SkColorSpace>& dstCS, SkAlphaType* outAT) const;

    void flatten(SkWriteBuffer& buffer) const;

private:
    skcms_TransferFunction fTF{};
    bool fUseDstTF = true;
    skcms_Matrix3x3 fGamut{};
    bool fUseDstGamut = true;
    SkAlphaType fAT = kPremul_SkAlphaType;
    bool fUseDstAT = true;
};

class SkWorkingFormatColorFilter final : public SkColorFilterBase {
public:
    SkWorkingFormatColorFilter(sk_sp<SkColorFilter> child,
                               const skcms_TransferFunction* tf,
                               const skcms_Matrix3x3* gamut,
                               const SkAlphaType* at);

    sk_sp<SkColorSpace> workingFormat(const sk_sp<SkColorSpace>& dstCS, SkAlphaType* outAT) const;

    SkColorFilterBase::Type type() const override {
        return SkColorFilterBase::Type::kWorkingFormat;
    }

    bool appendStages(const SkStageRec& rec, bool shaderIsOpaque) const override;

    SkPMColor4f onFilterColor4f(const SkPMColor4f& origColor,
                                SkColorSpace* rawDstCS) const override;

    bool onIsAlphaUnchanged() const override;

    sk_sp<SkColorFilter> child() const { return fChild; }

private:
    SK_FLATTENABLE_HOOKS(SkWorkingFormatColorFilter)

    void flatten(SkWriteBuffer& buffer) const override;

    // Queries describe the child operation, without folding in the surrounding
    // working-space conversions. Preserve that established query contract.
    bool onAsAColorMode(SkColor*, SkBlendMode*) const override;
    bool onAsAColorMatrix(float[20]) const override;

    sk_sp<SkColorFilter> fChild;
    SkWorkingFormatCalculator fWorkingFormatCalculator;
};

#endif
