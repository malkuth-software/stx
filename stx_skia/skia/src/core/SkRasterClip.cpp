/*
 * Copyright 2010 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkRasterClip.h"

#include "include/core/SkBlendMode.h"
#include "include/core/SkClipOp.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPath.h"
#include "include/core/SkScalar.h"
#include "include/private/SkDebug.h"
#include "src/core/SkRegionPriv.h"
#include "src/shaders/SkLocalMatrixShader.h"

#include <utility>

class SkBlitter;

// Clip operators convert to region operators by value.
static_assert(static_cast<int>(SkClipOp::kDifference) == SkRegion::kDifference_Op);
static_assert(static_cast<int>(SkClipOp::kIntersect) == SkRegion::kIntersect_Op);

static SkRegion::Op to_region_op(SkClipOp op) {
    const SkRegion::Op regionOp = static_cast<SkRegion::Op>(op);
    return regionOp;
}

SkRasterClip::SkRasterClip(const SkRasterClip& that)
        : fIsBW(that.fIsBW)
        , fIsEmpty(that.fIsEmpty)
        , fIsRect(that.fIsRect)
        , fShader(that.fShader)
{
    const SkAutoRasterClipValidate validateThat(that);

    if (fIsBW) {
        fBW = that.fBW;
    } else {
        fAA = that.fAA;
    }

    (*this).validate();
}

SkRasterClip& SkRasterClip::operator=(const SkRasterClip& that) {
    const SkAutoRasterClipValidate validateThat(that);

    // Copy the active representation and release the one this clip no longer uses.
    fIsBW = that.fIsBW;
    if (fIsBW) {
        fBW = that.fBW;
        fAA.setEmpty();
    } else {
        fAA = that.fAA;
        fBW.setEmpty();
    }

    fIsEmpty = that.isEmpty();
    fIsRect = that.isRect();
    fShader = that.fShader;
    (*this).validate();
    return *this;
}

SkRasterClip::SkRasterClip(const SkRegion& rgn) : fBW(rgn) {
    fIsBW = true;
    fIsEmpty = (*this).computeIsEmpty();  // bounds might be empty, so compute
    fIsRect = !fIsEmpty;
    (*this).validate();
}

SkRasterClip::SkRasterClip(const SkIRect& bounds) : fBW(bounds) {
    fIsBW = true;
    fIsEmpty = (*this).computeIsEmpty();  // bounds might be empty, so compute
    fIsRect = !fIsEmpty;
    (*this).validate();
}

SkRasterClip::SkRasterClip() {
    (*this).validate();
}

SkRasterClip::SkRasterClip(const SkPath& path, const SkIRect& bounds, bool doAA) {
    if (doAA) {
        fIsBW = false;
        fAA.setPath(path, bounds, true);
    } else {
        fIsBW = true;
        fBW.setPath(path, SkRegion(bounds));
    }
    fIsEmpty = (*this).computeIsEmpty();  // bounds might be empty, so compute
    fIsRect = (*this).computeIsRect();
    (*this).validate();
}

SkRasterClip::~SkRasterClip() {
    (*this).validate();
}

bool SkRasterClip::setEmpty() {
    const SkAutoRasterClipValidate validateOnExit(*this);

    fShader.reset();
    fIsBW = true;
    fBW.setEmpty();
    fAA.setEmpty();
    fIsEmpty = true;
    fIsRect = false;
    return false;
}

bool SkRasterClip::setRect(const SkIRect& rect) {
    const SkAutoRasterClipValidate validateOnExit(*this);

    fShader.reset();
    fIsBW = true;
    fAA.setEmpty();
    fIsRect = fBW.setRect(rect);
    fIsEmpty = !fIsRect;
    return fIsRect;
}

/////////////////////////////////////////////////////////////////////////////////////

bool SkRasterClip::op(const SkIRect& rect, SkClipOp op) {
    const SkAutoRasterClipValidate validateOnExit(*this);

    if (fIsBW) {
        fBW.op(rect, to_region_op(op));
    } else {
        fAA.op(rect, op);
    }
    const bool nonEmpty = (*this).updateCacheAndReturnNonEmpty();
    return nonEmpty;
}

bool SkRasterClip::op(const SkRegion& rgn, SkClipOp op) {
    const SkAutoRasterClipValidate validateOnExit(*this);

    if (fIsBW) {
        (void)fBW.op(rgn, to_region_op(op));
    } else {
        SkAAClip tmp;
        tmp.setRegion(rgn);
        (void)fAA.op(tmp, op);
    }
    const bool nonEmpty = (*this).updateCacheAndReturnNonEmpty();
    return nonEmpty;
}

/**
 *  Our antialiasing currently has a granularity of 1/4 of a pixel along each
 *  axis. Thus we can treat an axis coordinate as an integer if it differs
 *  from its nearest int by < half of that value (1/8 in this case).
 */
static bool nearly_integral(SkScalar x) {
    constexpr SkScalar domain = SK_Scalar1 / 4;
    constexpr SkScalar halfDomain = domain / 2;

    const SkScalar shifted = x + halfDomain;
    const bool integral = shifted - SkScalarFloorToScalar(shifted) < domain;
    return integral;
}

bool SkRasterClip::op(const SkRect& localRect, const SkMatrix& matrix, SkClipOp op, bool doAA) {
    const SkAutoRasterClipValidate validateOnExit(*this);

    const bool isScaleTrans = matrix.isScaleTranslate();
    if (!isScaleTrans) {
        const bool nonEmpty = (*this).op(SkPath::Rect(localRect), matrix, op, doAA);
        return nonEmpty;
    }

    const SkRect devRect = matrix.mapRect(localRect);
    bool useAA = doAA;
    if (fIsBW && useAA) {
        // check that the rect really needs aa, or is it close enought to
        // integer boundaries that we can just treat it as a BW rect?
        if (nearly_integral(devRect.fLeft) && nearly_integral(devRect.fTop) &&
            nearly_integral(devRect.fRight) && nearly_integral(devRect.fBottom)) {
            useAA = false;
        }
    }

    if (fIsBW && !useAA) {
        (void)fBW.op(devRect.round(), to_region_op(op));
    } else {
        if (fIsBW) {
            (*this).convertToAA();
        }
        (void)fAA.op(devRect, op, useAA);
    }
    const bool nonEmpty = (*this).updateCacheAndReturnNonEmpty();
    return nonEmpty;
}

bool SkRasterClip::op(const SkRRect& rrect, const SkMatrix& matrix, SkClipOp op, bool doAA) {
    const bool nonEmpty = (*this).op(SkPath::RRect(rrect), matrix, op, doAA);
    return nonEmpty;
}

bool SkRasterClip::op(const SkPath& path, const SkMatrix& matrix, SkClipOp op, bool doAA) {
    const SkAutoRasterClipValidate validateOnExit(*this);

    const SkPath devPath = path.makeTransform(matrix);

    // Since op is either intersect or difference, the clip is always shrinking; that means we can
    // always use our current bounds as the limiting factor for region/aaclip operations.
    if ((*this).isRect() && op == SkClipOp::kIntersect) {
        // However, in the relatively common case of intersecting a new path with a rectangular
        // clip, it's faster to convert the path into a region/aa-mask in place than evaluate the
        // actual intersection. See skbug.com/40043482
        if (doAA && fIsBW) {
            (*this).convertToAA();
        }
        // Copy the bounds: setting the path replaces the representation they belong to.
        const SkIRect bounds = (*this).getBounds();
        if (fIsBW) {
            fBW.setPath(devPath, SkRegion(bounds));
        } else {
            fAA.setPath(devPath, bounds, doAA);
        }
        const bool nonEmpty = (*this).updateCacheAndReturnNonEmpty();
        return nonEmpty;
    }
    const SkRasterClip pathClip(devPath, (*this).getBounds(), doAA);
    const bool nonEmpty = (*this).op(pathClip, op);
    return nonEmpty;
}

bool SkRasterClip::op(sk_sp<SkShader> sh) {
    const SkAutoRasterClipValidate validateOnExit(*this);

    // A clip can only shrink. A null shader restricts nothing; composing it with an earlier
    // shader clip would yield null and erase that clip.
    if (!sh) {
        const bool nonEmpty = !(*this).isEmpty();
        return nonEmpty;
    }
    if (!fShader) {
        fShader = std::move(sh);
    } else {
        fShader = SkShaders::Blend(SkBlendMode::kSrcIn, std::move(sh), fShader);
    }
    const bool nonEmpty = !(*this).isEmpty();
    return nonEmpty;
}

bool SkRasterClip::op(const SkRasterClip& clip, SkClipOp op) {
    const SkAutoRasterClipValidate validateOnExit(*this);
    clip.validate();

    if ((*this).isBW() && clip.isBW()) {
        (void)fBW.op(clip.fBW, to_region_op(op));
    } else {
        SkAAClip tmp;
        // Borrows either tmp or the other clip's antialiased representation.
        const SkAAClip* other = nullptr;

        if ((*this).isBW()) {
            (*this).convertToAA();
        }
        if (clip.isBW()) {
            tmp.setRegion(clip.bwRgn());
            other = &tmp;
        } else {
            other = &clip.aaRgn();
        }
        (void)fAA.op(*other, op);
    }
    const bool nonEmpty = (*this).updateCacheAndReturnNonEmpty();
    return nonEmpty;
}

void SkRasterClip::translate(int dx, int dy, SkRasterClip* dst) const {
    if (nullptr == dst) {
        return;
    }

    const SkAutoRasterClipValidate validateOnExit(*this);

    if (0 == (dx | dy)) {
        *dst = *this;
        return;
    }

    if ((*this).isEmpty()) {
        (*dst).setEmpty();
        return;
    }

    const SkIRect sourceBounds = (*this).getBounds();
    (*dst).fIsBW = fIsBW;
    if (fIsBW) {
        fBW.translate(dx, dy, &(*dst).fBW);
        (*dst).fAA.setEmpty();
    } else {
        fAA.translate(dx, dy, &(*dst).fAA);
        (*dst).fBW.setEmpty();
    }
    (*dst).updateCacheAndReturnNonEmpty();

    // Geometry may clamp a requested offset near the coordinate limits. The
    // shader must receive the offset that the clip actually applied.
    const SkIRect destinationBounds = (*dst).getBounds();
    const int appliedDx = destinationBounds.fLeft - sourceBounds.fLeft;
    const int appliedDy = destinationBounds.fTop - sourceBounds.fTop;
    if (fShader) {
        const SkPoint offset = {-static_cast<float>(appliedDx),
                                -static_cast<float>(appliedDy)};
        sk_sp<SkShader> translated(new SkCTMShader(fShader, SkMatrix::I(), offset));
        (*dst).fShader = std::move(translated);
    } else {
        (*dst).fShader.reset();
    }
}

void SkRasterClip::convertToAA() {
    const SkAutoRasterClipValidate validateOnExit(*this);

    SkASSERT(fIsBW);
    fAA.setRegion(fBW);
    fIsBW = false;
    // The region is no longer the active representation.
    fBW.setEmpty();

    // since we are being explicitly asked to convert-to-aa, we pass false so we don't "optimize"
    // ourselves back to BW.
    (void)(*this).updateCacheAndReturnNonEmpty(false);
}

#ifdef SK_DEBUG
void SkRasterClip::validate() const {
    if (fIsBW) {
        SkASSERT(fAA.isEmpty());
    }

    SkRegionPriv::Validate(fBW);
    fAA.validate();

    SkASSERT((*this).computeIsEmpty() == fIsEmpty);
    SkASSERT((*this).computeIsRect() == fIsRect);
}
#endif

///////////////////////////////////////////////////////////////////////////////

SkAAClipBlitterWrapper::SkAAClipBlitterWrapper() = default;

SkAAClipBlitterWrapper::SkAAClipBlitterWrapper(const SkRasterClip& clip,
                                               SkBlitter* blitter) {
    (*this).init(clip, blitter);
}

SkAAClipBlitterWrapper::SkAAClipBlitterWrapper(const SkAAClip* aaclip,
                                               SkBlitter* blitter) {
    SkASSERT(blitter);
    SkASSERT(aaclip);
    fBWRgn.setRect((*aaclip).getBounds());
    fAABlitter.init(blitter, aaclip);
    // now our return values
    fClipRgn = &fBWRgn;
    fBlitter = &fAABlitter;
}

void SkAAClipBlitterWrapper::init(const SkRasterClip& clip, SkBlitter* blitter) {
    SkASSERT(blitter);
    if (clip.isBW()) {
        fClipRgn = &clip.bwRgn();
        fBlitter = blitter;
    } else {
        const SkAAClip& aaclip = clip.aaRgn();
        fBWRgn.setRect(aaclip.getBounds());
        fAABlitter.init(blitter, &aaclip);
        // now our return values
        fClipRgn = &fBWRgn;
        fBlitter = &fAABlitter;
    }
}
