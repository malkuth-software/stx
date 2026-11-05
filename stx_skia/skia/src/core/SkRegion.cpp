/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkRegion.h"

#include "include/private/SkMalloc.h"
#include "include/private/SkMath.h"
#include "include/private/SkTemplates.h"
#include "include/private/SkTo.h"
#include "src/core/SkBuffer.h"
#include "src/core/SkRegionPriv.h"
#include "src/core/SkSafeMath.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

using namespace skia_private;

/* SkRegion Run-Length Encoding (RLE) Format:
 *
 * A region is stored as a series of non-overlapping horizontal strips (scanlines).
 * All rectangles in a strip share the same Top and Bottom Y-coordinates.
 *
 * Data layout:
 * [Top-Y]
 *    [Bottom-Y, Interval-Count (N), Left-1, Right-1, ..., Left-N, Right-N, X-Sentinel]
 *    [Bottom-Y, Interval-Count (M), Left-1, Right-1, ..., Left-M, Right-M, X-Sentinel]
 *    ...
 * [Y-Sentinel]
 *
 * - Sentinels are 0x7FFFFFFF (SkRegion_kRunTypeSentinel).
 * - Interval-Count can be 0 (representing a vertical gap between strips).
 * - Strips are sorted by Y; Intervals within a strip are sorted by X.
 *
 * Example: Two rects [10, 10, 20, 20] and [30, 10, 40, 20] on one line,
 *          then a gap until Y=30, then [15, 30, 25, 40] would be:
 *
 *          10                       // Global Top-Y
 *          20, 2, 10, 20, 30, 40, S // Strip 1: Y 10-20, 2 rects, X-Sentinel
 *          30, 0, S                 // Strip 2: Y 20-30, 0 rects (gap), X-Sentinel
 *          40, 1, 15, 25, S         // Strip 3: Y 30-40, 1 rect, X-Sentinel
 *          S                        // Y-Sentinel
 */

/////////////////////////////////////////////////////////////////////////////////////////////////

// The rectangle state of SkRegion::fRunHead; the empty state is SkRegion::emptyRunHeadPtr().
static SkRegionPriv::RunHead* const kRectRunHead = nullptr;

constexpr int kRunArrayStackCount = 256;

// This is a simple data structure which is like a STArray<N,T,true>, except that:
//   - It does not initialize memory: every element is written before it is read.
//   - It does not distinguish between reserved space and initialized space.
//   - resizeToAtLeast() instead of resize()
//   - Uses sk_realloc_throw()
//   - Can never be made smaller.
// Measurement:  for the `region_union_16` benchmark, this is 6% faster.
class RunArray {
public:
    RunArray() = default;
    RunArray(const RunArray&) = delete;
    RunArray& operator=(const RunArray&) = delete;

    int count() const { return fCount; }

    // Indexes a live element; the index is checked in Release.
    SkRegionPriv::RunType& operator[](int i) {
        SkASSERT_RELEASE(static_cast<unsigned>(i) < static_cast<unsigned>(fCount));
        return fPtr[i];
    }

    /** Resize the array to a size greater-than-or-equal-to count, preserving its values. */
    void resizeToAtLeast(int count) {
        if (count > fCount) {
            // leave at least 50% extra space for future growth (unless adding would overflow)
            SkSafeMath safe;
            const int grownCount = safe.addInt(count, count >> 1);
            const int newCount = safe ? grownCount : SK_MaxS32;
            fMalloc.realloc(static_cast<size_t>(newCount));
            if (fPtr == fStack) {
                const size_t liveBytes = static_cast<size_t>(fCount) * sizeof(SkRegionPriv::RunType);
                memcpy(fMalloc.get(), fStack, liveBytes);
            }
            fPtr = fMalloc.get();
            fCount = newCount;
        }
    }

private:
    SkRegionPriv::RunType fStack[kRunArrayStackCount];
    AutoTMalloc<SkRegionPriv::RunType> fMalloc;
    int fCount = kRunArrayStackCount;
    // Borrows fStack or fMalloc's storage, whichever currently holds the values.
    SkRegionPriv::RunType* fPtr = fStack;
};

/*  Pass in the beginning with the intervals.
 *  We back up 1 to read the interval-count.
 *  Return the beginning of the next scanline (i.e. the next Y-value)
 */
static SkRegionPriv::RunType* skip_intervals(const SkRegionPriv::RunType runs[]) {
    const int intervals = runs[-1];
#ifdef SK_DEBUG
    if (intervals > 0) {
        SkASSERT(runs[0] < runs[1]);
        SkASSERT(runs[1] < SkRegion_kRunTypeSentinel);
    } else {
        SkASSERT(0 == intervals);
        SkASSERT(SkRegion_kRunTypeSentinel == runs[0]);
    }
#endif
    const SkRegionPriv::RunType* next = runs + intervals * 2 + 1;
    return const_cast<SkRegionPriv::RunType*>(next);
}

static inline void assert_sentinel(int32_t value, bool isSentinel) {
    SkASSERT(SkRegionValueIsSentinel(value) == isSentinel);
}

bool SkRegion::RunsAreARect(const SkRegion::RunType runs[], int count,
                            SkIRect* bounds) {
    assert_sentinel(runs[0], false);    // top
    SkASSERT(count >= kRectRegionRuns);

    if (count == kRectRegionRuns) {
        assert_sentinel(runs[1], false);    // bottom
        SkASSERT(1 == runs[2]);
        assert_sentinel(runs[3], false);    // left
        assert_sentinel(runs[4], false);    // right
        assert_sentinel(runs[5], true);
        assert_sentinel(runs[6], true);

        SkASSERT(runs[0] < runs[1]);    // valid height
        SkASSERT(runs[3] < runs[4]);    // valid width

        (*bounds).setLTRB(runs[3], runs[0], runs[4], runs[1]);
        return true;
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////

SkRegion::SkRegion() = default;

// Members start empty, a state that freeRuns() leaves alone.
SkRegion::SkRegion(const SkRegion& src) {
    (*this).setRegion(src);
}

SkRegion::SkRegion(const SkIRect& rect) {
    (*this).setRect(rect);
}

SkRegion::~SkRegion() {
    (*this).freeRuns();
}

void SkRegion::freeRuns() {
    if ((*this).isComplex()) {
        SkASSERT((*fRunHead).fRefCnt >= 1);
        const int32_t remaining = --(*fRunHead).fRefCnt;
        if (remaining == 0) {
            sk_free(fRunHead);
        }
    }
}

void SkRegion::allocateRuns(int count, int ySpanCount, int intervalCount) {
    fRunHead = RunHead::Alloc(count, ySpanCount, intervalCount);
}

void SkRegion::allocateRuns(int count) {
    fRunHead = RunHead::Alloc(count);
}

void SkRegion::allocateRuns(const RunHead& head) {
    fRunHead = RunHead::Alloc(head.fRunCount,
                              head.getYSpanCount(),
                              head.getIntervalCount());
}

SkRegion& SkRegion::operator=(const SkRegion& src) {
    (void)(*this).setRegion(src);
    return *this;
}

void SkRegion::swap(SkRegion& other) {
    std::swap(fBounds, other.fBounds);
    std::swap(fRunHead, other.fRunHead);
}

int SkRegion::computeRegionComplexity() const {
    if ((*this).isEmpty()) {
        return 0;
    }
    if ((*this).isRect()) {
        return 1;
    }
    const int intervals = (*fRunHead).getIntervalCount();
    return intervals;
}

bool SkRegion::setEmpty() {
    (*this).freeRuns();
    fBounds.setEmpty();
    fRunHead = emptyRunHeadPtr();
    return false;
}

bool SkRegion::setRect(const SkIRect& r) {
    if (r.isEmpty() ||
        SkRegion_kRunTypeSentinel == r.right() ||
        SkRegion_kRunTypeSentinel == r.bottom()) {
        return (*this).setEmpty();
    }
    (*this).freeRuns();
    fBounds = r;
    fRunHead = kRectRunHead;
    return true;
}

bool SkRegion::setRegion(const SkRegion& src) {
    if (this != &src) {
        (*this).freeRuns();

        fBounds = src.fBounds;
        fRunHead = src.fRunHead;
        if ((*this).isComplex()) {
            (*fRunHead).fRefCnt++;
        }
    }
    const bool nonEmpty = fRunHead != emptyRunHeadPtr();
    return nonEmpty;
}

bool SkRegion::op(const SkIRect& rect, const SkRegion& rgn, Op op) {
    const SkRegion tmp(rect);
    const bool nonEmpty = (*this).op(tmp, rgn, op);
    return nonEmpty;
}

bool SkRegion::op(const SkRegion& rgn, const SkIRect& rect, Op op) {
    const SkRegion tmp(rect);
    const bool nonEmpty = (*this).op(rgn, tmp, op);
    return nonEmpty;
}

///////////////////////////////////////////////////////////////////////////////

int SkRegion::count_runtype_values(int* itop, int* ibot) const {
    int maxT = 0;

    if ((*this).isRect()) {
        maxT = 2;
    } else {
        SkASSERT((*this).isComplex());
        maxT = (*fRunHead).getIntervalCount() * 2;
    }
    *itop = fBounds.fTop;
    *ibot = fBounds.fBottom;
    return maxT;
}

static bool isRunCountEmpty(int count) {
    const bool empty = count <= 2;
    return empty;
}

bool SkRegion::setRuns(RunType runs[], int count) {
#ifdef SK_DEBUG
    SkRegionPriv::Validate(*this);
#endif
    SkASSERT(count > 0);

    if (isRunCountEmpty(count)) {
        assert_sentinel(runs[count-1], true);
        return (*this).setEmpty();
    }

    // trim off any empty spans from the top and bottom
    // weird I should need this, perhaps op() could be smarter...
    if (count > kRectRegionRuns) {
        RunType* stop = runs + count;
        assert_sentinel(runs[0], false);    // top
        assert_sentinel(runs[1], false);    // bottom
        // runs[2] is uncomputed intervalCount

        if (runs[3] == SkRegion_kRunTypeSentinel) {  // should be first left...
            runs += 3;  // skip empty initial span
            runs[0] = runs[-2]; // set new top to prev bottom
            assert_sentinel(runs[1], false);    // bot: a sentinel would mean two in a row
            assert_sentinel(runs[2], false);    // intervalcount
            assert_sentinel(runs[3], false);    // left
            assert_sentinel(runs[4], false);    // right
        }

        assert_sentinel(stop[-1], true);
        assert_sentinel(stop[-2], true);

        // now check for a trailing empty span
        if (stop[-5] == SkRegion_kRunTypeSentinel) { // eek, stop[-4] was a bottom with no x-runs
            stop[-4] = SkRegion_kRunTypeSentinel;    // kill empty last span
            stop -= 3;
            assert_sentinel(stop[-1], true);    // last y-sentinel
            assert_sentinel(stop[-2], true);    // last x-sentinel
            assert_sentinel(stop[-3], false);   // last right
            assert_sentinel(stop[-4], false);   // last left
            assert_sentinel(stop[-5], false);   // last interval-count
            assert_sentinel(stop[-6], false);   // last bottom
        }
        count = static_cast<int>(stop - runs);
    }

    SkASSERT(count >= kRectRegionRuns);

    if (SkRegion::RunsAreARect(runs, count, &fBounds)) {
        return (*this).setRect(fBounds);
    }

    //  if we get here, we need to become a complex region

    if (!(*this).isComplex() || (*fRunHead).fRunCount != count) {
        (*this).freeRuns();
        (*this).allocateRuns(count);
        SkASSERT((*this).isComplex());
    }

    // must call this before we can write directly into runs()
    // in case we are sharing the buffer with another region (copy on write)
    fRunHead = (*fRunHead).ensureWritable();
    const size_t runBytes = static_cast<size_t>(count) * sizeof(RunType);
    memcpy((*fRunHead).writable_runs(), runs, runBytes);
    (*fRunHead).computeRunBounds(&fBounds);

    // Our computed bounds might be too large, so we have to check here.
    if (fBounds.isEmpty()) {
        return (*this).setEmpty();
    }

#ifdef SK_DEBUG
    SkRegionPriv::Validate(*this);
#endif

    return true;
}

void SkRegion::BuildRectRuns(const SkIRect& bounds,
                             RunType runs[kRectRegionRuns]) {
    runs[0] = bounds.fTop;
    runs[1] = bounds.fBottom;
    runs[2] = 1;    // 1 interval for this scanline
    runs[3] = bounds.fLeft;
    runs[4] = bounds.fRight;
    runs[5] = SkRegion_kRunTypeSentinel;
    runs[6] = SkRegion_kRunTypeSentinel;
}

bool SkRegion::contains(int32_t x, int32_t y) const {
#ifdef SK_DEBUG
    SkRegionPriv::Validate(*this);
#endif

    if (!fBounds.contains(x, y)) {
        return false;
    }
    if ((*this).isRect()) {
        return true;
    }
    SkASSERT((*this).isComplex());

    const RunType* runs = (*fRunHead).findScanline(y);

    // Skip the Bottom and IntervalCount
    runs += 2;

    // Just walk this scanline, checking each interval. The X-sentinel will
    // appear as a left-inteval (runs[0]) and should abort the search.
    //
    // We could do a bsearch, using interval-count (runs[1]), but need to time
    // when that would be worthwhile.
    //
    for (;;) {
        if (x < runs[0]) {
            break;
        }
        if (x < runs[1]) {
            return true;
        }
        runs += 2;
    }
    return false;
}

static SkRegionPriv::RunType scanline_bottom(const SkRegionPriv::RunType runs[]) {
    const SkRegionPriv::RunType bottom = runs[0];
    return bottom;
}

static const SkRegionPriv::RunType* scanline_next(const SkRegionPriv::RunType runs[]) {
    // skip [B N [L R]... S]
    const SkRegionPriv::RunType* next = runs + 2 + runs[1] * 2 + 1;
    return next;
}

static bool scanline_contains(const SkRegionPriv::RunType runs[],
                              SkRegionPriv::RunType L, SkRegionPriv::RunType R) {
    runs += 2;  // skip Bottom and IntervalCount
    for (;;) {
        if (L < runs[0]) {
            break;
        }
        if (R <= runs[1]) {
            return true;
        }
        runs += 2;
    }
    return false;
}

bool SkRegion::contains(const SkIRect& r) const {
#ifdef SK_DEBUG
    SkRegionPriv::Validate(*this);
#endif

    if (!fBounds.contains(r)) {
        return false;
    }
    if ((*this).isRect()) {
        return true;
    }
    SkASSERT((*this).isComplex());

    const RunType* scanline = (*fRunHead).findScanline(r.fTop);
    for (;;) {
        if (!scanline_contains(scanline, r.fLeft, r.fRight)) {
            return false;
        }
        if (r.fBottom <= scanline_bottom(scanline)) {
            break;
        }
        scanline = scanline_next(scanline);
    }
    return true;
}

bool SkRegion::contains(const SkRegion& rgn) const {
#ifdef SK_DEBUG
    SkRegionPriv::Validate(*this);
    SkRegionPriv::Validate(rgn);
#endif

    if ((*this).isEmpty() || rgn.isEmpty() || !fBounds.contains(rgn.fBounds)) {
        return false;
    }
    if ((*this).isRect()) {
        return true;
    }
    if (rgn.isRect()) {
        return (*this).contains(rgn.getBounds());
    }

    /*
     *  A contains B is equivalent to
     *  B - A == 0
     */
    return !Oper(rgn, *this, kDifference_Op, nullptr);
}

const SkRegion::RunType* SkRegion::getRuns(RunType tmpStorage[],
                                           int* intervals) const {
    SkASSERT(tmpStorage && intervals);
    const RunType* runs = tmpStorage;

    if ((*this).isEmpty()) {
        tmpStorage[0] = SkRegion_kRunTypeSentinel;
        *intervals = 0;
    } else if ((*this).isRect()) {
        BuildRectRuns(fBounds, tmpStorage);
        *intervals = 1;
    } else {
        runs = (*fRunHead).readonly_runs();
        *intervals = (*fRunHead).getIntervalCount();
    }
    return runs;
}

///////////////////////////////////////////////////////////////////////////////

static bool scanline_intersects(const SkRegionPriv::RunType runs[],
                                SkRegionPriv::RunType L, SkRegionPriv::RunType R) {
    runs += 2;  // skip Bottom and IntervalCount
    for (;;) {
        if (R <= runs[0]) {
            break;
        }
        if (L < runs[1]) {
            return true;
        }
        runs += 2;
    }
    return false;
}

bool SkRegion::intersects(const SkIRect& r) const {
#ifdef SK_DEBUG
    SkRegionPriv::Validate(*this);
#endif

    if ((*this).isEmpty() || r.isEmpty()) {
        return false;
    }

    SkIRect sect = {0, 0, 0, 0};
    if (!sect.intersect(fBounds, r)) {
        return false;
    }
    if ((*this).isRect()) {
        return true;
    }
    SkASSERT((*this).isComplex());

    const RunType* scanline = (*fRunHead).findScanline(sect.fTop);
    for (;;) {
        if (scanline_intersects(scanline, sect.fLeft, sect.fRight)) {
            return true;
        }
        if (sect.fBottom <= scanline_bottom(scanline)) {
            break;
        }
        scanline = scanline_next(scanline);
    }
    return false;
}

bool SkRegion::intersects(const SkRegion& rgn) const {
    if ((*this).isEmpty() || rgn.isEmpty()) {
        return false;
    }

    if (!SkIRect::Intersects(fBounds, rgn.fBounds)) {
        return false;
    }

    bool weAreARect = (*this).isRect();
    bool theyAreARect = rgn.isRect();

    if (weAreARect && theyAreARect) {
        return true;
    }
    if (weAreARect) {
        return rgn.intersects((*this).getBounds());
    }
    if (theyAreARect) {
        return (*this).intersects(rgn.getBounds());
    }

    // both of us are complex
    return Oper(*this, rgn, kIntersect_Op, nullptr);
}

///////////////////////////////////////////////////////////////////////////////

bool SkRegion::operator==(const SkRegion& b) const {
#ifdef SK_DEBUG
    SkRegionPriv::Validate(*this);
    SkRegionPriv::Validate(b);
#endif

    if (this == &b) {
        return true;
    }
    if (fBounds != b.fBounds) {
        return false;
    }

    const SkRegion::RunHead* ah = fRunHead;
    const SkRegion::RunHead* bh = b.fRunHead;

    // this catches empties and rects being equal
    if (ah == bh) {
        return true;
    }
    // now we insist that both are complex (but different ptrs)
    if (!(*this).isComplex() || !b.isComplex()) {
        return false;
    }
    if ((*ah).fRunCount != (*bh).fRunCount) {
        return false;
    }
    const size_t runBytes = static_cast<size_t>((*ah).fRunCount) * sizeof(SkRegion::RunType);
    const bool equal = memcmp((*ah).readonly_runs(), (*bh).readonly_runs(), runBytes) == 0;
    return equal;
}

// Return a (new) offset such that when applied (+=) to min and max, we don't overflow/underflow
static int32_t pin_offset_s32(int32_t min, int32_t max, int32_t offset) {
    SkASSERT(min <= max);
    // SkIRect::offset saturates at SK_MinS32, one above INT_MIN. Keeping the
    // complete span in that range preserves its width and its run coordinates.
    // When a limit is crossed, lo - min and hi - max lie between offset and zero, so the
    // corrected offsets are representable.
    const int32_t lo = SK_MinS32;
    const int32_t hi = SK_MaxS32;
    int32_t pinned = offset;
    if (static_cast<int64_t>(min) + pinned < lo) {
        pinned = lo - min;
    }
    if (static_cast<int64_t>(max) + pinned > hi) {
        pinned = hi - max;
    }
    return pinned;
}

void SkRegion::translate(int dx, int dy, SkRegion* dst) const {
#ifdef SK_DEBUG
    SkRegionPriv::Validate(*this);
#endif

    if (nullptr == dst) {
        return;
    }
    if ((*this).isEmpty()) {
        (*dst).setEmpty();
        return;
    }
    // pin dx and dy so we don't overflow our existing bounds
    dx = pin_offset_s32(fBounds.fLeft, fBounds.fRight, dx);
    dy = pin_offset_s32(fBounds.fTop, fBounds.fBottom, dy);

    if ((*this).isRect()) {
        (*dst).setRect(fBounds.makeOffset(dx, dy));
    } else {
        if (this == dst) {
            (*dst).fRunHead = (*(*dst).fRunHead).ensureWritable();
        } else {
            SkRegion    tmp;
            tmp.allocateRuns(*fRunHead);
            SkASSERT(tmp.isComplex());
            tmp.fBounds = fBounds;
            (*dst).swap(tmp);
        }

        (*dst).fBounds.offset(dx, dy);

        // Source and destination have the same layout, so one index walks both. When
        // translating in place they are the same storage, and each value is read before it is
        // replaced.
        const RunType* sruns = (*fRunHead).readonly_runs();
        RunType* druns = (*(*dst).fRunHead).writable_runs();
        int index = 0;

        druns[index] = sruns[index] + dy;    // top
        index += 1;
        while (true) {
            const int bottom = sruns[index];
            if (bottom == SkRegion_kRunTypeSentinel) {
                break;
            }
            druns[index] = bottom + dy;    // bottom
            index += 1;
            druns[index] = sruns[index];    // copy intervalCount
            index += 1;
            while (true) {
                const int x = sruns[index];
                if (x == SkRegion_kRunTypeSentinel) {
                    break;
                }
                druns[index] = x + dx;
                druns[index + 1] = sruns[index + 1] + dx;
                index += 2;
            }
            druns[index] = SkRegion_kRunTypeSentinel;    // x sentinel
            index += 1;
        }
        druns[index] = SkRegion_kRunTypeSentinel;    // y sentinel
        index += 1;

        SkASSERT(index == (*fRunHead).fRunCount);
        SkASSERT(index == (*(*dst).fRunHead).fRunCount);
    }

#ifdef SK_DEBUG
    SkRegionPriv::Validate(*this);
#endif
}

///////////////////////////////////////////////////////////////////////////////

bool SkRegion::setRects(SkSpan<const SkIRect> rects) {
    if (rects.empty()) {
        return (*this).setEmpty();
    }
    if (rects.size() == 1) {
        return (*this).setRect(rects[0]);
    }
    // The depth here is O(log_2(count)) so we shouldn't run out of stackframes.
    const size_t mid = rects.size() / 2;
    SkRegion left;
    SkRegion right;
    left.setRects(rects.first(mid));
    right.setRects(rects.subspan(mid));
    const bool nonEmpty = (*this).op(left, right, kUnion_Op);
    return nonEmpty;
}

///////////////////////////////////////////////////////////////////////////////

static void assert_valid_pair(int left, int rite) {
    SkASSERT(left == SkRegion_kRunTypeSentinel || left < rite);
}

// Merges the intervals of one scanline of each operand into elementary spans. Each span records
// which operands cover it: 1 for A only, 2 for B only, 3 for both.
struct spanRec {
    // Borrowed from the operands' runs: the interval after the current one of each operand.
    const SkRegionPriv::RunType* fA_runs = nullptr;
    const SkRegionPriv::RunType* fB_runs = nullptr;
    int fA_left = 0;
    int fA_rite = 0;
    int fB_left = 0;
    int fB_rite = 0;
    int fLeft = 0;
    int fRite = 0;
    int fInside = 0;

    void init(const SkRegionPriv::RunType a_runs[],
              const SkRegionPriv::RunType b_runs[]) {
        fA_left = a_runs[0];
        fA_rite = a_runs[1];
        fB_left = b_runs[0];
        fB_rite = b_runs[1];

        fA_runs = a_runs + 2;
        fB_runs = b_runs + 2;
    }

    bool done() const {
        SkASSERT(fA_left <= SkRegion_kRunTypeSentinel);
        SkASSERT(fB_left <= SkRegion_kRunTypeSentinel);
        const bool finished = fA_left == SkRegion_kRunTypeSentinel &&
                              fB_left == SkRegion_kRunTypeSentinel;
        return finished;
    }

    void next() {
        assert_valid_pair(fA_left, fA_rite);
        assert_valid_pair(fB_left, fB_rite);

        int inside = 0;
        int left = 0;
        int rite = 0;
        bool a_flush = false;
        bool b_flush = false;

        int a_left = fA_left;
        int a_rite = fA_rite;
        int b_left = fB_left;
        int b_rite = fB_rite;

        if (a_left < b_left) {
            inside = 1;
            left = a_left;
            if (a_rite <= b_left) {   // [...] <...>
                rite = a_rite;
                a_flush = true;
            } else { // [...<..]...> or [...<...>...]
                a_left = b_left;
                rite = b_left;
            }
        } else if (b_left < a_left) {
            inside = 2;
            left = b_left;
            if (b_rite <= a_left) {   // [...] <...>
                rite = b_rite;
                b_flush = true;
            } else {    // [...<..]...> or [...<...>...]
                b_left = a_left;
                rite = a_left;
            }
        } else {    // a_left == b_left
            inside = 3;
            left = a_left;  // or b_left
            if (a_rite <= b_rite) {
                b_left = a_rite;
                rite = a_rite;
                a_flush = true;
            }
            if (b_rite <= a_rite) {
                a_left = b_rite;
                rite = b_rite;
                b_flush = true;
            }
        }

        if (a_flush) {
            a_left = fA_runs[0];
            a_rite = fA_runs[1];
            fA_runs += 2;
        }
        if (b_flush) {
            b_left = fB_runs[0];
            b_rite = fB_runs[1];
            fB_runs += 2;
        }

        SkASSERT(left <= rite);

        // now update our state
        fA_left = a_left;
        fA_rite = a_rite;
        fB_left = b_left;
        fB_rite = b_rite;

        fLeft = left;
        fRite = rite;
        fInside = inside;
    }
};

/**
 *  Returns the distance to the next X-sentinel.
 *
 *  The runs pointer points to the first interval [Left, Right].
 *  The value at runs[-1] is the number of intervals for this scanline.
 *  This is safe because the caller has already advanced the runs pointer
 *  past the interval-count.
 *
 *  The scanline layout is: [Bottom-Y] [IntervalCount] [L1, R1, L2, R2, ...] [X-Sentinel]
 */
static int distance_to_sentinel(const SkRegionPriv::RunType* runs) {
    const int intervals = runs[-1];
    SkASSERT_RELEASE(runs[intervals * 2] == SkRegion_kRunTypeSentinel);
    const int distance = intervals * 2;
    return distance;
}

static int operate_on_span(const SkRegionPriv::RunType a_runs[],
                           const SkRegionPriv::RunType b_runs[],
                           RunArray* array, int dstOffset,
                           int min, int max) {
    // This is a worst-case for this span plus two for TWO terminating sentinels: every edge of
    // the result is an edge of an operand, so it has at most as many intervals as both together.
    const int worstCount =
            dstOffset + distance_to_sentinel(a_runs) + distance_to_sentinel(b_runs) + 2;
    (*array).resizeToAtLeast(worstCount);
    // Borrow the storage only after resizing; it holds at least worstCount values.
    SkRegionPriv::RunType* dst = &(*array)[dstOffset];
    // Values this span may write, leaving the final slot for the operator's last sentinel.
    const int capacity = worstCount - dstOffset - 1;
    int written = 0;

    spanRec rec;
    bool firstInterval = true;

    rec.init(a_runs, b_runs);

    // A span is kept when its coverage code lies in [min, max].
    const unsigned keepRange = static_cast<unsigned>(max - min);
    while (!rec.done()) {
        rec.next();

        const int left = rec.fLeft;
        const int rite = rec.fRite;

        // add left,rite to our dst buffer (checking for coincidence
        const unsigned coverage = static_cast<unsigned>(rec.fInside - min);
        if (coverage <= keepRange && left < rite) {    // skip if equal
            if (firstInterval || dst[written - 1] < left) {
                // The pair and this span's sentinel must fit.
                SkASSERT_RELEASE(written + 2 < capacity);
                dst[written] = left;
                dst[written + 1] = rite;
                written += 2;
                firstInterval = false;
            } else {
                // update the right edge
                dst[written - 1] = rite;
            }
        }
    }
    SkASSERT_RELEASE(written < capacity);
    dst[written] = SkRegion_kRunTypeSentinel;
    written += 1;
    const int stop = dstOffset + written;
    return stop;
}

// The span coverage codes that each operator keeps.
struct OpMinMax {
    uint8_t fMin;
    uint8_t fMax;
};

static const OpMinMax gOpMinMax[] = {
    { 1, 1 },   // Difference
    { 3, 3 },   // Intersection
    { 1, 3 },   // Union
    { 1, 2 }    // XOR
};
// need to ensure that the op enum lines up with our minmax array
static_assert(0 == SkRegion::kDifference_Op, "");
static_assert(1 == SkRegion::kIntersect_Op,  "");
static_assert(2 == SkRegion::kUnion_Op,      "");
static_assert(3 == SkRegion::kXOR_Op,        "");

// Writes the scanlines of a region operation into a run array. The array is borrowed and must
// outlive the operator.
class RgnOper {
public:
    RgnOper(int top, RunArray* array, SkRegion::Op op)
        : fMin(gOpMinMax[op].fMin)
        , fMax(gOpMinMax[op].fMax)
        , fArray(array)
        , fTop(top)  // just a first guess, we might update this
        { SkASSERT(static_cast<unsigned>(op) <= 3); }

    void addSpan(int bottom, const SkRegionPriv::RunType a_runs[],
                 const SkRegionPriv::RunType b_runs[]) {
        // skip X values and slots for the next Y+intervalCount
        const int start = fPrevDst + static_cast<int>(fPrevLen) + 2;
        // start points to beginning of dst interval
        const int stop = operate_on_span(a_runs, b_runs, fArray, start, fMin, fMax);
        const size_t len = SkToSizeT(stop - start);
        SkASSERT(len >= 1 && (len & 1) == 1);
        SkASSERT(SkRegion_kRunTypeSentinel == (*fArray)[stop - 1]);

        // Assert memcmp won't exceed fArray->count().
        SkASSERT((*fArray).count() >= SkToInt(start + len - 1));
        bool sameIntervals = false;
        if (fPrevLen == len) {
            const size_t intervalBytes = (len - 1) * sizeof(SkRegionPriv::RunType);
            sameIntervals = 1 == len ||
                            memcmp(&(*fArray)[fPrevDst], &(*fArray)[start], intervalBytes) == 0;
        }
        if (sameIntervals) {
            // update Y value
            (*fArray)[fPrevDst - 2] = bottom;
        } else {    // accept the new span
            if (len == 1 && fPrevLen == 0) {
                fTop = bottom; // just update our bottom
            } else {
                (*fArray)[start - 2] = bottom;
                (*fArray)[start - 1] = SkToS32(len >> 1);
                fPrevDst = start;
                fPrevLen = len;
            }
        }
    }

    int flush() {
        (*fArray)[fStartDst] = fTop;
        // Previously reserved enough for TWO sentinels.
        const int sentinelIndex = fPrevDst + static_cast<int>(fPrevLen);
        SkASSERT((*fArray).count() > sentinelIndex);
        (*fArray)[sentinelIndex] = SkRegion_kRunTypeSentinel;
        const int count = sentinelIndex - fStartDst + 1;
        return count;
    }

    bool isEmpty() const {
        const bool empty = 0 == fPrevLen;
        return empty;
    }

    uint8_t fMin = 0;
    uint8_t fMax = 0;

private:
    RunArray* fArray = nullptr;
    int fStartDst = 0;
    int fPrevDst = 1;
    size_t fPrevLen = 0;  // will never match a length from operate_on_span
    SkRegionPriv::RunType fTop = 0;
};

// want a unique value to signal that we exited due to quickExit
constexpr int kQuickExitTrueCount = -1;

static int operate(const SkRegionPriv::RunType a_runs[],
                   const SkRegionPriv::RunType b_runs[],
                   RunArray* dst,
                   SkRegion::Op op,
                   bool quickExit) {
    const SkRegionPriv::RunType gEmptyScanline[] = {
        0,  // fake bottom value
        0,  // zero intervals
        SkRegion_kRunTypeSentinel,
        // just need a 2nd value, since spanRec.init() reads 2 values, even
        // though if the first value is the sentinel, it ignores the 2nd value.
        // w/o the 2nd value here, we might read uninitialized memory.
        // This happens when we are using gSentinel, which is pointing at
        // our sentinel value.
        0
    };
    const SkRegionPriv::RunType* const gSentinel = &gEmptyScanline[2];

    int a_top = a_runs[0];
    int a_bot = a_runs[1];
    int b_top = b_runs[0];
    int b_bot = b_runs[1];

    a_runs += 3;    // skip top, bottom and the intervalCount
    b_runs += 3;    // skip top, bottom and the intervalCount

    // Now a_runs and b_runs to their intervals (or sentinel)

    assert_sentinel(a_top, false);
    assert_sentinel(a_bot, false);
    assert_sentinel(b_top, false);
    assert_sentinel(b_bot, false);

    RgnOper oper(std::min(a_top, b_top), dst, op);

    int prevBot = SkRegion_kRunTypeSentinel; // so we fail the first test

    while (a_bot < SkRegion_kRunTypeSentinel ||
           b_bot < SkRegion_kRunTypeSentinel) {
        int top = 0;
        int bot = 0;
        const SkRegionPriv::RunType* run0 = gSentinel;
        const SkRegionPriv::RunType* run1 = gSentinel;
        bool a_flush = false;
        bool b_flush = false;

        if (a_top < b_top) {
            top = a_top;
            run0 = a_runs;
            if (a_bot <= b_top) {   // [...] <...>
                bot = a_bot;
                a_flush = true;
            } else {  // [...<..]...> or [...<...>...]
                a_top = b_top;
                bot = b_top;
            }
        } else if (b_top < a_top) {
            top = b_top;
            run1 = b_runs;
            if (b_bot <= a_top) {   // [...] <...>
                bot = b_bot;
                b_flush = true;
            } else {    // [...<..]...> or [...<...>...]
                b_top = a_top;
                bot = a_top;
            }
        } else {    // a_top == b_top
            top = a_top;    // or b_top
            run0 = a_runs;
            run1 = b_runs;
            if (a_bot <= b_bot) {
                b_top = a_bot;
                bot = a_bot;
                a_flush = true;
            }
            if (b_bot <= a_bot) {
                a_top = b_bot;
                bot = b_bot;
                b_flush = true;
            }
        }

        if (top > prevBot) {
            oper.addSpan(top, gSentinel, gSentinel);
        }
        oper.addSpan(bot, run0, run1);

        if (quickExit && !oper.isEmpty()) {
            return kQuickExitTrueCount;
        }

        if (a_flush) {
            a_runs = skip_intervals(a_runs);
            a_top = a_bot;
            a_bot = a_runs[0];
            a_runs += 2;    // skip the bottom and the uninitialized intervalCount
            if (a_bot == SkRegion_kRunTypeSentinel) {
                a_top = a_bot;
            }
        }
        if (b_flush) {
            b_runs = skip_intervals(b_runs);
            b_top = b_bot;
            b_bot = b_runs[0];
            b_runs += 2;    // skip the bottom and the uninitialized intervalCount
            if (b_bot == SkRegion_kRunTypeSentinel) {
                b_top = b_bot;
            }
        }

        prevBot = bot;
    }
    const int count = oper.flush();
    return count;
}

///////////////////////////////////////////////////////////////////////////////

// Each check stores its value in result when there is one and reports whether the value is
// nonempty.
static bool setEmptyCheck(SkRegion* result) {
    if (result) {
        (*result).setEmpty();
    }
    return false;
}

static bool setRectCheck(SkRegion* result, const SkIRect& rect) {
    if (result) {
        const bool nonEmpty = (*result).setRect(rect);
        return nonEmpty;
    }
    const bool nonEmpty = !rect.isEmpty();
    return nonEmpty;
}

static bool setRegionCheck(SkRegion* result, const SkRegion& rgn) {
    if (result) {
        const bool nonEmpty = (*result).setRegion(rgn);
        return nonEmpty;
    }
    const bool nonEmpty = !rgn.isEmpty();
    return nonEmpty;
}

bool SkRegion::Oper(const SkRegion& rgnaOrig, const SkRegion& rgnbOrig, Op op,
                    SkRegion* result) {
    SkASSERT(static_cast<unsigned>(op) < kOpCount);

    if (kReplace_Op == op) {
        return setRegionCheck(result, rgnbOrig);
    }

    // swith to using pointers, so we can swap them as needed
    const SkRegion* rgna = &rgnaOrig;
    const SkRegion* rgnb = &rgnbOrig;

    // collapse difference and reverse-difference into just difference
    if (kReverseDifference_Op == op) {
        std::swap(rgna, rgnb);
        op = kDifference_Op;
    }

    SkIRect bounds = {0, 0, 0, 0};
    const bool a_empty = (*rgna).isEmpty();
    const bool b_empty = (*rgnb).isEmpty();
    const bool a_rect = (*rgna).isRect();
    const bool b_rect = (*rgnb).isRect();

    switch (op) {
    case kDifference_Op:
        if (a_empty) {
            return setEmptyCheck(result);
        }
        if (b_empty || !SkIRect::Intersects((*rgna).fBounds, (*rgnb).fBounds)) {
            return setRegionCheck(result, *rgna);
        }
        if (b_rect && (*rgnb).fBounds.containsNoEmptyCheck((*rgna).fBounds)) {
            return setEmptyCheck(result);
        }
        break;

    case kIntersect_Op:
        if (a_empty || b_empty || !bounds.intersect((*rgna).fBounds, (*rgnb).fBounds)) {
            return setEmptyCheck(result);
        }
        if (a_rect && b_rect) {
            return setRectCheck(result, bounds);
        }
        if (a_rect && (*rgna).fBounds.contains((*rgnb).fBounds)) {
            return setRegionCheck(result, *rgnb);
        }
        if (b_rect && (*rgnb).fBounds.contains((*rgna).fBounds)) {
            return setRegionCheck(result, *rgna);
        }
        break;

    case kUnion_Op:
        if (a_empty) {
            return setRegionCheck(result, *rgnb);
        }
        if (b_empty) {
            return setRegionCheck(result, *rgna);
        }
        if (a_rect && (*rgna).fBounds.contains((*rgnb).fBounds)) {
            return setRegionCheck(result, *rgna);
        }
        if (b_rect && (*rgnb).fBounds.contains((*rgna).fBounds)) {
            return setRegionCheck(result, *rgnb);
        }
        break;

    case kXOR_Op:
        if (a_empty) {
            return setRegionCheck(result, *rgnb);
        }
        if (b_empty) {
            return setRegionCheck(result, *rgna);
        }
        break;
    default:
        SkDEBUGFAIL("unknown region op");
        return false;
    }

    RunType tmpA[kRectRegionRuns] = {};
    RunType tmpB[kRectRegionRuns] = {};

    int a_intervals = 0;
    int b_intervals = 0;
    // The runs borrow either the regions' storage or the temporary arrays above.
    const RunType* a_runs = (*rgna).getRuns(tmpA, &a_intervals);
    const RunType* b_runs = (*rgnb).getRuns(tmpB, &b_intervals);

    RunArray array;
    const int count = operate(a_runs, b_runs, &array, op, nullptr == result);
    SkASSERT(count <= array.count());

    if (result) {
        SkASSERT(count >= 0);
        const bool nonEmpty = (*result).setRuns(&array[0], count);
        return nonEmpty;
    }
    const bool nonEmpty = kQuickExitTrueCount == count || !isRunCountEmpty(count);
    return nonEmpty;
}

bool SkRegion::op(const SkRegion& rgna, const SkRegion& rgnb, Op op) {
#ifdef SK_DEBUG
    SkRegionPriv::Validate(*this);
#endif
    const bool nonEmpty = SkRegion::Oper(rgna, rgnb, op, this);
    return nonEmpty;
}

///////////////////////////////////////////////////////////////////////////////

size_t SkRegion::writeToMemory(void* storage) const {
    if (nullptr == storage) {
        size_t size = sizeof(int32_t); // -1 (empty), 0 (rect), runCount
        if (!(*this).isEmpty()) {
            size += sizeof(fBounds);
            if ((*this).isComplex()) {
                size += 2 * sizeof(int32_t);    // ySpanCount + intervalCount
                size += static_cast<size_t>((*fRunHead).fRunCount) * sizeof(RunType);
            }
        }
        return size;
    }

    SkWBuffer buffer(storage);

    if ((*this).isEmpty()) {
        buffer.write32(-1);
    } else {
        const bool isRect = (*this).isRect();

        buffer.write32(isRect ? 0 : (*fRunHead).fRunCount);
        buffer.write(&fBounds, sizeof(fBounds));

        if (!isRect) {
            buffer.write32((*fRunHead).getYSpanCount());
            buffer.write32((*fRunHead).getIntervalCount());
            const size_t runBytes = static_cast<size_t>((*fRunHead).fRunCount) * sizeof(RunType);
            buffer.write((*fRunHead).readonly_runs(), runBytes);
        }
    }
    const size_t written = buffer.pos();
    return written;
}

static bool validate_run_count(int ySpanCount, int intervalCount, int runCount) {
    // return 2 + 3 * ySpanCount + 2 * intervalCount;
    if (ySpanCount < 1 || intervalCount < 2) {
        return false;
    }
    SkSafeMath safeMath;
    int sum = 2;  // the top value and the final y-sentinel
    // 3 bytes per ySpan (stop Y, how many intervals, sentinel)
    sum = safeMath.addInt(sum, ySpanCount);
    sum = safeMath.addInt(sum, ySpanCount);
    sum = safeMath.addInt(sum, ySpanCount);
    // 2 bytes per interval (startX, stop X)
    sum = safeMath.addInt(sum, intervalCount);
    sum = safeMath.addInt(sum, intervalCount);
    const bool valid = safeMath && sum == runCount;
    return valid;
}

// Validate a region without reading outside the declared run count.
// The serialized payload is a byte sequence, not an array of live int32_t
// objects. A fixed-size copy admits unaligned caller storage without scratch.
static int32_t read_run_word(const char* runs, int index) {
    const size_t byteOffset = static_cast<size_t>(index) * sizeof(int32_t);
    int32_t value = 0;
    std::memcpy(&value, runs + byteOffset, sizeof(value));
    return value;
}

static int32_t read_run_word(const int32_t* runs, int index) {
    const int32_t value = runs[index];
    return value;
}

template <typename RunStorage>
static bool validate_run(const RunStorage* runs,
                         int runCount,
                         const SkIRect& givenBounds,
                         int32_t ySpanCount,
                         int32_t intervalCount) {
    // Region Layout:
    //    Top ( Bottom Span_Interval_Count ( Left Right )* Sentinel )+ Sentinel
    if (!validate_run_count(SkToInt(ySpanCount), SkToInt(intervalCount), runCount)) {
        return false;
    }
    SkASSERT(runCount >= 7);  // 7==SkRegion::kRectRegionRuns
    // quick safety check:
    if (read_run_word(runs, runCount - 1) != SkRegion_kRunTypeSentinel ||
        read_run_word(runs, runCount - 2) != SkRegion_kRunTypeSentinel) {
        return false;
    }
    int index = 0;
    SkIRect bounds = {0, 0, 0 ,0};  // calulated bounds
    SkIRect rect = {0, 0, 0, 0};    // current rect
    bool prevWasEmpty = true;       // If we start with an empty slice, that's corrupted data.
    rect.fTop = read_run_word(runs, index);
    index += 1;
    if (rect.fTop == SkRegion_kRunTypeSentinel) {
        return false;  // no rect can contain SkRegion_kRunTypeSentinel
    }
    if (rect.fTop != givenBounds.fTop) {
        return false;  // Must not begin with empty span that does not contribute to bounds.
    }
    do {
        --ySpanCount;
        if (ySpanCount < 0) {
            return false;  // too many yspans
        }
        if (index > runCount - 4) {
            return false;  // bottom, interval count, and both sentinels must remain
        }
        rect.fBottom = read_run_word(runs, index);
        index += 1;
        if (rect.fBottom == SkRegion_kRunTypeSentinel) {
            return false;
        }
        if (rect.fBottom > givenBounds.fBottom) {
            return false;  // Must not end with empty span that does not contribute to bounds.
        }
        if (rect.fBottom <= rect.fTop) {
            return false;  // y-intervals must be ordered; rects must be non-empty.
        }

        int32_t xIntervals = read_run_word(runs, index);
        index += 1;
        if (xIntervals < 0 || xIntervals > intervalCount ||
            xIntervals > (runCount - index - 2) / 2) {
            return false;
        }
        if (xIntervals == 0) {
            if (prevWasEmpty) {
                // back to back empty spans are invalid; our serialization always has them together
                return false;
            }
            prevWasEmpty = true;
        } else {
            prevWasEmpty = false;
        }
        intervalCount -= xIntervals;
        bool firstInterval = true;
        int32_t lastRight = 0;  // check that x-intervals are distinct and ordered.
        for (int interval = 0; interval < xIntervals; ++interval) {
            rect.fLeft = read_run_word(runs, index);
            rect.fRight = read_run_word(runs, index + 1);
            index += 2;
            if (rect.fLeft == SkRegion_kRunTypeSentinel ||
                rect.fRight == SkRegion_kRunTypeSentinel ||
                rect.fLeft >= rect.fRight ||  // check non-empty rect
                (!firstInterval && rect.fLeft <= lastRight)) {
                return false;
            }
            lastRight = rect.fRight;
            firstInterval = false;
            bounds.join(rect);
        }
        const int32_t xSentinel = read_run_word(runs, index);
        index += 1;
        if (xSentinel != SkRegion_kRunTypeSentinel) {
            return false;  // required check sentinel.
        }
        rect.fTop = rect.fBottom;
        if (index >= runCount) {
            return false;
        }
    } while (read_run_word(runs, index) != SkRegion_kRunTypeSentinel);
    ++index;
    const bool valid = index == runCount && ySpanCount == 0 && intervalCount == 0 &&
                       givenBounds == bounds;
    return valid;
}

size_t SkRegion::readFromMemory(const void* storage, size_t length) {
    SkRBuffer buffer(storage, length);
    SkRegion tmp;
    int32_t count = 0;

    // Serialized Region Format:
    //    Empty:
    //       -1
    //    Simple Rect:
    //       0  LEFT TOP RIGHT BOTTOM
    //    Complex Region:
    //       COUNT LEFT TOP RIGHT BOTTOM Y_SPAN_COUNT TOTAL_INTERVAL_COUNT [RUNS....]
    if (!buffer.readS32(&count) || count < -1) {
        return 0;
    }
    if (count >= 0) {
        if (!buffer.read(&tmp.fBounds, sizeof(tmp.fBounds)) || tmp.fBounds.isEmpty()) {
            return 0;  // Short buffer or bad bounds for non-empty region; report failure.
        }
        if (count == 0) {
            if (tmp.fBounds.right() == SkRegion_kRunTypeSentinel ||
                tmp.fBounds.bottom() == SkRegion_kRunTypeSentinel) {
                return 0;
            }
            tmp.fRunHead = kRectRunHead;
        } else {
            int32_t ySpanCount = 0;
            int32_t intervalCount = 0;
            if (!buffer.readS32(&ySpanCount) ||
                !buffer.readS32(&intervalCount) ||
                static_cast<size_t>(count) > buffer.available() / sizeof(int32_t)) {
                return 0;
            }
            const size_t runBytes = static_cast<size_t>(count) * sizeof(int32_t);
            const char* source = static_cast<const char*>(storage) + buffer.pos();
            if (!validate_run(source, count, tmp.fBounds, ySpanCount, intervalCount)) {
                return 0;  // invalid runs, don't even allocate
            }
            tmp.allocateRuns(count, ySpanCount, intervalCount);
            SkASSERT(tmp.isComplex());
            [[maybe_unused]] const bool runsRead =
                    buffer.read((*tmp.fRunHead).writable_runs(), runBytes);
            SkASSERT(runsRead);
        }
    }
    SkASSERT(tmp.isValid());
    SkASSERT(buffer.isValid());
    (*this).swap(tmp);
    const size_t consumed = buffer.pos();
    return consumed;
}

///////////////////////////////////////////////////////////////////////////////

bool SkRegion::isValid() const {
    if ((*this).isEmpty()) {
        return fBounds == SkIRect{0, 0, 0, 0};
    }
    if (fBounds.isEmpty()) {
        return false;
    }
    if ((*this).isRect()) {
        return true;
    }
    if (!fRunHead || (*fRunHead).fRefCnt <= 0) {
        return false;
    }
    const bool valid = validate_run((*fRunHead).readonly_runs(), (*fRunHead).fRunCount, fBounds,
                                    (*fRunHead).getYSpanCount(),
                                    (*fRunHead).getIntervalCount());
    return valid;
}

#ifdef SK_DEBUG
void SkRegionPriv::Validate(const SkRegion& rgn) { SkASSERT(rgn.isValid()); }

void SkRegion::dump() const {
    if ((*this).isEmpty()) {
        SkDebugf("  rgn: empty\n");
    } else {
        SkDebugf("  rgn: [%d %d %d %d]", fBounds.fLeft, fBounds.fTop, fBounds.fRight, fBounds.fBottom);
        if ((*this).isComplex()) {
            const RunType* runs = (*fRunHead).readonly_runs();
            for (int i = 0; i < (*fRunHead).fRunCount; i++) {
                SkDebugf(" %d", runs[i]);
            }
        }
        SkDebugf("\n");
    }
}

#endif

///////////////////////////////////////////////////////////////////////////////

SkRegion::Iterator::Iterator(const SkRegion& rgn) {
    (*this).reset(rgn);
}

bool SkRegion::Iterator::rewind() {
    if (fRgn) {
        (*this).reset(*fRgn);
        return true;
    }
    return false;
}

void SkRegion::Iterator::reset(const SkRegion& rgn) {
    fRgn = &rgn;
    if (rgn.isEmpty()) {
        fDone = true;
    } else {
        fDone = false;
        if (rgn.isRect()) {
            fRect = rgn.fBounds;
            fRuns = nullptr;
        } else {
            fRuns = (*rgn.fRunHead).readonly_runs();
            fRect.setLTRB(fRuns[3], fRuns[0], fRuns[4], fRuns[1]);
            fRuns += 5;
            // Now fRuns points to the 2nd interval (or x-sentinel)
        }
    }
}

void SkRegion::Iterator::next() {
    if (fDone) {
        return;
    }

    if (fRuns == nullptr) {   // rect case
        fDone = true;
        return;
    }

    const RunType* runs = fRuns;

    if (runs[0] < SkRegion_kRunTypeSentinel) { // valid X value
        fRect.fLeft = runs[0];
        fRect.fRight = runs[1];
        runs += 2;
    } else {    // we're at the end of a line
        runs += 1;
        if (runs[0] < SkRegion_kRunTypeSentinel) { // valid Y value
            int intervals = runs[1];
            if (0 == intervals) {    // empty line
                fRect.fTop = runs[0];
                runs += 3;
            } else {
                fRect.fTop = fRect.fBottom;
            }

            fRect.fBottom = runs[0];
            assert_sentinel(runs[2], false);
            assert_sentinel(runs[3], false);
            fRect.fLeft = runs[2];
            fRect.fRight = runs[3];
            runs += 4;
        } else {    // end of rgn
            fDone = true;
        }
    }
    fRuns = runs;
}

SkRegion::Cliperator::Cliperator(const SkRegion& rgn, const SkIRect& clip)
        : fIter(rgn), fClip(clip), fDone(true) {
    const SkIRect& r = fIter.rect();

    while (!fIter.done()) {
        if (r.fTop >= clip.fBottom) {
            break;
        }
        if (fRect.intersect(clip, r)) {
            fDone = false;
            break;
        }
        fIter.next();
    }
}

void SkRegion::Cliperator::next() {
    if (fDone) {
        return;
    }

    const SkIRect& r = fIter.rect();

    fDone = true;
    fIter.next();
    while (!fIter.done()) {
        if (r.fTop >= fClip.fBottom) {
            break;
        }
        if (fRect.intersect(fClip, r)) {
            fDone = false;
            break;
        }
        fIter.next();
    }
}

///////////////////////////////////////////////////////////////////////////////

SkRegion::Spanerator::Spanerator(const SkRegion& rgn, int y, int left,
                                 int right) {
#ifdef SK_DEBUG
    SkRegionPriv::Validate(rgn);
#endif

    const SkIRect& r = rgn.getBounds();

    fDone = true;
    if (!rgn.isEmpty() && y >= r.fTop && y < r.fBottom &&
            right > r.fLeft && left < r.fRight) {
        if (rgn.isRect()) {
            if (left < r.fLeft) {
                left = r.fLeft;
            }
            if (right > r.fRight) {
                right = r.fRight;
            }
            fLeft = left;
            fRight = right;
            fRuns = nullptr;    // means we're a rect, not a rgn
            fDone = false;
        } else {
            const SkRegion::RunType* runs = (*rgn.fRunHead).findScanline(y);
            runs += 2;  // skip Bottom and IntervalCount
            for (;;) {
                // runs[0..1] is to the right of the span, so we're done
                if (runs[0] >= right) {
                    break;
                }
                // runs[0..1] is to the left of the span, so continue
                if (runs[1] <= left) {
                    runs += 2;
                    continue;
                }
                // runs[0..1] intersects the span
                fRuns = runs;
                fLeft = left;
                fRight = right;
                fDone = false;
                break;
            }
        }
    }
}

bool SkRegion::Spanerator::next(int* left, int* right) {
    if (fDone) {
        return false;
    }

    if (fRuns == nullptr) {   // we're a rect
        fDone = true;   // ok, now we're done
        if (left) {
            *left = fLeft;
        }
        if (right) {
            *right = fRight;
        }
        return true;    // this interval is legal
    }

    const SkRegion::RunType* runs = fRuns;

    if (runs[0] >= fRight) {
        fDone = true;
        return false;
    }

    SkASSERT(runs[1] > fLeft);

    if (left) {
        *left = std::max(fLeft, runs[0]);
    }
    if (right) {
        *right = std::min(fRight, runs[1]);
    }
    fRuns = runs + 2;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static void visit_pairs(int pairCount, int y, const int32_t pairs[],
                        SkRegionSpanVisitor& visitor) {
    for (int i = 0; i < pairCount; ++i) {
        const SkIRect span = {pairs[2 * i], y, pairs[2 * i + 1], y + 1};
        visitor.visitSpan(span);
    }
}

void SkRegionPriv::VisitSpans(const SkRegion& rgn, SkRegionSpanVisitor& visitor) {
    if (rgn.isEmpty()) {
        return;
    }
    if (rgn.isRect()) {
        visitor.visitSpan(rgn.getBounds());
    } else {
        const int32_t* runs = (*rgn.fRunHead).readonly_runs();
        int index = 0;
        int32_t top = runs[index];
        int32_t bot = runs[index + 1];
        index += 2;
        do {
            const int pairCount = runs[index];
            index += 1;
            if (pairCount == 1) {
                const SkIRect band = {runs[index], top, runs[index + 1], bot};
                visitor.visitSpan(band);
                index += 2;
            } else if (pairCount > 1) {
                // we have to loop repeated in Y, sending each interval in Y -> X order
                for (int y = top; y < bot; ++y) {
                    visit_pairs(pairCount, y, &runs[index], visitor);
                }
                index += pairCount * 2;
            }
            assert_sentinel(runs[index], true);
            index += 1; // skip sentinel

            // read next bottom or sentinel
            top = bot;
            bot = runs[index];
            index += 1;
        } while (!SkRegionValueIsSentinel(bot));
    }
}

