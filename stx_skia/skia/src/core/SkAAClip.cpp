/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkAAClip.h"

#include "include/core/SkClipOp.h"
#include "include/core/SkPath.h"
#include "include/core/SkRegion.h"
#include "include/core/SkTypes.h"
#include "include/private/SkCPUTypes.h"
#include "include/private/SkDebug.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkMath.h"
#include "include/private/SkTDArray.h"
#include "include/private/SkTo.h"
#include "src/core/SkBlitter.h"
#include "src/core/SkColorData.h"
#include "src/core/SkMask.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPathRaw.h"
#include "src/core/SkSafeMath.h"
#include "src/core/SkScan.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <vector>

namespace {

// Validates the clip on construction and again on destruction. validate() is empty in Release.
class AutoAAClipValidate {
public:
    explicit AutoAAClipValidate(const SkAAClip& clip) : fClip(clip) {
        fClip.validate();
    }
    ~AutoAAClipValidate() {
        fClip.validate();
    }

    AutoAAClipValidate(const AutoAAClipValidate&) = delete;
    AutoAAClipValidate& operator=(const AutoAAClipValidate&) = delete;

private:
    // Borrowed: the clip outlives this validator.
    const SkAAClip& fClip;
};

///////////////////////////////////////////////////////////////////////////////

static constexpr int32_t kMaxInt32 = 0x7FFFFFFF;

#ifdef SK_DEBUG
// assert we're exactly width-wide, and then return the number of bytes used
static size_t compute_row_length(const uint8_t row[], int width) {
    size_t index = 0;
    int remaining = width;
    while (remaining > 0) {
        const int n = row[index];
        SkASSERT(n > 0);
        SkASSERT(n <= remaining);
        index += 2;
        remaining -= n;
    }
    SkASSERT(0 == remaining);
    return index;
}
#endif

/*
 *  Data runs are packed [count, alpha]
 */
// One row record: the last y (relative to the clip's top) that the row covers, and the byte
// offset of the row's runs in the clip's data.
struct YOffset {
    int32_t  fY = 0;
    uint32_t fOffset = 0;
};

// Walks the runs of one row. The row is borrowed from the clip's data; a null row reads as one
// transparent run that never ends.
class RowIter {
public:
    RowIter(const uint8_t* row, const SkIRect& bounds)
            : fRow(row)
            , fLeft(bounds.fLeft)
            , fBoundsRight(bounds.fRight) {
        if (row) {
            fRight = bounds.fLeft + row[0];
            SkASSERT(fRight <= fBoundsRight);
            fAlpha = row[1];
            fDone = false;
        } else {
            fDone = true;
            fRight = kMaxInt32;
            fAlpha = 0;
        }
    }

    bool done() const { return fDone; }
    int left() const { return fLeft; }
    int right() const { return fRight; }
    U8CPU alpha() const { return fAlpha; }
    void next() {
        if (!fDone) {
            fLeft = fRight;
            if (fRight == fBoundsRight) {
                fDone = true;
                fRight = kMaxInt32;
                fAlpha = 0;
            } else {
                fRow += 2;
                fRight += fRow[0];
                fAlpha = fRow[1];
                SkASSERT(fRight <= fBoundsRight);
            }
        }
    }

private:
    const uint8_t*  fRow = nullptr;
    int             fLeft = 0;
    int             fRight = kMaxInt32;
    int             fBoundsRight = 0;
    bool            fDone = true;
    uint8_t         fAlpha = 0;
};

// Walks the rows of a clip from top to bottom. The rows and data are borrowed from the clip,
// which must outlive the iterator.
class Iter {
public:
    Iter() = default;

    Iter(int y, const uint8_t* data, const YOffset* start, const YOffset* end)
            : fCurrYOff(start)
            , fStopYOff(end)
            , fData(data + (*start).fOffset)
            , fTop(y)
            , fBottom(y + (*start).fY + 1)
            , fDone(false) {}

    bool done() const { return fDone; }
    int top() const { return fTop; }
    int bottom() const { return fBottom; }
    const uint8_t* data() const { return fData; }

    void next() {
        if (!fDone) {
            const YOffset* prev = fCurrYOff;
            const YOffset* curr = prev + 1;
            SkASSERT(curr <= fStopYOff);

            fTop = fBottom;
            if (curr >= fStopYOff) {
                fDone = true;
                fBottom = kMaxInt32;
                fData = nullptr;
            } else {
                fBottom += (*curr).fY - (*prev).fY;
                fData += (*curr).fOffset - (*prev).fOffset;
                fCurrYOff = curr;
            }
        }
    }

private:
    const YOffset* fCurrYOff = nullptr;
    const YOffset* fStopYOff = nullptr;
    const uint8_t* fData = nullptr;

    int fTop = kMaxInt32;
    int fBottom = kMaxInt32;
    bool fDone = true;
};

}  // namespace

///////////////////////////////////////////////////////////////////////////////

/**
 *  Shared, reference-counted clip storage. One allocation holds this header, fRowCount row
 *  records and fDataSize bytes of row runs. The header is constructed in place and the storage
 *  is released with sk_free when the last reference goes away.
 */
struct SkAAClip::RunHead {
    std::atomic<int32_t> fRefCnt{1};
    int32_t fRowCount = 0;
    size_t  fDataSize = 0;

    // The row records follow the header; the row data follows the records.
    YOffset* yoffsets() {
        YOffset* records = reinterpret_cast<YOffset*>(reinterpret_cast<char*>(this) +
                                                      sizeof(RunHead));
        return records;
    }
    const YOffset* yoffsets() const {
        const YOffset* records = reinterpret_cast<const YOffset*>(
                reinterpret_cast<const char*>(this) + sizeof(RunHead));
        return records;
    }
    uint8_t* data() {
        uint8_t* bytes = reinterpret_cast<uint8_t*>((*this).yoffsets() + fRowCount);
        return bytes;
    }
    const uint8_t* data() const {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>((*this).yoffsets() + fRowCount);
        return bytes;
    }

    static RunHead* Alloc(int rowCount, size_t dataSize) {
        SkASSERT(rowCount >= 0);
        SkSafeMath safe;
        const size_t recordBytes = safe.mul(static_cast<size_t>(rowCount), sizeof(YOffset));
        const size_t size = safe.add(safe.add(sizeof(RunHead), recordBytes), dataSize);
        if (!safe.ok()) {
            sk_out_of_memory();
        }
        void* storage = sk_malloc_throw(size);
        RunHead* head = new (storage) RunHead();
        (*head).fRowCount = rowCount;
        (*head).fDataSize = dataSize;
        return head;
    }

    static int ComputeRowSizeForWidth(int width) {
        // 2 bytes per segment, where each segment can store up to 255 for count
        int segments = 0;
        int remaining = width;
        while (remaining > 0) {
            segments += 1;
            const int n = std::min(remaining, 255);
            remaining -= n;
        }
        const int rowSize = segments * 2;    // each segment is row[0] + row[1] (n + alpha)
        return rowSize;
    }

    static RunHead* AllocRect(const SkIRect& bounds) {
        SkASSERT(!bounds.isEmpty());
        int width = bounds.width();
        const size_t rowSize = static_cast<size_t>(ComputeRowSizeForWidth(width));
        RunHead* head = RunHead::Alloc(1, rowSize);
        YOffset* yoff = (*head).yoffsets();
        (*yoff).fY = bounds.height() - 1;
        (*yoff).fOffset = 0;
        uint8_t* row = (*head).data();
        size_t index = 0;
        while (width > 0) {
            const int n = std::min(width, 255);
            row[index] = static_cast<uint8_t>(n);
            row[index + 1] = 0xFF;
            width -= n;
            index += 2;
        }
        return head;
    }

    static Iter Iterate(const SkAAClip& clip) {
        const RunHead* head = clip.fRunHead;
        if (!clip.fRunHead) {
            // A null run head is an empty clip, so return aan already finished iterator.
            return Iter();
        }

        const Iter rows(clip.getBounds().fTop, (*head).data(), (*head).yoffsets(),
                        (*head).yoffsets() + (*head).fRowCount);
        return rows;
    }
};

///////////////////////////////////////////////////////////////////////////////

// Accumulates the runs of a clip in Y then X order and packs them into a RunHead.
class SkAAClip::Builder {
    class Blitter;

    SkIRect fBounds = {0, 0, 0, 0};
    // One accumulated row. Rows own their run bytes.
    struct Row {
        int fY = 0;
        int fWidth = 0;
        std::unique_ptr<SkTDArray<uint8_t>> fData;
    };
    std::vector<Row> fRows;
    // Index of the row receiving runs, or kNoRow.
    int fCurrRow = kNoRow;
    int fPrevY = -1;
    int fWidth = 0;
    int fMinY = 0;

    static constexpr int kNoRow = -1;

public:
    explicit Builder(const SkIRect& bounds)
            : fBounds(bounds)
            , fWidth(bounds.width())
            , fMinY(bounds.fTop) {}

    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;

    bool applyClipOp(SkAAClip* target, const SkAAClip& other, SkClipOp op);
    bool blitPath(SkAAClip* target, const SkPath& path, bool doAA);

private:
    using AlphaProc = U8CPU (*)(U8CPU alphaA, U8CPU alphaB);
    void operateX(int lastY, RowIter& iterA, RowIter& iterB, AlphaProc proc);
    void operateY(const SkAAClip& A, const SkAAClip& B, SkClipOp op);

    void addRun(int x, int y, U8CPU alpha, int count) {
        SkASSERT(count > 0);
        SkASSERT(fBounds.contains(x, y));
        SkASSERT(fBounds.contains(x + count - 1, y));

        const int localX = x - fBounds.left();
        const int localY = y - fBounds.top();

        if (localY != fPrevY) {
            SkASSERT(localY > fPrevY);
            fPrevY = localY;
            const int next = (*this).flushRow(true);
            Row& row = fRows[next];
            row.fY = localY;
            row.fWidth = 0;
            SkASSERT(row.fData);
            SkASSERT((*row.fData).empty());
            fCurrRow = next;
        }

        Row& row = fRows[fCurrRow];
        SkASSERT(row.fWidth <= localX);
        SkASSERT(row.fWidth < fBounds.width());

        SkTDArray<uint8_t>& data = *row.fData;

        const int gap = localX - row.fWidth;
        if (gap) {
            AppendRun(data, 0, gap);
            row.fWidth += gap;
            SkASSERT(row.fWidth < fBounds.width());
        }

        AppendRun(data, alpha, count);
        row.fWidth += count;
        SkASSERT(row.fWidth <= fBounds.width());
    }

    // Extends the current row down to cover height rows ending at y + height - 1.
    void extendCurrentRow(int y, int height) {
        Row& row = fRows[fCurrRow];
        const int localY = y - fBounds.fTop;
        SkASSERT(localY == row.fY);
        row.fY = localY + height - 1;
    }

    void addColumn(int x, int y, U8CPU alpha, int height) {
        SkASSERT(fBounds.contains(x, y + height - 1));

        (*this).addRun(x, y, alpha, 1);
        (*this).flushRowH(fRows[fCurrRow]);
        (*this).extendCurrentRow(y, height);
    }

    void addRectRun(int x, int y, int width, int height) {
        SkASSERT(fBounds.contains(x + width - 1, y + height - 1));
        (*this).addRun(x, y, 0xFF, width);

        // we assum the rect must be all we'll see for these scanlines
        // so we ensure our row goes all the way to our right
        (*this).flushRowH(fRows[fCurrRow]);
        (*this).extendCurrentRow(y, height);
    }

    void addAntiRectRun(int x, int y, int width, int height,
                        SkAlpha leftAlpha, SkAlpha rightAlpha) {
        // According to SkBlitter.cpp, no matter whether leftAlpha is 0 or positive,
        // we should always consider [x, x+1] as the left-most column and [x+1, x+1+width]
        // as the rect with full alpha.
        SkASSERT(fBounds.contains(x + width + (rightAlpha > 0 ? 1 : 0),
                 y + height - 1));
        SkASSERT(width >= 0);

        // Conceptually we're always adding 3 runs, but we should
        // merge or omit them if possible.
        int left = x;
        int fullWidth = width;
        if (leftAlpha == 0xFF) {
            fullWidth += 1;
        } else if (leftAlpha > 0) {
            (*this).addRun(left, y, leftAlpha, 1);
            left += 1;
        } else {
            // leftAlpha is 0, ignore the left column
            left += 1;
        }
        if (rightAlpha == 0xFF) {
            fullWidth += 1;
        }
        if (fullWidth > 0) {
            (*this).addRun(left, y, 0xFF, fullWidth);
        }
        if (rightAlpha > 0 && rightAlpha < 255) {
            (*this).addRun(left + fullWidth, y, rightAlpha, 1);
        }

        // if we never called addRun, we might not have a fCurrRow yet
        if (fCurrRow != kNoRow) {
            // we assume the rect must be all we'll see for these scanlines
            // so we ensure our row goes all the way to our right
            (*this).flushRowH(fRows[fCurrRow]);
            (*this).extendCurrentRow(y, height);
        }
    }

    bool finish(SkAAClip* target) {
        (*this).flushRow(false);

        size_t dataSize = 0;
        for (const Row& row : fRows) {
            dataSize += (*row.fData).size();
        }

        if (0 == dataSize) {
            return (*target).setEmpty();
        }

        SkASSERT(fMinY >= fBounds.fTop);
        SkASSERT(fMinY < fBounds.fBottom);
        const int adjustY = fMinY - fBounds.fTop;
        fBounds.fTop = fMinY;

        RunHead* head = RunHead::Alloc((*this).rowCount(), dataSize);
        YOffset* yoffset = (*head).yoffsets();
        uint8_t* data = (*head).data();
        size_t offset = 0;

#ifdef SK_DEBUG
        int prevY = fRows[0].fY - 1;
#endif
        for (size_t rowIndex = 0; rowIndex < fRows.size(); ++rowIndex) {
            const Row& row = fRows[rowIndex];
#ifdef SK_DEBUG
            SkASSERT(prevY < row.fY);  // must be monotonic
            prevY = row.fY;
#endif

            yoffset[rowIndex].fY = row.fY - adjustY;
            yoffset[rowIndex].fOffset = SkToU32(offset);

            const size_t n = (*row.fData).size();
            memcpy(data + offset, (*row.fData).begin(), n);
#ifdef SK_DEBUG
            SkASSERT(compute_row_length(data + offset, fBounds.width()) == n);
#endif
            offset += n;
        }

        (*target).freeRuns();
        (*target).fBounds = fBounds;
        (*target).fRunHead = head;
        const bool nonEmpty = (*target).trimBounds();
        return nonEmpty;
    }

    void validate() {
#ifdef SK_DEBUG
        int prevY = -1;
        for (const Row& row : fRows) {
            SkASSERT(prevY < row.fY);
            SkASSERT(fWidth == row.fWidth);
            const int count = (*row.fData).size();
            const uint8_t* bytes = (*row.fData).begin();
            SkASSERT(!(count & 1));
            int w = 0;
            for (int x = 0; x < count; x += 2) {
                const int n = bytes[x];
                SkASSERT(n > 0);
                w += n;
                SkASSERT(w <= fWidth);
            }
            SkASSERT(w == fWidth);
            prevY = row.fY;
        }
#endif
    }

    void flushRowH(Row& row) {
        // flush current row if needed
        if (row.fWidth < fWidth) {
            AppendRun(*row.fData, 0, fWidth - row.fWidth);
            row.fWidth = fWidth;
        }
    }

    // Completes the last row, merging it into the previous row when the two have the same runs.
    // When readyForAnother is set, returns the index of an empty row to fill next (the merged
    // row, reused, or a new one); otherwise returns kNoRow.
    int flushRow(bool readyForAnother) {
        int next = kNoRow;
        const int count = (*this).rowCount();
        if (count > 0) {
            (*this).flushRowH(fRows[count - 1]);
        }
        if (count > 1) {
            // are our last two runs the same?
            Row& prev = fRows[count - 2];
            Row& curr = fRows[count - 1];
            SkASSERT(prev.fWidth == fWidth);
            SkASSERT(curr.fWidth == fWidth);
            if (*prev.fData == *curr.fData) {
                prev.fY = curr.fY;
                if (readyForAnother) {
                    (*curr.fData).clear();
                    next = count - 1;
                } else {
                    fRows.pop_back();
                }
            } else {
                if (readyForAnother) {
                    next = (*this).appendRow();
                }
            }
        } else {
            if (readyForAnother) {
                next = (*this).appendRow();
            }
        }
        return next;
    }

    int appendRow() {
        Row row;
        row.fData = std::make_unique<SkTDArray<uint8_t>>();
        fRows.push_back(std::move(row));
        const int index = (*this).rowCount() - 1;
        return index;
    }

    // There is at most one row per scanline of the bounds, so the count fits an int; the check
    // keeps that visible.
    int rowCount() const {
        const size_t rows = fRows.size();
        SkASSERT_RELEASE(rows <= static_cast<size_t>(SK_MaxS32));
        return static_cast<int>(rows);
    }

    static void AppendRun(SkTDArray<uint8_t>& data, U8CPU alpha, int count) {
        int remaining = count;
        do {
            const int n = std::min(remaining, 255);
            uint8_t* pair = data.append(2);
            pair[0] = static_cast<uint8_t>(n);
            pair[1] = static_cast<uint8_t>(alpha);
            remaining -= n;
        } while (remaining > 0);
    }
};

// Moves a row iterator past the run that ends at rite, if any.
static void advance_row_iter(RowIter& iter, int& iterLeft, int& iterRite, int rite) {
    if (rite == iterRite) {
        iter.next();
        iterLeft = iter.left();
        iterRite = iter.right();
    }
}

void SkAAClip::Builder::operateX(int lastY, RowIter& iterA, RowIter& iterB, AlphaProc proc) {
    int leftA = iterA.left();
    int riteA = iterA.right();
    int leftB = iterB.left();
    int riteB = iterB.right();

    int prevRite = fBounds.fLeft;

    do {
        U8CPU alphaA = 0;
        U8CPU alphaB = 0;
        int left = 0;
        int rite = 0;

        if (leftA < leftB) {
            left = leftA;
            alphaA = iterA.alpha();
            if (riteA <= leftB) {
                rite = riteA;
            } else {
                leftA = leftB;
                rite = leftB;
            }
        } else if (leftB < leftA) {
            left = leftB;
            alphaB = iterB.alpha();
            if (riteB <= leftA) {
                rite = riteB;
            } else {
                leftB = leftA;
                rite = leftA;
            }
        } else {
            left = leftA;   // or leftB, since leftA == leftB
            const int sharedRite = std::min(riteA, riteB);
            leftA = sharedRite;
            leftB = sharedRite;
            rite = sharedRite;
            alphaA = iterA.alpha();
            alphaB = iterB.alpha();
        }

        if (left >= fBounds.fRight) {
            break;
        }
        if (rite > fBounds.fRight) {
            rite = fBounds.fRight;
        }

        if (left >= fBounds.fLeft) {
            SkASSERT(rite > left);
            (*this).addRun(left, lastY, proc(alphaA, alphaB), rite - left);
            prevRite = rite;
        }

        advance_row_iter(iterA, leftA, riteA, rite);
        advance_row_iter(iterB, leftB, riteB, rite);
    } while (!iterA.done() || !iterB.done());

    if (prevRite < fBounds.fRight) {
        (*this).addRun(prevRite, lastY, 0, fBounds.fRight - prevRite);
    }
}

static U8CPU alpha_difference(U8CPU a, U8CPU b) {
    const U8CPU alpha = SkMulDiv255Round(a, 0xFF - b);
    return alpha;
}

static U8CPU alpha_intersect(U8CPU a, U8CPU b) {
    const U8CPU alpha = SkMulDiv255Round(a, b);
    return alpha;
}

// Moves a clip row iterator past the rows that end at bot, if any.
static void advance_iter(Iter& iter, int& iterTop, int& iterBot, int bot) {
    if (bot == iterBot) {
        iter.next();
        iterTop = iterBot;
        SkASSERT(iterBot == iter.top());
        iterBot = iter.bottom();
    }
}

void SkAAClip::Builder::operateY(const SkAAClip& A, const SkAAClip& B, SkClipOp op) {
    const AlphaProc proc = (op == SkClipOp::kDifference) ? alpha_difference : alpha_intersect;

    Iter iterA = RunHead::Iterate(A);
    Iter iterB = RunHead::Iterate(B);

    SkASSERT(!iterA.done());
    int topA = iterA.top();
    int botA = iterA.bottom();
    SkASSERT(!iterB.done());
    int topB = iterB.top();
    int botB = iterB.bottom();

    do {
        const uint8_t* rowA = nullptr;
        const uint8_t* rowB = nullptr;
        int top = 0;
        int bot = 0;

        if (topA < topB) {
            top = topA;
            rowA = iterA.data();
            if (botA <= topB) {
                bot = botA;
            } else {
                topA = topB;
                bot = topB;
            }

        } else if (topB < topA) {
            top = topB;
            rowB = iterB.data();
            if (botB <= topA) {
                bot = botB;
            } else {
                topB = topA;
                bot = topA;
            }
        } else {
            top = topA;   // or topB, since topA == topB
            const int sharedBot = std::min(botA, botB);
            topA = sharedBot;
            topB = sharedBot;
            bot = sharedBot;
            rowA = iterA.data();
            rowB = iterB.data();
        }

        if (top >= fBounds.fBottom) {
            break;
        }

        if (bot > fBounds.fBottom) {
            bot = fBounds.fBottom;
        }
        SkASSERT(top < bot);

        if (!rowA && !rowB) {
            (*this).addRun(fBounds.fLeft, bot - 1, 0, fBounds.width());
        } else if (top >= fBounds.fTop) {
            SkASSERT(bot <= fBounds.fBottom);
            RowIter rowIterA(rowA, rowA ? A.getBounds() : fBounds);
            RowIter rowIterB(rowB, rowB ? B.getBounds() : fBounds);
            (*this).operateX(bot - 1, rowIterA, rowIterB, proc);
        }

        advance_iter(iterA, topA, botA, bot);
        advance_iter(iterB, topB, botB, bot);
    } while (!iterA.done() || !iterB.done());
}

// Receives the scan converter's output for one path and forwards it, clipped to the builder's
// bounds, as runs. The builder is borrowed and must outlive the blitter.
class SkAAClip::Builder::Blitter final : public SkBlitter {
    int fLastY = -SK_MaxS32;    // sentinel

    /*
        If we see a gap of 1 or more empty scanlines while building in Y-order,
        we inject an explicit empty scanline (alpha==0)

        See AAClipTest.cpp : test_path_with_hole()
     */
    void checkForYGap(int y) {
        SkASSERT(y >= fLastY);
        if (fLastY > -SK_MaxS32) {
            const int gap = y - fLastY;
            if (gap > 1) {
                (*fBuilder).addRun(fLeft, y - 1, 0, fRight - fLeft);
            }
        }
        fLastY = y;
    }

public:
    explicit Blitter(Builder* builder)
            : fBuilder(builder)
            , fLeft((*builder).fBounds.fLeft)
            , fRight((*builder).fBounds.fRight) {}

    void finish() {
        if (fMinY < SK_MaxS32) {
            (*fBuilder).fMinY = fMinY;
        }
    }

    /**
       Must evaluate clips in scan-line order, so don't want to allow blitV(),
       but an AAClip can be clipped down to a single pixel wide, so we
       must support it (given AntiRect semantics: minimum width is 2).
       Instead we'll rely on the runtime asserts to guarantee Y monotonicity;
       any failure cases that misses may have minor artifacts.
    */
    void blitV(int x, int y, int height, SkAlpha alpha) override {
        if (height == 1) {
            // We're still in scan-line order if height is 1
            // This is useful for Analytic AA
            const SkAlpha alphas[2] = {alpha, 0};
            const int16_t runs[2] = {1, 0};
            (*this).blitAntiH(x, y, alphas, runs);
        } else {
            (*this).recordMinY(y);
            (*fBuilder).addColumn(x, y, alpha, height);
            fLastY = y + height - 1;
        }
    }

    void blitRect(int x, int y, int width, int height) override {
        (*this).recordMinY(y);
        (*this).checkForYGap(y);
        (*fBuilder).addRectRun(x, y, width, height);
        fLastY = y + height - 1;
    }

    void blitAntiRect(int x, int y, int width, int height,
                      SkAlpha leftAlpha, SkAlpha rightAlpha) override {
        (*this).recordMinY(y);
        (*this).checkForYGap(y);
        (*fBuilder).addAntiRectRun(x, y, width, height, leftAlpha, rightAlpha);
        fLastY = y + height - 1;
    }

    void blitMask(const SkMask&, const SkIRect& clip) override {
        (*this).unexpected();
    }

    void blitH(int x, int y, int width) override {
        (*this).recordMinY(y);
        (*this).checkForYGap(y);
        (*fBuilder).addRun(x, y, 0xFF, width);
    }

    void blitAntiH(int x, int y, const SkAlpha alpha[], const int16_t runs[]) override {
        (*this).recordMinY(y);
        (*this).checkForYGap(y);
        int index = 0;
        while (true) {
            const int count = runs[index];
            if (count <= 0) {
                return;
            }

            // The supersampler's buffer can be the width of the device, so
            // we may have to trim the run to our bounds. Previously, we assert that
            // the extra spans are always alpha==0.
            // However, the analytic AA is too sensitive to precision errors
            // so it may have extra spans with very tiny alpha because after several
            // arithmatic operations, the edge may bleed the path boundary a little bit.
            // Therefore, instead of always asserting alpha==0, we assert alpha < 0x10.
            const int runX = x + index;
            int localX = runX;
            int localCount = count;
            if (runX < fLeft) {
                SkASSERT(0x10 > alpha[index]);
                const int gap = fLeft - runX;
                SkASSERT(gap <= count);
                localX += gap;
                localCount -= gap;
            }
            const int right = runX + count;
            if (right > fRight) {
                SkASSERT(0x10 > alpha[index]);
                localCount -= right - fRight;
                SkASSERT(localCount >= 0);
            }

            if (localCount) {
                (*fBuilder).addRun(localX, y, alpha[index], localCount);
            }
            // Next run
            index += count;
        }
    }

private:
    Builder* fBuilder = nullptr;
    int      fLeft = 0; // cache of builder's bounds' left edge
    int      fRight = 0;
    int      fMinY = SK_MaxS32;

    /*
     *  We track this, in case the scan converter skipped some number of
     *  scanlines at the (relative to the bounds it was given). This allows
     *  the builder, during its finish, to trip its bounds down to the "real"
     *  top.
     */
    void recordMinY(int y) {
        if (y < fMinY) {
            fMinY = y;
        }
    }

    void unexpected() {
        SK_ABORT("---- did not expect to get called here");
    }
};

bool SkAAClip::Builder::applyClipOp(SkAAClip* target, const SkAAClip& other, SkClipOp op) {
    (*this).operateY(*target, other, op);
    const bool nonEmpty = (*this).finish(target);
    return nonEmpty;
}

bool SkAAClip::Builder::blitPath(SkAAClip* target, const SkPath& path, bool doAA) {
    Blitter blitter(this);
    const SkRegion clip(fBounds);

    const std::optional<SkPathRaw> resolved = SkPathPriv::Raw(path, SkResolveConvexity::kYes);
    const SkPathRaw raw = resolved.value_or(SkPathRaw::Empty(path.getFillType()));
    if (doAA) {
        SkScan::AntiFillPath(raw, clip, &blitter, true);
    } else {
        SkScan::FillPath(raw, clip, &blitter);
    }
    blitter.finish();
    const bool nonEmpty = (*this).finish(target);
    return nonEmpty;
}

///////////////////////////////////////////////////////////////////////////////

// Expands one row of runs into width coverage bytes at dst.
static void expand_row_to_mask(uint8_t* dst, const uint8_t* row, int width) {
    size_t written = 0;
    size_t index = 0;
    int remaining = width;
    while (remaining > 0) {
        const int n = row[index];
        SkASSERT(remaining >= n);
        memset(dst + written, row[index + 1], static_cast<size_t>(n));
        written += static_cast<size_t>(n);
        index += 2;
        remaining -= n;
    }
    SkASSERT(0 == remaining);
}

void SkAAClip::copyToMask(SkMaskBuilder* mask) const {
    (*mask).format() = SkMask::kA8_Format;
    if ((*this).isEmpty()) {
        (*mask).bounds().setEmpty();
        (*mask).image() = nullptr;
        (*mask).rowBytes() = 0;
        return;
    }

    (*mask).bounds() = fBounds;
    (*mask).rowBytes() = fBounds.width();
    const size_t size = (*mask).computeImageSize();
    if (size == 0) {
        // A nonempty clip exceeded the mask's representable image extent.
        (*mask).bounds().setEmpty();
        (*mask).rowBytes() = 0;
        (*mask).image() = nullptr;
        return;
    }
    (*mask).image() = SkMaskBuilder::AllocImage(size);

    Iter iter = RunHead::Iterate(*this);
    uint8_t* image = (*mask).image();
    const size_t rowBytes = (*mask).fRowBytes;
    const int width = fBounds.width();

    int y = fBounds.fTop;
    size_t rowIndex = 0;
    while (!iter.done()) {
        do {
            expand_row_to_mask(image + rowIndex * rowBytes, iter.data(), width);
            rowIndex += 1;
            y += 1;
        } while (y < iter.bottom());
        iter.next();
    }
}

#ifdef SK_DEBUG

void SkAAClip::validate() const {
    if (nullptr == fRunHead) {
        SkASSERT(fBounds.isEmpty());
        return;
    }
    SkASSERT(!fBounds.isEmpty());

    const RunHead* head = fRunHead;
    SkASSERT((*head).fRefCnt.load() > 0);
    SkASSERT((*head).fRowCount > 0);

    const YOffset* yoff = (*head).yoffsets();
    const int lastY = fBounds.height() - 1;

    // Y and offset must be monotonic
    int prevY = -1;
    int64_t prevOffset = -1;
    for (int i = 0; i < (*head).fRowCount; ++i) {
        SkASSERT(prevY < yoff[i].fY);
        SkASSERT(yoff[i].fY <= lastY);
        prevY = yoff[i].fY;
        SkASSERT(prevOffset < static_cast<int64_t>(yoff[i].fOffset));
        prevOffset = yoff[i].fOffset;
        const uint8_t* row = (*head).data() + yoff[i].fOffset;
        const size_t rowLength = compute_row_length(row, fBounds.width());
        SkASSERT(yoff[i].fOffset + rowLength <= (*head).fDataSize);
    }
    // check the last entry;
    SkASSERT(yoff[(*head).fRowCount - 1].fY == lastY);
}

#endif

///////////////////////////////////////////////////////////////////////////////

// Count the number of zeros on the left and right edges of the passed in
// RLE row. If 'row' is all zeros return 'width' in both variables.
static void count_left_right_zeros(const uint8_t* row, int width,
                                   int* leftZ, int* riteZ) {
    int zeros = 0;
    size_t index = 0;
    int remaining = width;
    do {
        if (row[index + 1]) {
            break;
        }
        const int n = row[index];
        SkASSERT(n > 0);
        SkASSERT(n <= remaining);
        zeros += n;
        index += 2;
        remaining -= n;
    } while (remaining > 0);
    *leftZ = zeros;

    if (0 == remaining) {
        // this line is completely empty return 'width' in both variables
        *riteZ = *leftZ;
        return;
    }

    zeros = 0;
    while (remaining > 0) {
        const int n = row[index];
        SkASSERT(n > 0);
        if (0 == row[index + 1]) {
            zeros += n;
        } else {
            zeros = 0;
        }
        index += 2;
        remaining -= n;
    }
    *riteZ = zeros;
}

// modify row in place, trimming off (zeros) from the left and right sides.
// return the number of bytes that were completely eliminated from the left
static int trim_row_left_right(uint8_t* row, int width, int leftZ, int riteZ) {
    int trim = 0;
    size_t index = 0;
    int remaining = width;
    int leftZeros = leftZ;
    while (leftZeros > 0) {
        SkASSERT(0 == row[index + 1]);
        const int n = row[index];
        SkASSERT(n > 0);
        SkASSERT(n <= remaining);
        remaining -= n;
        if (n > leftZeros) {
            row[index] = static_cast<uint8_t>(n - leftZeros);
            index += 2;
            break;
        }
        index += 2;
        trim += 2;
        leftZeros -= n;
        SkASSERT(leftZeros >= 0);
    }

    int riteZeros = riteZ;
    if (riteZeros) {
        // walk row to the end, and then we'll back up to trim riteZ
        while (remaining > 0) {
            const int n = row[index];
            SkASSERT(n <= remaining);
            remaining -= n;
            index += 2;
        }
        // now skip whole runs of zeros
        do {
            index -= 2;
            SkASSERT(0 == row[index + 1]);
            const int n = row[index];
            SkASSERT(n > 0);
            if (n > riteZeros) {
                row[index] = static_cast<uint8_t>(n - riteZeros);
                break;
            }
            riteZeros -= n;
            SkASSERT(riteZeros >= 0);
        } while (riteZeros > 0);
    }

    return trim;
}

bool SkAAClip::trimLeftRight() {
    if ((*this).isEmpty()) {
        return false;
    }

    const AutoAAClipValidate validateOnExit(*this);

    const int width = fBounds.width();
    RunHead* head = fRunHead;
    YOffset* yoff = (*head).yoffsets();
    const int rowCount = (*head).fRowCount;
    uint8_t* base = (*head).data();

    // After this loop, 'leftZeros' & 'rightZeros' will contain the minimum
    // number of zeros on the left and right of the clip. This information
    // can be used to shrink the bounding box.
    int leftZeros = width;
    int riteZeros = width;
    for (int i = 0; i < rowCount; ++i) {
        int L = 0;
        int R = 0;
        count_left_right_zeros(base + yoff[i].fOffset, width, &L, &R);
        SkASSERT(L + R < width || (L == width && R == width));
        if (L < leftZeros) {
            leftZeros = L;
        }
        if (R < riteZeros) {
            riteZeros = R;
        }
        if (0 == (leftZeros | riteZeros)) {
            // no trimming to do
            return true;
        }
    }

    SkASSERT(leftZeros || riteZeros);
    if (width == leftZeros) {
        SkASSERT(width == riteZeros);
        return (*this).setEmpty();
    }

    (*this).validate();

    fBounds.fLeft += leftZeros;
    fBounds.fRight -= riteZeros;
    SkASSERT(!fBounds.isEmpty());

    // For now we don't realloc the storage (for time), we just shrink in place
    // This means we don't have to do any memmoves either, since we can just
    // play tricks with the yoff->fOffset for each row
    for (int i = 0; i < rowCount; ++i) {
        uint8_t* row = base + yoff[i].fOffset;
#ifdef SK_DEBUG
        (void)compute_row_length(row, width);
#endif
        yoff[i].fOffset += trim_row_left_right(row, width, leftZeros, riteZeros);
#ifdef SK_DEBUG
        (void)compute_row_length(base + yoff[i].fOffset, width - leftZeros - riteZeros);
#endif
    }
    return true;
}

static bool row_is_all_zeros(const uint8_t* row, int width) {
    SkASSERT(width > 0);
    size_t index = 0;
    int remaining = width;
    do {
        if (row[index + 1]) {
            return false;
        }
        const int n = row[index];
        SkASSERT(n <= remaining);
        remaining -= n;
        index += 2;
    } while (remaining > 0);
    SkASSERT(0 == remaining);
    return true;
}

bool SkAAClip::trimTopBottom() {
    if ((*this).isEmpty()) {
        return false;
    }

    (*this).validate();

    const int width = fBounds.width();
    RunHead* head = fRunHead;
    YOffset* yoff = (*head).yoffsets();
    const uint8_t* base = (*head).data();

    //  Look to trim away empty rows from the top.
    //
    int skip = 0;
    while (skip < (*head).fRowCount) {
        const uint8_t* data = base + yoff[skip].fOffset;
        if (!row_is_all_zeros(data, width)) {
            break;
        }
        skip += 1;
    }
    SkASSERT(skip <= (*head).fRowCount);
    if (skip == (*head).fRowCount) {
        return (*this).setEmpty();
    }
    if (skip > 0) {
        // adjust fRowCount and fBounds.fTop, and slide all the data up
        // as we remove [skip] number of YOffset entries
        const int dy = yoff[skip - 1].fY + 1;
        for (int i = skip; i < (*head).fRowCount; ++i) {
            SkASSERT(yoff[i].fY >= dy);
            yoff[i].fY -= dy;
        }
        // The records and the data are contiguous; both move up by the removed records.
        const size_t recordBytes = static_cast<size_t>((*head).fRowCount) * sizeof(YOffset);
        const size_t size = recordBytes + (*head).fDataSize;
        const size_t removedBytes = static_cast<size_t>(skip) * sizeof(YOffset);
        memmove(yoff, yoff + skip, size - removedBytes);

        fBounds.fTop += dy;
        SkASSERT(!fBounds.isEmpty());
        (*head).fRowCount -= skip;
        SkASSERT((*head).fRowCount > 0);

        (*this).validate();
        // need to reset this after the memmove
        base = (*head).data();
    }

    //  Look to trim away empty rows from the bottom.
    //  We know that we have at least one non-zero row, so we can just walk
    //  backwards without checking for running past the start.
    //
    const int rowCount = (*head).fRowCount;
    int last = rowCount;
    do {
        last -= 1;
    } while (row_is_all_zeros(base + yoff[last].fOffset, width));
    skip = rowCount - last - 1;
    SkASSERT(skip >= 0 && skip < (*head).fRowCount);
    if (skip > 0) {
        // removing from the bottom is easier than from the top, as we don't
        // have to adjust any of the Y values, we just have to trim the array
        YOffset* stop = yoff + rowCount;
        memmove(stop - skip, stop, (*head).fDataSize);

        fBounds.fBottom = fBounds.fTop + yoff[last].fY + 1;
        SkASSERT(!fBounds.isEmpty());
        (*head).fRowCount -= skip;
        SkASSERT((*head).fRowCount > 0);
    }
    (*this).validate();

    return true;
}

// can't validate before we're done, since trimming is part of the process of
// making us valid after the Builder. Since we build from top to bottom, its
// possible our fBounds.fBottom is bigger than our last scanline of data, so
// we trim fBounds.fBottom back up.
//
// TODO: check for duplicates in X and Y to further compress our data
//
bool SkAAClip::trimBounds() {
    if ((*this).isEmpty()) {
        return false;
    }

    const RunHead* head = fRunHead;
    const YOffset* yoff = (*head).yoffsets();

    SkASSERT((*head).fRowCount > 0);
    const YOffset& lastY = yoff[(*head).fRowCount - 1];
    SkASSERT(lastY.fY + 1 <= fBounds.height());
    fBounds.fBottom = fBounds.fTop + lastY.fY + 1;
    SkASSERT(lastY.fY + 1 == fBounds.height());
    SkASSERT(!fBounds.isEmpty());

    if (!(*this).trimTopBottom()) {
        return false;
    }
    const bool nonEmpty = (*this).trimLeftRight();
    return nonEmpty;
}

///////////////////////////////////////////////////////////////////////////////

SkAAClip::SkAAClip() = default;

SkAAClip::SkAAClip(const SkAAClip& src) {
    *this = src;
}

SkAAClip::~SkAAClip() {
    (*this).freeRuns();
}

SkAAClip& SkAAClip::operator=(const SkAAClip& src) {
    const AutoAAClipValidate validateOnExit(*this);
    src.validate();

    if (this != &src) {
        (*this).freeRuns();
        fBounds = src.fBounds;
        fRunHead = src.fRunHead;
        if (fRunHead) {
            (*fRunHead).fRefCnt++;
        }
    }
    return *this;
}

bool SkAAClip::setEmpty() {
    (*this).freeRuns();
    fBounds.setEmpty();
    fRunHead = nullptr;
    return false;
}

bool SkAAClip::setRect(const SkIRect& bounds) {
    if (bounds.isEmpty()) {
        return (*this).setEmpty();
    }

    const AutoAAClipValidate validateOnExit(*this);

    (*this).freeRuns();
    fBounds = bounds;
    fRunHead = RunHead::AllocRect(bounds);
    SkASSERT(!(*this).isEmpty());
    return true;
}

bool SkAAClip::isRect() const {
    if ((*this).isEmpty()) {
        return false;
    }

    const RunHead* head = fRunHead;
    if ((*head).fRowCount != 1) {
        return false;
    }
    const YOffset* yoff = (*head).yoffsets();
    if ((*yoff).fY != fBounds.fBottom - 1) {
        return false;
    }

    const uint8_t* row = (*head).data() + (*yoff).fOffset;
    size_t index = 0;
    int width = fBounds.width();
    do {
        if (row[index + 1] != 0xFF) {
            return false;
        }
        const int n = row[index];
        SkASSERT(n <= width);
        width -= n;
        index += 2;
    } while (width > 0);
    return true;
}

// The byte offset at which the next row's runs start. SkTDArray counts are nonnegative ints.
static uint32_t next_row_offset(const SkTDArray<uint8_t>& xArray) {
    const int bytes = xArray.size();
    SkASSERT_RELEASE(bytes >= 0);
    return static_cast<uint32_t>(bytes);
}

// Appends runs of value covering count pixels, at most 255 pixels per run.
static void append_x_run(SkTDArray<uint8_t>& xArray, uint8_t value, int count) {
    SkASSERT(count >= 0);
    int remaining = count;
    while (remaining > 0) {
        const int n = std::min(remaining, 255);
        uint8_t* data = xArray.append(2);
        data[0] = static_cast<uint8_t>(n);
        data[1] = value;
        remaining -= n;
    }
}

bool SkAAClip::setRegion(const SkRegion& rgn) {
    if (rgn.isEmpty()) {
        return (*this).setEmpty();
    }
    if (rgn.isRect()) {
        return (*this).setRect(rgn.getBounds());
    }


    const SkIRect& bounds = rgn.getBounds();
    const int offsetX = bounds.fLeft;
    const int offsetY = bounds.fTop;

    SkTDArray<YOffset> yArray;
    SkTDArray<uint8_t> xArray;

    yArray.reserve(std::min(bounds.height(), 1024));
    xArray.reserve(std::min(bounds.width(), 512) * 128);

    SkRegion::Iterator iter(rgn);
    int prevRight = 0;
    int prevBot = 0;
    bool haveRow = false;

    for (; !iter.done(); iter.next()) {
        const SkIRect& r = iter.rect();
        SkASSERT(bounds.contains(r));

        const int bot = r.fBottom - offsetY;
        SkASSERT(bot >= prevBot);
        if (bot > prevBot) {
            if (haveRow) {
                // flush current row
                append_x_run(xArray, 0, bounds.width() - prevRight);
            }
            // did we introduce an empty-gap from the prev row?
            const int top = r.fTop - offsetY;
            if (top > prevBot) {
                YOffset* gapY = yArray.append();
                (*gapY).fY = top - 1;
                (*gapY).fOffset = next_row_offset(xArray);
                append_x_run(xArray, 0, bounds.width());
            }
            // create a new record for this Y value
            YOffset* currY = yArray.append();
            (*currY).fY = bot - 1;
            (*currY).fOffset = next_row_offset(xArray);
            haveRow = true;
            prevRight = 0;
            prevBot = bot;
        }

        const int x = r.fLeft - offsetX;
        append_x_run(xArray, 0, x - prevRight);

        const int w = r.fRight - r.fLeft;
        append_x_run(xArray, 0xFF, w);
        prevRight = x + w;
        SkASSERT(prevRight <= bounds.width());
    }
    // flush last row
    append_x_run(xArray, 0, bounds.width() - prevRight);

    // now pack everything into a RunHead
    RunHead* head = RunHead::Alloc(yArray.size(), xArray.size_bytes());
    memcpy((*head).yoffsets(), yArray.begin(), yArray.size_bytes());
    memcpy((*head).data(), xArray.begin(), xArray.size_bytes());

    (*this).setEmpty();
    fBounds = bounds;
    fRunHead = head;
    (*this).validate();
    return true;
}

bool SkAAClip::setPath(const SkPath& path, const SkIRect& clip, bool doAA) {
    const AutoAAClipValidate validateOnExit(*this);

    if (clip.isEmpty()) {
        return (*this).setEmpty();
    }

    SkIRect ibounds = {0, 0, 0, 0};
    // Since we assert that the BuilderBlitter will never blit outside the intersection
    // of clip and ibounds, we create the builder with the snug bounds.
    if (path.isInverseFillType()) {
        ibounds = clip;
    } else {
        path.getBounds().roundOut(&ibounds);
        // It's possible the bounds of our path might exceed SK_MaxS32 in width
        // but since our clip is within that width (otherwise isEmpty() above
        // would catch it), we can use isEmpty64() safely here. blitPath will
        // interesect the two bounds before drawing.
        if (ibounds.isEmpty64() || !ibounds.intersect(clip)) {
            return (*this).setEmpty();
        }
    }

    Builder builder(ibounds);
    const bool nonEmpty = builder.blitPath(this, path, doAA);
    return nonEmpty;
}

///////////////////////////////////////////////////////////////////////////////

bool SkAAClip::op(const SkAAClip& other, SkClipOp op) {
    const AutoAAClipValidate validateOnExit(*this);

    if ((*this).isEmpty()) {
        // Once the clip is empty, it cannot become un-empty.
        return false;
    }

    SkIRect bounds = fBounds;
    switch(op) {
        case SkClipOp::kDifference:
            if (other.isEmpty() || !SkIRect::Intersects(fBounds, other.fBounds)) {
                // this remains unmodified and isn't empty
                return true;
            }
            break;

        case SkClipOp::kIntersect:
            if (other.isEmpty() || !bounds.intersect(other.fBounds)) {
                // the intersected clip becomes empty
                return (*this).setEmpty();
            }
            break;
    }


    SkASSERT(SkIRect::Intersects(bounds, fBounds));
    SkASSERT(SkIRect::Intersects(bounds, other.fBounds));

    // Any operator other than difference intersects, as in operateY.
    Builder builder(bounds);
    const bool nonEmpty = builder.applyClipOp(this, other, op);
    return nonEmpty;
}

bool SkAAClip::op(const SkIRect& rect, SkClipOp op) {
    // It can be expensive to build a local aaclip before applying the op, so
    // we first see if we can restrict the bounds of new rect to our current
    // bounds, or note that the new rect subsumes our current clip.
    SkIRect pixelBounds = fBounds;
    if (!pixelBounds.intersect(rect)) {
        // No change or clip becomes empty depending on 'op'
        switch(op) {
            case SkClipOp::kDifference: return !(*this).isEmpty();
            case SkClipOp::kIntersect:  return (*this).setEmpty();
        }
        SkUNREACHABLE;
    } else if (pixelBounds == fBounds) {
        // Wholly inside 'rect', so clip becomes empty or remains unchanged
        switch(op) {
            case SkClipOp::kDifference: return (*this).setEmpty();
            case SkClipOp::kIntersect:  return !(*this).isEmpty();
        }
        SkUNREACHABLE;
    } else if (op == SkClipOp::kIntersect && (*this).quickContains(pixelBounds)) {
        // We become just the remaining rectangle
        return (*this).setRect(pixelBounds);
    } else {
        SkAAClip clip;
        clip.setRect(rect);
        const bool nonEmpty = (*this).op(clip, op);
        return nonEmpty;
    }
}

bool SkAAClip::op(const SkRect& rect, SkClipOp op, bool doAA) {
    if (!doAA) {
        const bool nonEmpty = (*this).op(rect.round(), op);
        return nonEmpty;
    }
    // Tighten bounds for "path" aaclip of the rect
    SkIRect pixelBounds = fBounds;
    if (!pixelBounds.intersect(rect.roundOut())) {
        // No change or clip becomes empty depending on 'op'
        switch(op) {
            case SkClipOp::kDifference: return !(*this).isEmpty();
            case SkClipOp::kIntersect:  return (*this).setEmpty();
        }
        SkUNREACHABLE;
    } else if (rect.contains(SkRect::Make(fBounds))) {
        // Wholly inside 'rect', so clip becomes empty or remains unchanged
        switch(op) {
            case SkClipOp::kDifference: return (*this).setEmpty();
            case SkClipOp::kIntersect:  return !(*this).isEmpty();
        }
        SkUNREACHABLE;
    } else if (op == SkClipOp::kIntersect && (*this).quickContains(pixelBounds)) {
        // We become just the rect intersected with pixel bounds (preserving fractional coords
        // for AA edges).
        const bool nonEmpty = (*this).setPath(SkPath::Rect(rect), pixelBounds, /*doAA=*/true);
        return nonEmpty;
    } else {
        SkAAClip rectClip;
        const SkIRect& rectBounds = op == SkClipOp::kDifference ? fBounds : pixelBounds;
        rectClip.setPath(SkPath::Rect(rect), rectBounds, /*doAA=*/true);
        const bool nonEmpty = (*this).op(rectClip, op);
        return nonEmpty;
    }
}

///////////////////////////////////////////////////////////////////////////////

// Preserve the run dimensions when translation approaches the signed coordinate limits.
// Saturating each edge separately can produce empty bounds around live coverage runs.
static int pin_aa_offset(int minimum, int maximum, int offset) {
    SkASSERT(minimum <= maximum);
    const int64_t shiftedMinimum = static_cast<int64_t>(minimum) + offset;
    if (shiftedMinimum < SK_MinS32) {
        const int64_t pinned = static_cast<int64_t>(SK_MinS32) - minimum;
        return static_cast<int>(pinned);
    }
    const int64_t shiftedMaximum = static_cast<int64_t>(maximum) + offset;
    if (shiftedMaximum > SK_MaxS32) {
        const int64_t pinned = static_cast<int64_t>(SK_MaxS32) - maximum;
        return static_cast<int>(pinned);
    }
    return offset;
}

bool SkAAClip::translate(int dx, int dy, SkAAClip* dst) const {
    if (nullptr == dst) {
        const bool nonEmpty = !(*this).isEmpty();
        return nonEmpty;
    }

    if ((*this).isEmpty()) {
        return (*dst).setEmpty();
    }

    const int appliedDx = pin_aa_offset(fBounds.fLeft, fBounds.fRight, dx);
    const int appliedDy = pin_aa_offset(fBounds.fTop, fBounds.fBottom, dy);

    if (this != dst) {
        (*fRunHead).fRefCnt++;
        (*dst).freeRuns();
        (*dst).fRunHead = fRunHead;
        (*dst).fBounds = fBounds;
    }
    (*dst).fBounds.offset(appliedDx, appliedDy);
    return true;
}

void SkAAClip::freeRuns() {
    if (fRunHead) {
        SkASSERT((*fRunHead).fRefCnt.load() >= 1);
        const int32_t previous = (*fRunHead).fRefCnt--;
        if (1 == previous) {
            sk_free(fRunHead);
        }
    }
}

const uint8_t* SkAAClip::findRow(int y, int* lastYForRow) const {
    SkASSERT(fRunHead);

    if (y < fBounds.fTop || y >= fBounds.fBottom) {
        return nullptr;
    }
    const int localY = y - fBounds.y();  // our yoffs values are relative to the top

    // The last row ends at the bottom of the bounds, so the search stops there at the latest.
    const YOffset* yoff = (*fRunHead).yoffsets();
    const int lastRow = (*fRunHead).fRowCount - 1;
    int row = 0;
    while (row < lastRow && yoff[row].fY < localY) {
        row += 1;
    }

    if (lastYForRow) {
        *lastYForRow = fBounds.y() + yoff[row].fY;
    }
    const uint8_t* rowData = (*fRunHead).data() + yoff[row].fOffset;
    return rowData;
}

const uint8_t* SkAAClip::findX(const uint8_t data[], int x, int* initialCount) const {
    SkASSERT(x >= fBounds.fLeft && x < fBounds.fRight);
    int localX = x - fBounds.x();

    // first skip up to X
    size_t index = 0;
    while (true) {
        const int n = data[index];
        if (localX < n) {
            if (initialCount) {
                *initialCount = n - localX;
            }
            break;
        }
        index += 2;
        localX -= n;
    }
    const uint8_t* run = data + index;
    return run;
}

bool SkAAClip::quickContains(int left, int top, int right, int bottom) const {
    if ((*this).isEmpty()) {
        return false;
    }
    if (!fBounds.contains(SkIRect{left, top, right, bottom})) {
        return false;
    }

    int lastY = 0;
    const uint8_t* row = (*this).findRow(top, &lastY);
    if (lastY < bottom) {
        return false;
    }
    // now just need to check in X
    int count = 0;
    row = (*this).findX(row, left, &count);

    int rectWidth = right - left;
    size_t index = 0;
    while (0xFF == row[index + 1]) {
        if (count >= rectWidth) {
            return true;
        }
        rectWidth -= count;
        index += 2;
        count = row[index];
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////

// Writes width pixels of runs and coverage, starting with the clipped first run of data. The
// runs and coverage are distinct parts of the blitter's scratch storage.
static void expandToRuns(const uint8_t* SK_RESTRICT data, int initialCount, int width,
                         int16_t* SK_RESTRICT runs, SkAlpha* SK_RESTRICT aa) {
    // we don't read our initial n from data, since the caller may have had to
    // clip it, hence the initialCount parameter.
    int n = initialCount;
    int x = 0;
    size_t index = 0;
    int remaining = width;
    while (true) {
        if (n > remaining) {
            n = remaining;
        }
        SkASSERT(n > 0);
        runs[x] = static_cast<int16_t>(n);
        aa[x] = data[index + 1];
        x += n;

        index += 2;
        remaining -= n;
        if (0 == remaining) {
            break;
        }
        // load the next count
        n = data[index];
    }
    runs[x] = 0;    // sentinel
}

SkAAClipBlitter::~SkAAClipBlitter() = default;

void SkAAClipBlitter::ensureScratch(int activeWidth) {
    SkASSERT(activeWidth > 0);
    SkSafeMath size;
    // The extra entry stores the terminating run count. Four bytes per entry
    // also cover the largest mask row used by blitMask.
    const size_t count = size.add(static_cast<size_t>(activeWidth), 1);
    const size_t bytes = size.mul(count, sizeof(SkPMColor));
    if (!size.ok()) {
        sk_out_of_memory();
    }
    void* storage = fScanlineScratch.reset(bytes, SkAutoMalloc::kReuse_OnShrink);
    fRuns = static_cast<int16_t*>(storage);
    fAA = reinterpret_cast<SkAlpha*>(fRuns + count);
}

void SkAAClipBlitter::blitH(int x, int y, int width) {
    SkASSERT(width > 0);
    SkASSERT(fAAClipBounds.contains(x, y));
    SkASSERT(fAAClipBounds.contains(x + width  - 1, y));

    const uint8_t* row = (*fAAClip).findRow(y);
    int initialCount = 0;
    row = (*fAAClip).findX(row, x, &initialCount);

    if (initialCount >= width) {
        const SkAlpha alpha = row[1];
        if (0 == alpha) {
            return;
        }
        if (0xFF == alpha) {
            (*fBlitter).blitH(x, y, width);
            return;
        }
    }

    (*this).ensureScratch(width);
    expandToRuns(row, initialCount, width, fRuns, fAA);

    (*fBlitter).blitAntiH(x, y, fAA, fRuns);
}

// Writes the product of the source coverage runs and the clip row runs. The destination is the
// blitter's scratch storage, distinct from both inputs.
static void merge(const uint8_t* SK_RESTRICT row, int rowN,
                  const SkAlpha* SK_RESTRICT srcAA,
                  const int16_t* SK_RESTRICT srcRuns,
                  SkAlpha* SK_RESTRICT dstAA,
                  int16_t* SK_RESTRICT dstRuns,
                  int width) {
#ifdef SK_DEBUG
    int accumulated = 0;
#endif
    int srcN = srcRuns[0];
    // do we need this check?
    if (0 == srcN) {
        return;
    }

    while (true) {
        SkASSERT(rowN > 0);
        SkASSERT(srcN > 0);

        const unsigned newAlpha = SkMulDiv255Round(srcAA[0], row[1]);
        const int minN = std::min(srcN, rowN);
        dstRuns[0] = static_cast<int16_t>(minN);
        dstRuns += minN;
        dstAA[0] = static_cast<SkAlpha>(newAlpha);
        dstAA += minN;

        srcN -= minN;
        if (0 == srcN) {
            srcN = srcRuns[0];  // refresh
            srcRuns += srcN;
            srcAA += srcN;
            srcN = srcRuns[0];  // reload
            if (0 == srcN) {
                break;
            }
        }
        rowN -= minN;
        if (0 == rowN) {
            row += 2;
            rowN = row[0];  // reload
        }

#ifdef SK_DEBUG
        accumulated += minN;
        SkASSERT(accumulated <= width);
#endif
    }
    dstRuns[0] = 0;
}

static int anti_run_width(const int16_t runs[], int limit) {
    int width = 0;
    while (width < limit) {
        const int count = runs[width];
        if (count == 0) {
            return width;
        }
        if (count < 0 || count > limit - width) {
            return 0;
        }
        width += count;
    }
    const int terminated = runs[width] == 0 ? width : 0;
    return terminated;
}

void SkAAClipBlitter::blitAntiH(int x, int y, const SkAlpha aa[],
                                const int16_t runs[]) {

    if (!fAAClipBounds.contains(x, y)) {
        return;
    }
    const int remaining = fAAClipBounds.fRight - x;
    const int width = anti_run_width(runs, remaining);
    if (width == 0) {
        return;
    }

    const uint8_t* row = (*fAAClip).findRow(y);
    int initialCount = 0;
    row = (*fAAClip).findX(row, x, &initialCount);

    (*this).ensureScratch(width);

    merge(row, initialCount, aa, runs, fAA, fRuns, width);
    (*fBlitter).blitAntiH(x, y, fAA, fRuns);
}

void SkAAClipBlitter::blitV(int x, int y, int height, SkAlpha alpha) {
    if ((*fAAClip).quickContains(x, y, x + 1, y + height)) {
        (*fBlitter).blitV(x, y, height, alpha);
        return;
    }

    int top = y;
    int remaining = height;
    while (true) {
        int lastY = 0;
        const uint8_t* row = (*fAAClip).findRow(top, &lastY);
        int dy = lastY - top + 1;
        if (dy > remaining) {
            dy = remaining;
        }
        remaining -= dy;

        row = (*fAAClip).findX(row, x);
        const SkAlpha newAlpha = SkMulDiv255Round(alpha, row[1]);
        if (newAlpha) {
            (*fBlitter).blitV(x, top, dy, newAlpha);
        }
        SkASSERT(remaining >= 0);
        if (remaining <= 0) {
            break;
        }
        top = lastY + 1;
    }
}

void SkAAClipBlitter::blitRect(int x, int y, int width, int height) {
    if ((*fAAClip).quickContains(x, y, x + width, y + height)) {
        (*fBlitter).blitRect(x, y, width, height);
        return;
    }

    for (int row = 0; row < height; ++row) {
        (*this).blitH(x, y + row, width);
    }
}

// Writes srcN merged mask values to dst, starting with the clipped first run of row.
using MergeAAProc = void (*)(const void* src, int width, const uint8_t* row,
                             int initialRowCount, void* dst);

static inline uint8_t mergeOne(uint8_t value, unsigned alpha) {
    const uint8_t merged = SkMulDiv255Round(value, alpha);
    return merged;
}

static inline uint16_t mergeOne(uint16_t value, unsigned alpha) {
    const unsigned r = SkGetPackedR16(value);
    const unsigned g = SkGetPackedG16(value);
    const unsigned b = SkGetPackedB16(value);
    const uint16_t merged = SkPackRGB16(SkMulDiv255Round(r, alpha),
                                        SkMulDiv255Round(g, alpha),
                                        SkMulDiv255Round(b, alpha));
    return merged;
}

// The source mask row and the destination scratch row are distinct.
template <typename T>
static void mergeT(const void* inSrc, int srcN, const uint8_t* SK_RESTRICT row, int rowN,
                   void* inDst) {
    const T* SK_RESTRICT src = static_cast<const T*>(inSrc);
    T* SK_RESTRICT       dst = static_cast<T*>(inDst);
    int remaining = srcN;
    int runN = rowN;
    int x = 0;
    size_t index = 0;
    while (true) {
        SkASSERT(runN > 0);
        SkASSERT(remaining > 0);

        const int n = std::min(runN, remaining);
        const size_t bytes = static_cast<size_t>(n) * sizeof(T);
        const unsigned rowA = row[index + 1];
        if (0xFF == rowA) {
            memcpy(dst + x, src + x, bytes);
        } else if (0 == rowA) {
            memset(dst + x, 0, bytes);
        } else {
            for (int i = 0; i < n; ++i) {
                dst[x + i] = mergeOne(src[x + i], rowA);
            }
        }

        remaining -= n;
        if (0 == remaining) {
            break;
        }

        x += n;

        SkASSERT(runN == n);
        index += 2;
        runN = row[index];
    }
}

// Returns nullptr for formats the clip cannot merge.
static MergeAAProc find_merge_aa_proc(SkMask::Format format) {
    switch (format) {
        case SkMask::kBW_Format:
            SkDEBUGFAIL("unsupported");
            return nullptr;
        case SkMask::kA8_Format:
        case SkMask::k3D_Format:
            return mergeT<uint8_t>;
        case SkMask::kLCD16_Format:
            return mergeT<uint16_t>;
        default:
            SkDEBUGFAIL("unsupported");
            return nullptr;
    }
}

static U8CPU bit2byte(int bitInAByte) {
    SkASSERT(bitInAByte <= 0xFF);
    // negation turns any non-zero into 0xFFFFFF??, so we just shift down
    // some value >= 8 to get a full FF value
    const int extended = -bitInAByte >> 8;
    return static_cast<U8CPU>(extended);
}

static void upscaleBW2A8(SkMask* dstMask, const SkMask& srcMask) {
    SkASSERT(SkMask::kBW_Format == srcMask.fFormat);
    SkASSERT(SkMask::kA8_Format == (*dstMask).fFormat);

    const int width = srcMask.fBounds.width();
    const int height = srcMask.fBounds.height();

    const uint8_t* SK_RESTRICT src = srcMask.fImage;
    const size_t srcRB = srcMask.fRowBytes;
    uint8_t* SK_RESTRICT dst = const_cast<uint8_t*>((*dstMask).fImage);
    const size_t dstRB = (*dstMask).fRowBytes;

    const int wholeBytes = width >> 3;
    const int leftOverBits = width & 7;

    for (int y = 0; y < height; ++y) {
        uint8_t* SK_RESTRICT d = dst;
        for (int i = 0; i < wholeBytes; ++i) {
            const int srcByte = src[i];
            d[0] = bit2byte(srcByte & (1 << 7));
            d[1] = bit2byte(srcByte & (1 << 6));
            d[2] = bit2byte(srcByte & (1 << 5));
            d[3] = bit2byte(srcByte & (1 << 4));
            d[4] = bit2byte(srcByte & (1 << 3));
            d[5] = bit2byte(srcByte & (1 << 2));
            d[6] = bit2byte(srcByte & (1 << 1));
            d[7] = bit2byte(srcByte & (1 << 0));
            d += 8;
        }
        if (leftOverBits) {
            int srcByte = src[wholeBytes];
            for (int x = 0; x < leftOverBits; ++x) {
                d[x] = bit2byte(srcByte & 0x80);
                srcByte <<= 1;
            }
        }
        src += srcRB;
        dst += dstRB;
    }
}

// When the full A8 image exceeds SkMask's extent, expand only the visible
// rectangle. Source bit alignment is relative to the original mask bounds.
static void upscaleBWClipToA8(SkMask* dstMask, const SkMask& srcMask) {
    const SkIRect& bounds = (*dstMask).fBounds;
    const int width = bounds.width();
    const int height = bounds.height();
    const size_t firstBit = static_cast<size_t>(
        static_cast<int64_t>(bounds.fLeft) - srcMask.fBounds.fLeft) & 7U;
    for (int y = 0; y < height; ++y) {
        const uint8_t* source = srcMask.getAddr1(bounds.fLeft, bounds.fTop + y);
        uint8_t* destination = const_cast<uint8_t*>((*dstMask).fImage) +
                               static_cast<size_t>(y) * (*dstMask).fRowBytes;
        for (int x = 0; x < width; ++x) {
            const size_t bit = firstBit + static_cast<size_t>(x);
            const uint8_t value = source[bit >> 3];
            const uint8_t mask = static_cast<uint8_t>(0x80U >> (bit & 7U));
            destination[x] = (value & mask) != 0 ? 0xFF : 0;
        }
    }
}

void SkAAClipBlitter::blitMask(const SkMask& origMask, const SkIRect& clip) {
    SkASSERT((*fAAClip).getBounds().contains(clip));
    if (clip.isEmpty()) {
        return;
    }

    if ((*fAAClip).quickContains(clip)) {
        (*fBlitter).blitMask(origMask, clip);
        return;
    }

    const SkMask* mask = &origMask;

    // if we're BW, we need to upscale to A8 (ugh)
    SkMaskBuilder  grayMask;
    if (SkMask::kBW_Format == origMask.fFormat) {
        grayMask.format() = SkMask::kA8_Format;
        grayMask.bounds() = origMask.fBounds;
        grayMask.rowBytes() = origMask.fBounds.width();
        size_t size = grayMask.computeImageSize();
        bool clippedConversion = false;
        if (size == 0) {
            grayMask.bounds() = clip;
            grayMask.rowBytes() = clip.width();
            size = grayMask.computeImageSize();
            if (size == 0) {
                return;
            }
            clippedConversion = true;
        }
        grayMask.image() = reinterpret_cast<uint8_t*>(
            fGrayMaskScratch.reset(size, SkAutoMalloc::kReuse_OnShrink));

        if (clippedConversion) {
            upscaleBWClipToA8(&grayMask, origMask);
        } else {
            upscaleBW2A8(&grayMask, origMask);
        }
        mask = &grayMask;
    }

    const MergeAAProc mergeProc = find_merge_aa_proc((*mask).fFormat);
    if (!mergeProc) {
        // The clip has no coverage merge for this format, so nothing is drawn.
        return;
    }

    const int width = clip.width();
    (*this).ensureScratch(width);

    // HACK -- we are devolving 3D into A8, need to copy the rest of the 3D
    // data into a temp block to support it better (ugh)

    SkMaskBuilder rowMask;
    rowMask.format() = SkMask::k3D_Format == (*mask).fFormat ? SkMask::kA8_Format : (*mask).fFormat;
    rowMask.bounds().fLeft = clip.fLeft;
    rowMask.bounds().fRight = clip.fRight;
    rowMask.rowBytes() = (*mask).fRowBytes; // doesn't matter, since our height==1
    rowMask.image() = static_cast<uint8_t*>(fScanlineScratch.get());

    int y = clip.fTop;
    const int stopY = y + clip.height();

    do {
        int localStopY = 0;
        const uint8_t* row = (*fAAClip).findRow(y, &localStopY);
        // findRow returns last Y, not stop, so we add 1
        localStopY = std::min(localStopY + 1, stopY);

        int initialCount = 0;
        row = (*fAAClip).findX(row, clip.fLeft, &initialCount);
        do {
            // Address each source row directly; the mask contains every row of the clip.
            const void* src = (*mask).getAddr(clip.fLeft, y);
            mergeProc(src, width, row, initialCount, rowMask.image());
            rowMask.bounds().fTop = y;
            rowMask.bounds().fBottom = y + 1;
            (*fBlitter).blitMask(rowMask, rowMask.fBounds);
            y += 1;
        } while (y < localStopY);
    } while (y < stopY);
}
