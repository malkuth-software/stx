/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRegionPriv_DEFINED
#define SkRegionPriv_DEFINED

#include "include/core/SkRegion.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkMath.h"
#include "include/private/SkTo.h"

#include <atomic>
#include <cstring>
#include <new>

// Receives the spans of SkRegionPriv::VisitSpans. The visitor is borrowed for that call only.
class SkRegionSpanVisitor {
public:
    virtual void visitSpan(const SkIRect& span) = 0;

protected:
    ~SkRegionSpanVisitor() = default;
};

class SkRegionPriv {
public:
    inline static constexpr int kRunTypeSentinel = 0x7FFFFFFF;
    using RunType = SkRegion::RunType;
    using RunHead = SkRegion::RunHead;

    // Call the visitor with each span, in Y -> X ascending order.
    // We pass a rect, but we will still ensure the span Y->X ordering, so often the height
    // of the rect may be 1. It should never be empty.
    static void VisitSpans(const SkRegion& rgn, SkRegionSpanVisitor& visitor);

#ifdef SK_DEBUG
    static void Validate(const SkRegion& rgn);
#endif
};

static constexpr int SkRegion_kRunTypeSentinel = 0x7FFFFFFF;

inline bool SkRegionValueIsSentinel(int32_t value) {
    const bool sentinel = value == SkRegion_kRunTypeSentinel;
    return sentinel;
}

#ifdef SK_DEBUG
// Given the first interval (just past the interval-count), compute the
// interval count, by search for the x-sentinel
//
static int compute_intervalcount(const SkRegionPriv::RunType runs[]) {
    int index = 0;
    while (runs[index] < SkRegion_kRunTypeSentinel) {
        SkASSERT(runs[index] < runs[index + 1]);
        SkASSERT(runs[index + 1] < SkRegion_kRunTypeSentinel);
        index += 2;
    }
    const int intervals = index >> 1;
    return intervals;
}
#endif

/**
 *  Shared, reference-counted run storage of a complex region. One allocation holds this header
 *  followed by fRunCount run values; the header is constructed in place and the storage is
 *  released with sk_free when the last reference goes away.
 */
struct SkRegion::RunHead {
    std::atomic<int32_t> fRefCnt{1};
    int32_t fRunCount = 0;

    /**
     *  Number of spans with different Y values. This does not count the initial
     *  Top value, nor does it count the final Y-Sentinel value. In the logical
     *  case of a rectangle, this would return 1, and an empty region would
     *  return 0.
     */
    int getYSpanCount() const {
        return fYSpanCount;
    }

    /**
     *  Number of intervals in the entire region. This equals the number of
     *  rects that would be returned by the Iterator. In the logical case of
     *  a rect, this would return 1, and an empty region would return 0.
     */
    int getIntervalCount() const {
        return fIntervalCount;
    }

    static RunHead* Alloc(int count) {
        if (count < SkRegion::kRectRegionRuns) {
            return nullptr;
        }

        const int64_t size = sk_64_mul(count, sizeof(RunType)) + sizeof(RunHead);
        if (count < 0 || !SkTFitsIn<int32_t>(size)) { SK_ABORT("Invalid Size"); }

        void* storage = sk_malloc_throw(static_cast<size_t>(size));
        // The span and interval counts must be filled in later, otherwise the head is invalid.
        RunHead* head = new (storage) RunHead();
        (*head).fRunCount = count;
        return head;
    }

    static RunHead* Alloc(int count, int yspancount, int intervalCount) {
        if (yspancount <= 0 || intervalCount <= 1) {
            return nullptr;
        }

        RunHead* head = Alloc(count);
        if (!head) {
            return nullptr;
        }
        (*head).fYSpanCount = yspancount;
        (*head).fIntervalCount = intervalCount;
        return head;
    }

    // Borrows the fRunCount values that follow the header in the same allocation.
    SkRegion::RunType* writable_runs() {
        SkASSERT(fRefCnt == 1);
        SkRegion::RunType* runs = reinterpret_cast<SkRegion::RunType*>(this + 1);
        return runs;
    }

    const SkRegion::RunType* readonly_runs() const {
        const SkRegion::RunType* runs = reinterpret_cast<const SkRegion::RunType*>(this + 1);
        return runs;
    }

    // Returns a head whose runs this reference may write: this head when it is unshared,
    // otherwise a copy, after giving up this reference.
    RunHead* ensureWritable() {
        RunHead* writable = this;
        if (fRefCnt > 1) {
            // We need to alloc & copy the current region before decrease
            // the refcount because it could be freed in the meantime.
            writable = Alloc(fRunCount, fYSpanCount, fIntervalCount);
            const size_t runBytes = static_cast<size_t>(fRunCount) * sizeof(RunType);
            memcpy((*writable).writable_runs(), (*this).readonly_runs(), runBytes);

            // fRefCount might have changed since we last checked.
            // If we own the last reference at this point, we need to
            // free the memory.
            const int32_t remaining = --fRefCnt;
            if (remaining == 0) {
                sk_free(this);
            }
        }
        return writable;
    }

    /**
     *  Given a scanline (including its Bottom value at runs[0]), return the next
     *  scanline. Asserts that there is one (i.e. runs[0] < Sentinel)
     */
    static SkRegion::RunType* SkipEntireScanline(const SkRegion::RunType runs[]) {
        // we are not the Y Sentinel
        SkASSERT(runs[0] < SkRegion_kRunTypeSentinel);

        const int intervals = runs[1];
        SkASSERT(runs[2 + intervals * 2] == SkRegion_kRunTypeSentinel);
#ifdef SK_DEBUG
        {
            const int n = compute_intervalcount(&runs[2]);
            SkASSERT(n == intervals);
        }
#endif

        // skip the entire line [B N [L R] S]
        const SkRegion::RunType* next = runs + 1 + 1 + intervals * 2 + 1;
        return const_cast<SkRegion::RunType*>(next);
    }


    /**
     *  Return the scanline that contains the Y value. This requires that the Y
     *  value is already known to be contained within the bounds of the region,
     *  and so this routine never returns nullptr.
     *
     *  It returns the beginning of the scanline, starting with its Bottom value.
     */
    SkRegion::RunType* findScanline(int y) const {
        const RunType* runs = (*this).readonly_runs();

        // if the top-check fails, we didn't do a quick check on the bounds
        SkASSERT(y >= runs[0]);

        runs += 1;  // skip top-Y
        while (true) {
            const int bottom = runs[0];
            // If we hit this, we've walked off the region, and our bounds check
            // failed.
            SkASSERT(bottom < SkRegion_kRunTypeSentinel);
            if (y < bottom) {
                break;
            }
            runs = SkipEntireScanline(runs);
        }
        return const_cast<SkRegion::RunType*>(runs);
    }

    // Computes the span count, interval count and bounds of the runs written into this head.
    void computeRunBounds(SkIRect* bounds) {
        const RunType* runs = (*this).writable_runs();
        int index = 0;
        (*bounds).fTop = runs[index];
        index += 1;

        int bot = 0;
        int ySpanCount = 0;
        int intervalCount = 0;
        int left = SK_MaxS32;
        int rite = SK_MinS32;

        do {
            bot = runs[index];
            index += 1;
            SkASSERT(bot < SkRegion_kRunTypeSentinel);
            ySpanCount += 1;

            const int intervals = runs[index];
            index += 1;
            SkASSERT(intervals >= 0);
            SkASSERT(intervals < SkRegion_kRunTypeSentinel);

            if (intervals > 0) {
#ifdef SK_DEBUG
                {
                    const int n = compute_intervalcount(&runs[index]);
                    SkASSERT(n == intervals);
                }
#endif
                const RunType L = runs[index];
                SkASSERT(L < SkRegion_kRunTypeSentinel);
                if (left > L) {
                    left = L;
                }

                index += intervals * 2;
                const RunType R = runs[index - 1];
                SkASSERT(R < SkRegion_kRunTypeSentinel);
                if (rite < R) {
                    rite = R;
                }

                intervalCount += intervals;
            }
            SkASSERT(SkRegion_kRunTypeSentinel == runs[index]);
            index += 1;  // skip x-sentinel

            // test Y-sentinel
        } while (SkRegion_kRunTypeSentinel > runs[index]);

        // +1 to skip the last Y-sentinel
        SkASSERT(index + 1 == fRunCount);

        fYSpanCount = ySpanCount;
        fIntervalCount = intervalCount;

        (*bounds).fLeft = left;
        (*bounds).fRight = rite;
        (*bounds).fBottom = bot;
    }

private:
    int32_t fYSpanCount = 0;
    int32_t fIntervalCount = 0;
};

#endif
