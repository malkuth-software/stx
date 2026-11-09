/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkVertState_DEFINED
#define SkVertState_DEFINED

#include "include/core/SkVertices.h"

#include <cstdint>

/** \struct VertState
    This is a helper for drawVertices(). It is used to iterate over the triangles
    that are to be rendered based on an SkCanvas::VertexMode and (optionally) an
    index array. It does not copy the index array and the client must ensure it
    remains valid for the lifetime of the VertState object.
*/

struct VertState {
    // The current triangle's vertex indices.
    int f0 = 0;
    int f1 = 0;
    int f2 = 0;

    /**
     *  Construct a VertState from a vertex count, index array, and index count.
     *  If the vertices are unindexed pass nullptr for indices.
     */
    VertState(int vCount, const uint16_t indices[], int indexCount)
            : fCount(indices ? indexCount : vCount)
            , fIndices(indices) {}

    using Proc = bool (*)(VertState*);

    /**
     *  Choose an appropriate function to traverse the vertices.
     *  @param mode    Specifies the SkCanvas::VertexMode. A mode outside the enumeration
     *                 yields a function that produces no triangles.
     */
    Proc chooseProc(SkVertices::VertexMode mode);

private:
    int             fCount = 0;
    int             fCurrIndex = 0;
    const uint16_t* fIndices = nullptr;

    static bool Triangles(VertState*);
    static bool TrianglesX(VertState*);
    static bool TriangleStrip(VertState*);
    static bool TriangleStripX(VertState*);
    static bool TriangleFan(VertState*);
    static bool TriangleFanX(VertState*);
    static bool NoTriangles(VertState*);
};

#endif
