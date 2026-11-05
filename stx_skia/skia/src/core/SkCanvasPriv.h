/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkCanvasPriv_DEFINED
#define SkCanvasPriv_DEFINED

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkTileMode.h"
#include "include/private/SkDebug.h"
#include "include/private/SkNoncopyable.h"

#include <cstddef>

class SkDevice;
class SkMatrix;
class SkReadBuffer;
struct SkRect;
class SkWriteBuffer;

class [[nodiscard]] SkAutoCanvasMatrixPaint : SkNoncopyable {
public:
    SkAutoCanvasMatrixPaint(SkCanvas*, const SkMatrix*, const SkPaint*, const SkRect& bounds);
    ~SkAutoCanvasMatrixPaint();

private:
    // Borrowed: the canvas outlives this object.
    SkCanvas*   fCanvas = nullptr;
    int         fSaveCount = 0;
};

class SkCanvasPriv {
public:
    // The lattice has pointers directly into the readbuffer
    [[nodiscard]] static bool ReadLattice(SkReadBuffer&, SkCanvas::Lattice*);

    static void WriteLattice(SkWriteBuffer&, const SkCanvas::Lattice&);

    // return the byte-size of the lattice, even if the buffer is null
    // storage must be 4-byte aligned
    static size_t WriteLattice(void* storage, const SkCanvas::Lattice&);

    static int SaveBehind(SkCanvas* canvas, const SkRect* subset) {
        return (*canvas).only_axis_aligned_saveBehind(subset);
    }
    static void DrawBehind(SkCanvas* canvas, const SkPaint& paint) {
        (*canvas).drawClippedToSaveBehind(paint);
    }

    // Exposed for testing on non-Android framework builds
    static void ResetClip(SkCanvas* canvas) {
        (*canvas).internal_private_resetClip();
    }

    static SkDevice* TopDevice(const SkCanvas* canvas) {
        return (*canvas).topDevice();
    }

    // The experimental_DrawEdgeAAImageSet API accepts separate dstClips and preViewMatrices arrays,
    // where entries refer into them, but no explicit size is provided. Given a set of entries,
    // computes the minimum length for these arrays that would provide index access errors.
    static void GetDstClipAndMatrixCounts(const SkCanvas::ImageSetEntry set[], int count,
                                          int* totalDstClipCount, int* totalMatrixCount);


};

/**
 *  This constant is trying to balance the speed of ref'ing a subpicture into a parent picture,
 *  against the playback cost of recursing into the subpicture to get at its actual ops.
 *
 *  For now we pick a conservatively small value, though measurement (and other heuristics like
 *  the type of ops contained) may justify changing this value.
 */
constexpr int kMaxPictureOpsToUnrollInsteadOfRef = 1;

// Captures paint ownership after the canvas has prepared its destination.
// CPU mask effects stay on the paint for direct device rendering.
struct PreparedDraw {
    explicit PreparedDraw(const SkPaint& source) : paint(source) {}
    SkPaint paint;
};

#endif
