/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPaint.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkBlender.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkRect.h"
#include "include/core/SkScalar.h"
#include "include/core/SkShader.h"
#include "include/core/SkStrokeRec.h"
#include "include/private/SkTPin.h"
#include "include/private/SkTo.h"
#include "src/core/SkBlenderBase.h"
#include "src/core/SkColorSpacePriv.h"
#include "src/core/SkColorSpaceXformSteps.h"
#include "src/core/SkMaskFilterBase.h"
#include "src/core/SkPaintDefaults.h"
#include "src/core/SkPathEffectBase.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"

#include <cstdint>
#include <optional>
#include <utility>

SkPaint::SkPaint()
    : fColor4f{0, 0, 0, 1}  // opaque black
    , fWidth{0}
    , fMiterLimit{SkPaintDefaults_MiterLimit}
    , fBitfields{0u,                                            // fAntiAlias
                 0u,                                            // fDither
                 static_cast<unsigned>(SkPaint::kDefault_Cap),  // fCapType
                 static_cast<unsigned>(SkPaint::kDefault_Join), // fJoinType
                 static_cast<unsigned>(SkPaint::kFill_Style),   // fStyle
                 0u}                                            // fPadding
{
    static_assert(sizeof(Bitfields) == sizeof(uint32_t));
}

SkPaint::SkPaint(const SkColor4f& color, SkColorSpace* colorSpace) : SkPaint() {
    setColor(color, colorSpace);
}

SkPaint::SkPaint(const SkPaint& src) = default;

SkPaint::SkPaint(SkPaint&& src) = default;

SkPaint::~SkPaint() = default;

SkPaint& SkPaint::operator=(const SkPaint& src) = default;

SkPaint& SkPaint::operator=(SkPaint&& src) = default;

bool operator==(const SkPaint& a, const SkPaint& b) {
    const bool equal = a.fPathEffect == b.fPathEffect &&
                       a.fShader == b.fShader &&
                       a.fMaskFilter == b.fMaskFilter &&
                       a.fColorFilter == b.fColorFilter &&
                       a.fBlender == b.fBlender &&
                       a.fColor4f == b.fColor4f &&
                       a.fWidth == b.fWidth &&
                       a.fMiterLimit == b.fMiterLimit &&
                       a.fBitfields.fAntiAlias == b.fBitfields.fAntiAlias &&
                       a.fBitfields.fDither == b.fBitfields.fDither &&
                       a.fBitfields.fCapType == b.fBitfields.fCapType &&
                       a.fBitfields.fJoinType == b.fBitfields.fJoinType &&
                       a.fBitfields.fStyle == b.fBitfields.fStyle;
    return equal;
}

sk_sp<SkColorFilter> SkPaint::refColorFilter() const { return fColorFilter; }
sk_sp<SkBlender> SkPaint::refBlender() const { return fBlender; }
sk_sp<SkMaskFilter> SkPaint::refMaskFilter() const { return fMaskFilter; }
sk_sp<SkPathEffect> SkPaint::refPathEffect() const { return fPathEffect; }
sk_sp<SkShader> SkPaint::refShader() const { return fShader; }

void SkPaint::setColorFilter(sk_sp<SkColorFilter> f) { fColorFilter = std::move(f); }
void SkPaint::setMaskFilter(sk_sp<SkMaskFilter> f) { fMaskFilter = std::move(f); }
void SkPaint::setPathEffect(sk_sp<SkPathEffect> f) { fPathEffect = std::move(f); }
void SkPaint::setShader(sk_sp<SkShader> f) { fShader = std::move(f); }

///////////////////////////////////////////////////////////////////////////////

void SkPaint::reset() { *this = SkPaint(); }

// The enum setters keep the previous value when given a value outside their enumeration.
void SkPaint::setStyle(Style style) {
    if (static_cast<unsigned>(style) < kStyleCount) {
        fBitfields.fStyle = static_cast<unsigned>(style);
    }
}

void SkPaint::setStroke(bool isStroke) {
    fBitfields.fStyle = static_cast<unsigned>(isStroke ? kStroke_Style : kFill_Style);
}

void SkPaint::setColor(SkColor color) {
    fColor4f = SkColor4f::FromColor(color);
}

void SkPaint::setColor(const SkColor4f& color, SkColorSpace* colorSpace) {
    const SkColorSpaceXformSteps steps{colorSpace,          kUnpremul_SkAlphaType,
                                       sk_srgb_singleton(), kUnpremul_SkAlphaType};
    fColor4f = color.pinAlpha();
    steps.apply(fColor4f.vec());
}

void SkPaint::setAlphaf(float a) {
    fColor4f.fA = SkTPin(a, 0.0f, 1.0f);
}

void SkPaint::setARGB(U8CPU a, U8CPU r, U8CPU g, U8CPU b) {
    setColor(SkColorSetARGB(a, r, g, b));
}

// A null blender is source-over.
std::optional<SkBlendMode> SkPaint::asBlendMode() const {
    if (!fBlender) {
        const std::optional<SkBlendMode> srcOver = SkBlendMode::kSrcOver;
        return srcOver;
    }
    const std::optional<SkBlendMode> mode = (*as_BB(fBlender)).asBlendMode();
    return mode;
}

SkBlendMode SkPaint::getBlendMode_or(SkBlendMode defaultMode) const {
    const SkBlendMode mode = asBlendMode().value_or(defaultMode);
    return mode;
}

bool SkPaint::isSrcOver() const {
    const bool srcOver = !fBlender || (*as_BB(fBlender)).asBlendMode() == SkBlendMode::kSrcOver;
    return srcOver;
}

void SkPaint::setBlendMode(SkBlendMode mode) {
    sk_sp<SkBlender> blender = mode == SkBlendMode::kSrcOver ? nullptr : SkBlender::Mode(mode);
    setBlender(std::move(blender));
}

void SkPaint::setBlender(sk_sp<SkBlender> blender) {
    fBlender = std::move(blender);
}

// Negative (and NaN) widths and miter limits keep the previous value.
void SkPaint::setStrokeWidth(SkScalar width) {
    if (width >= 0) {
        fWidth = width;
    }
}

void SkPaint::setStrokeMiter(SkScalar limit) {
    if (limit >= 0) {
        fMiterLimit = limit;
    }
}

void SkPaint::setStrokeCap(Cap ct) {
    if (static_cast<unsigned>(ct) < kCapCount) {
        fBitfields.fCapType = static_cast<unsigned>(ct);
    }
}

void SkPaint::setStrokeJoin(Join jt) {
    if (static_cast<unsigned>(jt) < kJoinCount) {
        fBitfields.fJoinType = static_cast<unsigned>(jt);
    }
}

///////////////////////////////////////////////////////////////////////////////

bool SkPaint::canComputeFastBounds() const {
    // Pass nullptr for the bounds to determine if they can be computed
    if (getPathEffect() && !(*as_PEB(getPathEffect())).computeFastBounds(nullptr)) {
        return false;
    }
    return true;
}

const SkRect& SkPaint::computeFastBounds(const SkRect& orig, SkRect* storage) const {
    // Things like stroking, etc... will do math on the bounds rect, assuming that it's sorted.
    SkASSERT(orig.isSorted());
    const SkPaint::Style style = getStyle();
    // ultra fast-case: filling with no effects that affect geometry
    if (kFill_Style == style && !getMaskFilter() && !getPathEffect()) {
        return orig;
    }

    return doComputeFastBounds(orig, storage, style);
}

const SkRect& SkPaint::doComputeFastBounds(const SkRect& origSrc,
                                           SkRect* storage,
                                           Style style) const {
    // The documented preconditions: storage, and (via canComputeFastBounds) a path effect
    // that can bound its output. Ignoring a failed path effect would return bounds it never
    // computed, which can cull a visible draw.
    SkASSERT_RELEASE(storage != nullptr);

    const SkRect* src = &origSrc;

    SkRect tmpSrc = origSrc;
    if (getPathEffect()) {
        const bool bounded = (*as_PEB(getPathEffect())).computeFastBounds(&tmpSrc);
        SkASSERT_RELEASE(bounded);
        src = &tmpSrc;
    }

    const SkScalar radius = SkStrokeRec::GetInflationRadius(*this, style);
    *storage = (*src).makeOutset(radius, radius);

    if (getMaskFilter()) {
        (*as_MFB(getMaskFilter())).computeFastBounds(*storage, storage);
    }

    return *storage;
}

///////////////////////////////////////////////////////////////////////////////

// return true if the filter exists, and may affect alpha
static bool affects_alpha(const SkColorFilter* cf) {
    const bool affects = cf && !(*as_CFB(cf)).isAlphaUnchanged();
    return affects;
}

bool SkPaint::nothingToDraw() const {
    const std::optional<SkBlendMode> bm = asBlendMode();
    if (!bm) {
        return false;
    }
    switch (bm.value()) {
        case SkBlendMode::kSrcOver:
        case SkBlendMode::kSrcATop:
        case SkBlendMode::kDstOut:
        case SkBlendMode::kDstOver:
        case SkBlendMode::kPlus:
            if (0 == getAlpha()) {
                return !affects_alpha(fColorFilter.get());
            }
            break;
        case SkBlendMode::kDst:
            return true;
        default:
            break;
    }
    return false;
}
