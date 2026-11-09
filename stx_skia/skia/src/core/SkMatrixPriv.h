/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkMatrixPriv_DEFINE
#define SkMatrixPriv_DEFINE

#include "include/core/SkM44.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkScalar.h"
#include "include/core/SkTypes.h"

#include <cstddef>

// Matrix operations retained for serialization, path transforms, bitmap sampling, device
// clipping and stroking.
class SkMatrixPriv {
public:
    // Writes the nine matrix values and returns their size; a null buffer only queries the size.
    static size_t WriteToMemory(const SkMatrix& matrix, void* buffer) {
        const size_t size = matrix.writeToMemory(buffer);
        return size;
    }

    // Returns the bytes consumed, or 0 (leaving *matrix unchanged) if length is too short.
    static size_t ReadFromMemory(SkMatrix* matrix, const void* buffer, size_t length) {
        const size_t consumed = (*matrix).readFromMemory(buffer, length);
        return consumed;
    }

    using MapPtsProc = SkMatrix::MapPtsProc;

    // The point-mapping kernel for matrix's current type. The kernel does not retain matrix.
    static MapPtsProc GetMapPtsProc(const SkMatrix& matrix) {
        const MapPtsProc proc = SkMatrix::GetMapPtsProc(matrix.getType());
        return proc;
    }

    static bool PostIDiv(SkMatrix* matrix, int divx, int divy) {
        const bool divided = (*matrix).postIDiv(divx, divy);
        return divided;
    }

    // Borrows the sixteen column-major values of m for the lifetime of m.
    static const SkScalar* M44ColMajor(const SkM44& m) { return m.fMat; }
    static const SkScalar* M44ColMajor(const SkM44&&) = delete;

    // This is legacy functionality that only checks the 3x3 portion. The matrix could have Z-based
    // shear, or other complex behavior. Only use this if you're planning to use the information
    // to accelerate some purely 2D operation.
    static bool IsScaleTranslateAsM33(const SkM44& m) {
        const bool scaleTranslate = m.rc(1,0) == 0 && m.rc(3,0) == 0 &&
                                    m.rc(0,1) == 0 && m.rc(3,1) == 0 &&
                                    m.rc(3,3) == 1;
        return scaleTranslate;
    }

    // Map the four corners of 'r' and return the bounding box of those points. The four corners of
    // 'r' are assumed to have z = 0 and w = 1. If the matrix has perspective, the returned
    // rectangle will be the bounding box of the projected points after being clipped to w > 0.
    static SkRect MapRect(const SkM44& m, const SkRect& r);

    // The larger of the two basis-vector lengths, or 1 when that is not finite and positive.
    static SkScalar ComputeResScaleForStroking(const SkMatrix& matrix);
};

#endif
