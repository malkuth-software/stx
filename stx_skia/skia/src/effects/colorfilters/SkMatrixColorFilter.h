/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkMatrixColorFilter_DEFINED
#define SkMatrixColorFilter_DEFINED

#include "include/core/SkColorFilter.h"
#include "include/core/SkFlattenable.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"

#include <array>
#include <cstdint>

class SkReadBuffer;
class SkWriteBuffer;
struct SkStageRec;

class SkMatrixColorFilter final : public SkColorFilterBase {
public:
    enum class Domain : uint8_t { kRGBA, kHSLA };
    using Clamp = SkColorFilters::Clamp;

    explicit SkMatrixColorFilter(const float array[20], Domain, Clamp);

    bool appendStages(const SkStageRec& rec, bool shaderIsOpaque) const override;

    bool onIsAlphaUnchanged() const override { return fAlphaIsUnchanged; }

    SkColorFilterBase::Type type() const override { return SkColorFilterBase::Type::kMatrix; }

    Domain domain() const { return fDomain; }
    SkColorFilters::Clamp clamp() const { return fClamp; }
    // Pipeline contexts borrow these 20 coefficients; retain the filter through execution.
    const float* matrix() const & {
        const float* coefficients = fMatrix.data();
        return coefficients;
    }
    const float* matrix() const && = delete;

private:
    SK_FLATTENABLE_HOOKS(SkMatrixColorFilter)

    void flatten(SkWriteBuffer&) const override;
    bool onAsAColorMatrix(float matrix[20]) const override;

    std::array<float, 20> fMatrix; // Four RGBA rows, each with a translation coefficient.
    bool fAlphaIsUnchanged;
    Domain fDomain;
    Clamp fClamp;
};

#endif
