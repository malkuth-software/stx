/*
 * Copyright 2010 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRasterClip_DEFINED
#define SkRasterClip_DEFINED

#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkRegion.h"
#include "include/core/SkShader.h"
#include "include/private/SkAssert.h"
#include "include/private/SkDebug.h"
#include "include/private/SkNoncopyable.h"
#include "src/core/SkAAClip.h"

class SkBlitter;
class SkMatrix;
class SkPath;
class SkRRect;
enum class SkClipOp;

/**
 *  Wraps a SkRegion and SkAAClip, so we have a single object that can represent either our
 *  BW or antialiased clips.
 */
class SkRasterClip {
public:
    SkRasterClip();
    explicit SkRasterClip(const SkIRect&);
    explicit SkRasterClip(const SkRegion&);
    explicit SkRasterClip(const SkRasterClip&);
    SkRasterClip(const SkPath& path, const SkIRect& bounds, bool doAA);

    ~SkRasterClip();

    SkRasterClip& operator=(const SkRasterClip&);

    bool isBW() const { return fIsBW; }
    bool isAA() const { return !fIsBW; }
    // Borrow the active representation; valid while this clip is alive and unchanged.
    const SkRegion& bwRgn() const& { SkASSERT(fIsBW); return fBW; }
    const SkRegion& bwRgn() const&& = delete;
    const SkAAClip& aaRgn() const& { SkASSERT(!fIsBW); return fAA; }
    const SkAAClip& aaRgn() const&& = delete;

    bool isEmpty() const {
        SkASSERT((*this).computeIsEmpty() == fIsEmpty);
        return fIsEmpty;
    }

    bool isRect() const {
        SkASSERT((*this).computeIsRect() == fIsRect);
        return fIsRect;
    }

    bool isComplex() const {
        if (fIsBW) {
            const bool complex = fBW.isComplex();
            return complex;
        }
        const bool complex = !fAA.isEmpty();
        return complex;
    }
    // Borrows the active representation's bounds; valid while this clip is alive and unchanged.
    const SkIRect& getBounds() const& {
        if (fIsBW) {
            return fBW.getBounds();
        }
        return fAA.getBounds();
    }
    const SkIRect& getBounds() const&& = delete;

    bool setEmpty();
    bool setRect(const SkIRect&);

    bool op(const SkIRect&, SkClipOp);
    bool op(const SkRegion&, SkClipOp);
    bool op(const SkRect&, const SkMatrix& matrix, SkClipOp, bool doAA);
    bool op(const SkRRect&, const SkMatrix& matrix, SkClipOp, bool doAA);
    bool op(const SkPath&, const SkMatrix& matrix, SkClipOp, bool doAA);
    bool op(sk_sp<SkShader>);

    void translate(int dx, int dy, SkRasterClip* dst) const;

    bool quickContains(const SkIRect& rect) const {
        if (fIsBW) {
            const bool contained = fBW.quickContains(rect);
            return contained;
        }
        const bool contained = fAA.quickContains(rect);
        return contained;
    }

    /**
     *  Return true if this region is empty, or if the specified rectangle does
     *  not intersect the region. Returning false is not a guarantee that they
     *  intersect, but returning true is a guarantee that they do not.
     */
    bool quickReject(const SkIRect& rect) const {
        const bool rejected = !SkIRect::Intersects((*this).getBounds(), rect);
        return rejected;
    }

#ifdef SK_DEBUG
    void validate() const;
#else
    void validate() const {}
#endif

    sk_sp<SkShader> clipShader() const { return fShader; }

private:
    // Exactly one representation is active, selected by fIsBW; the other is kept empty.
    SkRegion    fBW;
    SkAAClip    fAA;
    bool        fIsBW = true;
    // these 2 are caches based on querying the right obj based on fIsBW
    bool        fIsEmpty = true;
    bool        fIsRect = false;
    // if present, this augments the clip, not replaces it
    sk_sp<SkShader> fShader;

    bool computeIsEmpty() const {
        if (fIsBW) {
            const bool empty = fBW.isEmpty();
            return empty;
        }
        const bool empty = fAA.isEmpty();
        return empty;
    }

    bool computeIsRect() const {
        if (fIsBW) {
            const bool rect = fBW.isRect();
            return rect;
        }
        const bool rect = fAA.isRect();
        return rect;
    }

    bool updateCacheAndReturnNonEmpty(bool detectAARect = true) {
        fIsEmpty = (*this).computeIsEmpty();

        // detect that our computed AA is really just a (hard-edged) rect
        if (detectAARect && !fIsEmpty && !fIsBW && fAA.isRect()) {
            fBW.setRect(fAA.getBounds());
            fAA.setEmpty(); // don't need this anymore
            fIsBW = true;
        }

        fIsRect = (*this).computeIsRect();
        return !fIsEmpty;
    }

    void convertToAA();

    bool op(const SkRasterClip&, SkClipOp);
};

// Validates the clip on construction and again on destruction. validate() is empty in Release.
class [[nodiscard]] SkAutoRasterClipValidate : SkNoncopyable {
public:
    explicit SkAutoRasterClipValidate(const SkRasterClip& rc) : fRC(rc) {
        fRC.validate();
    }
    ~SkAutoRasterClipValidate() {
        fRC.validate();
    }
private:
    // Borrowed: the clip outlives this validator.
    const SkRasterClip& fRC;
};

///////////////////////////////////////////////////////////////////////////////

/**
 *  Encapsulates the logic of deciding if we need to change/wrap the blitter
 *  for aaclipping. If so, getRgn and getBlitter return modified values. If
 *  not, they return the raw blitter and (bw) clip region.
 *
 *  We need to keep the constructor/destructor cost as small as possible, so we
 *  can freely put this on the stack, and not pay too much for the case when
 *  we're really BW anyways.
 */
class SkAAClipBlitterWrapper {
public:
    SkAAClipBlitterWrapper();
    SkAAClipBlitterWrapper(const SkRasterClip&, SkBlitter*);
    SkAAClipBlitterWrapper(const SkAAClip*, SkBlitter*);

    SkAAClipBlitterWrapper(const SkAAClipBlitterWrapper&) = delete;
    SkAAClipBlitterWrapper& operator=(const SkAAClipBlitterWrapper&) = delete;

    void init(const SkRasterClip&, SkBlitter*);

    // The region and blitter are borrowed from this wrapper or from the clip and blitter passed
    // to init; they are valid while all three are alive.
    const SkIRect& getBounds() const& {
        SkASSERT(fClipRgn);
        return (*fClipRgn).getBounds();
    }
    const SkIRect& getBounds() const&& = delete;
    const SkRegion& getRgn() const& {
        SkASSERT(fClipRgn);
        return *fClipRgn;
    }
    const SkRegion& getRgn() const&& = delete;
    SkBlitter* getBlitter() & {
        SkASSERT(fBlitter);
        return fBlitter;
    }
    SkBlitter* getBlitter() && = delete;

private:
    SkRegion        fBWRgn;
    SkAAClipBlitter fAABlitter;
    // what we return
    const SkRegion* fClipRgn = nullptr;
    SkBlitter* fBlitter = nullptr;
};

#endif
