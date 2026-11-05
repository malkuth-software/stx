/*
 * Copyright 2017 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRasterClipStack_DEFINED
#define SkRasterClipStack_DEFINED

#include "include/core/SkClipOp.h"
#include "src/core/SkRasterClip.h"
#include "src/core/SkScan.h"
#include "src/core/SkTBlockList.h"

#include <climits>

class SkRasterClipStack : SkNoncopyable {
public:
    // Sizes must not be negative, as for setNewSize().
    SkRasterClipStack(int width, int height)
            : fRootBounds(SkIRect::MakeWH(width, height))
            , fDisableAA(SkScan::PathRequiresTiling(fRootBounds)) {
        SkASSERT_RELEASE(width >= 0 && height >= 0);
        fStack.emplace_back(SkRasterClip(fRootBounds));
        SkASSERT(fStack.count() == 1);
    }

    void setNewSize(int w, int h) {
        SkASSERT_RELEASE(w >= 0 && h >= 0);
        SkASSERT_RELEASE(fStack.count() == 1);
        Rec& rec = fStack.back();
        SkASSERT_RELEASE(rec.fDeferredCount == 0);

        const SkIRect newBounds = SkIRect::MakeWH(w, h);
        const bool disableAA = SkScan::PathRequiresTiling(newBounds);
        fRootBounds = newBounds;
        fDisableAA = disableAA;
        rec.fRC.setRect(newBounds);
    }

    // Borrows the current clip; valid until the next save, restore, clip or resize.
    const SkRasterClip& rc() const& { return fStack.back().fRC; }
    const SkRasterClip& rc() const&& = delete;

    // Saves that have not changed the clip are counted on the current record; a count that would
    // overflow stops the program in every build.
    void save() {
        SkASSERT(fStack.back().fDeferredCount >= 0);
        SkASSERT_RELEASE(fStack.back().fDeferredCount < INT_MAX);
        fStack.back().fDeferredCount += 1;
    }

    // Every restore must match an earlier save. An unmatched restore would pop the root record
    // and leave the stack empty, so it stops the program in every build.
    void restore() {
        Rec& rec = fStack.back();
        if (rec.fDeferredCount > 0) {
            rec.fDeferredCount -= 1;
            return;
        }
        SkASSERT_RELEASE(fStack.count() > 1);
        fStack.pop_back();
    }

    void clipRect(const SkMatrix& ctm, const SkRect& rect, SkClipOp op, bool aa) {
        (*this).writable_rc().op(rect, ctm, op, (*this).finalAA(aa));
        (*this).validate();
    }

    void clipRRect(const SkMatrix& ctm, const SkRRect& rrect, SkClipOp op, bool aa) {
        (*this).writable_rc().op(rrect, ctm, op, (*this).finalAA(aa));
        (*this).validate();
    }

    void clipPath(const SkMatrix& ctm, const SkPath& path, SkClipOp op, bool aa) {
        (*this).writable_rc().op(path, ctm, op, (*this).finalAA(aa));
        (*this).validate();
    }

    void clipShader(sk_sp<SkShader> sh) {
        (*this).writable_rc().op(std::move(sh));
        (*this).validate();
    }

    void clipRegion(const SkRegion& rgn, SkClipOp op) {
        (*this).writable_rc().op(rgn, op);
        (*this).validate();
    }

    void replaceClip(const SkIRect& rect) {
        SkIRect devRect = rect;
        if (!devRect.intersect(fRootBounds)) {
            (*this).writable_rc().setEmpty();
        } else {
            (*this).writable_rc().setRect(devRect);
        }
        (*this).validate();
    }

    void validate() const {
#ifdef SK_DEBUG
        const SkRasterClip& clip = (*this).rc();
        if (fRootBounds.isEmpty()) {
            SkASSERT(clip.isEmpty());
        } else if (!clip.isEmpty()) {
            SkASSERT(fRootBounds.contains(clip.getBounds()));
        }
#endif
    }

private:
    // One saved clip. Saves that have not yet changed the clip are counted instead of copied.
    struct Rec {
        SkRasterClip fRC;
        int          fDeferredCount = 0; // 0 for a "normal" entry

        explicit Rec(const SkRasterClip& rc) : fRC(rc) {}
    };

    SkTBlockList<Rec, 16> fStack;
    SkIRect fRootBounds = {0, 0, 0, 0};
    bool fDisableAA = false;

    SkRasterClip& writable_rc() {
        SkASSERT(fStack.back().fDeferredCount >= 0);
        if (fStack.back().fDeferredCount > 0) {
            fStack.back().fDeferredCount -= 1;
            fStack.emplace_back(fStack.back().fRC);
        }
        return fStack.back().fRC;
    }

    bool finalAA(bool aa) const {
        const bool useAA = aa && !fDisableAA;
        return useAA;
    }
};

#endif
