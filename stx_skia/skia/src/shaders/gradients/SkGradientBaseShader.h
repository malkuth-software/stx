/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkGradientShaderPriv_DEFINED
#define SkGradientShaderPriv_DEFINED

#include "include/core/SkBitmap.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/effects/SkGradient.h"
#include "include/private/SkAssert.h"
#include "include/private/SkTArray.h"
#include "include/private/SkTemplates.h"
#include "src/core/SkColorData.h"
#include "src/shaders/SkShaderBase.h"

#include <cstddef>
#include <cstdint>

class SkArenaAlloc;
class SkRasterPipeline;
class SkReadBuffer;
class SkShader;
class SkWriteBuffer;
enum class SkTileMode;
struct SkStageRec;

class SkGradientScope {
public:
    // Returned stop spans borrow this scope until its next unflatten or destruction.
    std::optional<SkGradient> unflatten(SkReadBuffer&, SkMatrix* legacyLocalMatrix) &;
    std::optional<SkGradient> unflatten(SkReadBuffer&, SkMatrix*) && = delete;
private:
    skia_private::STArray<16, SkColor4f> fColorStorage{};
    skia_private::STArray<16, SkScalar> fPositionStorage{};
};

class SkGradientBaseShader : public SkShaderBase {
public:
    using Interpolation = SkGradient::Interpolation;

    SkGradientBaseShader(const SkGradient&, const SkMatrix& ptsToUnit);
    ~SkGradientBaseShader() override;

    ShaderType type() const final { return ShaderType::kGradientBase; }

    bool isOpaque() const override;

    bool interpolateInPremul() const {
        const bool premul = fInterpolation.fInPremul == Interpolation::InPremul::kYes;
        return premul;
    }

    const SkMatrix& getGradientMatrix() const & { return fPtsToUnit; }
    const SkMatrix& getGradientMatrix() const && = delete;
    size_t getColorCount() const { return fColorCount; }
    const float* getPositions() const & { return fPositions; }
    const float* getPositions() const && = delete;

    const Interpolation& getInterpolation() const & { return fInterpolation; }
    const Interpolation& getInterpolation() const && = delete;

    static bool ValidGradient(SkSpan<const SkColor4f>,
                              SkTileMode tileMode,
                              const Interpolation& interpolation);

    static sk_sp<SkShader> MakeDegenerateGradient(const SkGradient::Colors&);

    // The default SkScalarNearlyZero threshold of .0024 is too big and causes regressions for svg
    // gradients defined in the wild.
    static constexpr SkScalar kDegenerateThreshold = SK_Scalar1 / (1 << 15);

protected:
    void flatten(SkWriteBuffer&) const override;

    void commonAsAGradient(GradientInfo*) const;

    bool onAsLuminanceColor(SkColor4f*) const override;

    bool appendStages(const SkStageRec&, const SkShaders::MatrixRec&) const override;

    virtual void appendGradientStages(SkArenaAlloc* alloc,
                                      SkRasterPipeline* tPipeline,
                                      SkRasterPipeline* postPipeline) const = 0;

    const SkMatrix fPtsToUnit;
    SkTileMode fTileMode = SkTileMode::kClamp;

public:
    static void AppendGradientFillStages(SkRasterPipeline* p,
                                         SkArenaAlloc* alloc,
                                         const SkPMColor4f* colors,
                                         const SkScalar* positions,
                                         int count);

    static void AppendInterpolatedToDstStages(SkRasterPipeline* p,
                                              SkArenaAlloc* alloc,
                                              bool colorsAreOpaque,
                                              const Interpolation& interpolation,
                                              const SkColorSpace* intermediateColorSpace,
                                              const SkColorSpace* dstColorSpace);

    float getPos(size_t i) const {
        SkASSERT_RELEASE(i < static_cast<size_t>(fColorCount));
        const float position = fPositions ? fPositions[i] : SkIntToScalar(i) / (fColorCount - 1);
        return position;
    }

    SkColor getLegacyColor(size_t i) const {
        SkASSERT_RELEASE(i < static_cast<size_t>(fColorCount));
        const SkColor color = fColors[i].toSkColor();
        return color;
    }

    SkSpan<const SkColor4f> colors() const & {
        const SkSpan<const SkColor4f> view(fColors, fColorCount);
        return view;
    }
    SkSpan<const SkColor4f> colors() const && = delete;
    SkSpan<const float> positions() const & {
        const size_t count = fPositions ? fColorCount : 0;
        const SkSpan<const float> view(fPositions, count);
        return view;
    }
    SkSpan<const float> positions() const && = delete;

    bool firstStopIsImplicit() const { return fFirstStopIsImplicit; }
    bool lastStopIsImplicit() const { return fLastStopIsImplicit; }

    const sk_sp<SkColorSpace>& colorSpace() const & { return fColorSpace; }
    const sk_sp<SkColorSpace>& colorSpace() const && = delete;
    const Interpolation& interpolation() const & { return fInterpolation; }
    const Interpolation& interpolation() const && = delete;

    bool colorsAreOpaque() const { return fColorsAreOpaque; }

    SkTileMode getTileMode() const { return fTileMode; }

private:
    // Reserve inline space for up to 4 stops.
    inline static constexpr size_t kInlineStopCount = 4;
    inline static constexpr size_t kInlineStorageSize =
            (sizeof(SkColor4f) + sizeof(SkScalar)) * kInlineStopCount;
    skia_private::AutoSTMalloc<kInlineStorageSize, uint8_t> fStorage;

    SkColor4f* fColors = nullptr;               // points into fStorage
    SkScalar* fPositions = nullptr;             // points into fStorage, or nullptr
    sk_sp<SkColorSpace> fColorSpace{};  // color space of gradient stops
    Interpolation fInterpolation{};
    size_t fColorCount = 0;               // length of fColors (and fPositions, if not nullptr)
    bool fFirstStopIsImplicit = false;
    bool fLastStopIsImplicit = false;

    bool fColorsAreOpaque = true;
};

///////////////////////////////////////////////////////////////////////////////

struct SkColor4fXformer {
    // Original positions borrow shader; replacement positions belong to this transform.
    // The caller keeps shader alive while using fPositions. Pipeline setup copies the values.
    SkColor4fXformer(const SkGradientBaseShader* shader, SkColorSpace* dst);
    SkColor4fXformer(const SkColor4fXformer&) = delete;
    SkColor4fXformer& operator=(const SkColor4fXformer&) = delete;
    SkColor4fXformer(SkColor4fXformer&&) = delete;
    SkColor4fXformer& operator=(SkColor4fXformer&&) = delete;

    using ColorStorage = skia_private::STArray<4, SkPMColor4f>;
    using PositionStorage = skia_private::STArray<4, float>;

    ColorStorage fColors{};
    PositionStorage fPositionStorage{};
    const float* fPositions = nullptr;
    sk_sp<SkColorSpace> fIntermediateColorSpace{};
};


#endif
