/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkCanvasPriv.h"

#include "include/core/SkBlendMode.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkShader.h"
#include "include/private/SkAlign.h"
#include "include/private/SkAssert.h"
#include "include/private/SkTo.h"
#include "src/core/SkAutoMalloc.h"
#include "src/core/SkMaskFilterBase.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"
#include "src/core/SkWriter32.h"

#include <limits>
#include <utility>
#include <cstdint>

SkAutoCanvasMatrixPaint::SkAutoCanvasMatrixPaint(SkCanvas* canvas, const SkMatrix* matrix,
                                                 const SkPaint* paint, const SkRect& bounds)
        : fCanvas(canvas)
        , fSaveCount((*canvas).getSaveCount()) {
    if (paint) {
        SkRect newBounds = bounds;
        if (matrix) {
            (*matrix).mapRect(&newBounds);
        }
        (*canvas).saveLayer(&newBounds, paint);
    } else if (matrix) {
        (*canvas).save();
    }

    if (matrix) {
        (*canvas).concat(*matrix);
    }
}

SkAutoCanvasMatrixPaint::~SkAutoCanvasMatrixPaint() {
    (*fCanvas).restoreToCount(fSaveCount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool SkCanvasPriv::ReadLattice(SkReadBuffer& buffer, SkCanvas::Lattice* lattice) {
    (*lattice).fXCount = buffer.readInt();
    if ((*lattice).fXCount < 0) {
        return false;
    }
    (*lattice).fXDivs = buffer.skipT<int32_t>((*lattice).fXCount);
    (*lattice).fYCount = buffer.readInt();
    if ((*lattice).fYCount < 0) {
        return false;
    }
    (*lattice).fYDivs = buffer.skipT<int32_t>((*lattice).fYCount);
    const int flagCount = buffer.readInt();
    (*lattice).fRectTypes = nullptr;
    (*lattice).fColors = nullptr;
    // flagCount was serialized as (xCount + 1) * (yCount + 1) if there were rect types and colors,
    // so if that isn't still the case the buffer is invalid.
    if (flagCount) {
        const uint64_t expected = (static_cast<uint64_t>((*lattice).fXCount) + 1) *
                                  (static_cast<uint64_t>((*lattice).fYCount) + 1);
        if (flagCount < 0 || static_cast<uint64_t>(flagCount) != expected) {
            return false;
        }
        (*lattice).fRectTypes = buffer.skipT<SkCanvas::Lattice::RectType>(flagCount);
        (*lattice).fColors = buffer.skipT<SkColor>(flagCount);
    }
    (*lattice).fBounds = buffer.skipT<SkIRect>();
    const bool valid = buffer.isValid();
    return valid;
}

size_t SkCanvasPriv::WriteLattice(void* buffer, const SkCanvas::Lattice& lattice) {
    const int flagCount = lattice.fRectTypes ? (lattice.fXCount + 1) * (lattice.fYCount + 1) : 0;

    const size_t size = (1 + lattice.fXCount + 1 + lattice.fYCount + 1) * sizeof(int32_t) +
                        SkAlign4(flagCount * sizeof(SkCanvas::Lattice::RectType)) +
                        SkAlign4(flagCount * sizeof(SkColor)) +
                        sizeof(SkIRect);

    if (buffer) {
        SkWriter32 writer(buffer, size);
        writer.write32(lattice.fXCount);
        writer.write(lattice.fXDivs, lattice.fXCount * sizeof(uint32_t));
        writer.write32(lattice.fYCount);
        writer.write(lattice.fYDivs, lattice.fYCount * sizeof(uint32_t));
        writer.write32(flagCount);
        writer.writePad(lattice.fRectTypes, flagCount * sizeof(uint8_t));
        writer.write(lattice.fColors, flagCount * sizeof(SkColor));
        SkASSERT(lattice.fBounds);
        writer.write(lattice.fBounds, sizeof(SkIRect));
        SkASSERT(writer.bytesWritten() == size);
    }
    return size;
}

void SkCanvasPriv::WriteLattice(SkWriteBuffer& buffer, const SkCanvas::Lattice& lattice) {
    const size_t size = WriteLattice(nullptr, lattice);
    SkAutoSMalloc<1024> storage(size);
    WriteLattice(storage.get(), lattice);
    buffer.writePad32(storage.get(), size);
}

void SkCanvasPriv::GetDstClipAndMatrixCounts(const SkCanvas::ImageSetEntry set[], int count,
                                             int* totalDstClipCount, int* totalMatrixCount) {
    int dstClipCount = 0;
    int maxMatrixIndex = -1;
    for (int i = 0; i < count; ++i) {
        dstClipCount += set[i].fHasClip ? 4 : 0;
        if (set[i].fMatrixIndex > maxMatrixIndex) {
            maxMatrixIndex = set[i].fMatrixIndex;
        }
    }

    *totalDstClipCount = dstClipCount;
    *totalMatrixCount = maxMatrixIndex + 1;
}
