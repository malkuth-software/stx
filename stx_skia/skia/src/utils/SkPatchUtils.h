/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkPatchUtils_DEFINED
#define SkPatchUtils_DEFINED

#include "include/core/SkColor.h"
#include "include/core/SkRefCnt.h"

class SkColorSpace;
class SkMatrix;
class SkVertices;
struct SkISize;
struct SkPoint;

class SkPatchUtils {

public:
    // Enums for control points based on the order specified in the constructor (clockwise).
    enum {
        kNumCtrlPts = 12,
        kNumCorners = 4,
        kNumPtsCubic = 4
    };

    /**
     * Get the points corresponding to the top cubic of cubics.
     */
    static void GetTopCubic(const SkPoint cubics[12], SkPoint points[4]);

    /**
     * Get the points corresponding to the bottom cubic of cubics.
     */
    static void GetBottomCubic(const SkPoint cubics[12], SkPoint points[4]);

    /**
     * Get the points corresponding to the left cubic of cubics.
     */
    static void GetLeftCubic(const SkPoint cubics[12], SkPoint points[4]);

    /**
     * Get the points corresponding to the right cubic of cubics.
     */
    static void GetRightCubic(const SkPoint cubics[12], SkPoint points[4]);

    /**
     * Calculates a level of detail (number of subdivisions) for a patch in both axes, from its
     * edges' lengths under matrix: at least 8, saturating at the largest int a float holds. A
     * patch with a non-finite edge length gets {0, 0}, which MakeVertices refuses.
     */
    static SkISize GetLevelOfDetail(const SkPoint cubics[12], const SkMatrix& matrix);

    /**
     * Tessellates the patch at lodX by lodY steps, both at least 1, or returns null. Levels whose
     * vertex count would pass 10000, or either of which passes 200, are reduced in proportion.
     */
    static sk_sp<SkVertices> MakeVertices(const SkPoint cubics[12], const SkColor colors[4],
                                          const SkPoint texCoords[4], int lodX, int lodY,
                                          SkColorSpace* colorSpace = nullptr);
};

#endif
