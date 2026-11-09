/*
 * Copyright 2008 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPathMeasure.h"

#include "include/core/SkContourMeasure.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"  // IWYU pragma: keep
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/private/SkTDArray.h"
#include "src/core/SkPathMeasurePriv.h"

#include <cstddef>

class SkMatrix;

SkPathMeasure::SkPathMeasure() = default;

SkPathMeasure::SkPathMeasure(const SkPath& path, bool forceClosed, SkScalar resScale)
    : fIter(path, forceClosed, resScale)
{
    fContour = fIter.next();
}

SkPathMeasure::~SkPathMeasure() = default;

void SkPathMeasure::setPath(const SkPath* path, bool forceClosed) {
    if (path) {
        fIter.reset(*path, forceClosed);
    } else {
        fIter.reset(SkPath(), forceClosed);
    }
    fContour = fIter.next();
}

SkScalar SkPathMeasure::getLength() {
    const SkScalar length = fContour ? (*fContour).length() : 0;
    return length;
}

bool SkPathMeasure::getPosTan(SkScalar distance, SkPoint* position, SkVector* tangent) {
    const bool found = fContour && (*fContour).getPosTan(distance, position, tangent);
    return found;
}

bool SkPathMeasure::getMatrix(SkScalar distance, SkMatrix* matrix, MatrixFlags flags) {
    const bool found = fContour &&
            (*fContour).getMatrix(distance, matrix,
                                  static_cast<SkContourMeasure::MatrixFlags>(flags));
    return found;
}

bool SkPathMeasure::getSegment(SkScalar startD, SkScalar stopD, SkPathBuilder* dst,
                               bool startWithMoveTo) {
    const bool appended = fContour &&
            (*fContour).getSegment(startD, stopD, dst, startWithMoveTo);
    return appended;
}

bool SkPathMeasure::isClosed() {
    const bool closed = fContour && (*fContour).isClosed();
    return closed;
}

bool SkPathMeasure::nextContour() {
    fContour = fIter.next();
    const bool found = fContour != nullptr;
    return found;
}

#ifdef SK_DEBUG
void SkPathMeasure::dump() {}
#endif

/////

size_t SkPathMeasurePriv::CountSegments(const SkPathMeasure& meas) {
    const SkContourMeasure* contour = meas.currentMeasure();
    if (contour) {
        const size_t count = static_cast<size_t>((*contour).fSegments.size());
        return count;
    }
    return 0;
}
