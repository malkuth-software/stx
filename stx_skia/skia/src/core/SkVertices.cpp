/*
 * Copyright 2017 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "include/core/SkVertices.h"

#include "include/core/SkSpan.h"
#include "include/core/SkTypes.h"
#include "include/private/SkDebug.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkTo.h"
#include "src/core/SkSafeMath.h"
#include "src/core/SkVerticesPriv.h"
#include "src/core/SkVx.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <new>
#include <utility>

static uint32_t next_id() {
    static std::atomic<uint32_t> nextID{1};

    uint32_t id = SK_InvalidGenID;
    do {
        id = nextID.fetch_add(1, std::memory_order_relaxed);
    } while (id == SK_InvalidGenID);
    return id;
}

struct SkVertices::Desc {
    VertexMode  fMode = kTriangles_VertexMode;
    int         fVertexCount = 0;
    int         fIndexCount = 0;
    bool        fHasTexs = false;
    bool        fHasColors = false;
};

// The byte sizes of one vertices allocation: the object, then positions, texture coordinates,
// colors and indices, in that order. An invalid description has all sizes zero.
struct SkVertices::Sizes {
    explicit Sizes(const Desc& desc) {
        // Negative counts and modes outside the enumeration describe nothing drawable; refusing
        // them here also keeps the fan arithmetic below from overflowing.
        const bool validMode = desc.fMode == kTriangles_VertexMode ||
                               desc.fMode == kTriangleStrip_VertexMode ||
                               desc.fMode == kTriangleFan_VertexMode;
        if (!validMode || desc.fVertexCount < 0 || desc.fIndexCount < 0) {
            invalidate();
            return;
        }
        SkSafeMath safe;

        fVSize = safe.mul(desc.fVertexCount, sizeof(SkPoint));
        fTSize = desc.fHasTexs ? safe.mul(desc.fVertexCount, sizeof(SkPoint)) : 0;
        fCSize = desc.fHasColors ? safe.mul(desc.fVertexCount, sizeof(SkColor)) : 0;

        fBuilderTriFanISize = 0;
        fISize = safe.mul(desc.fIndexCount, sizeof(uint16_t));
        if (kTriangleFan_VertexMode == desc.fMode) {
            int numFanTris = 0;
            if (desc.fIndexCount) {
                fBuilderTriFanISize = fISize;
                numFanTris = desc.fIndexCount - 2;
            } else {
                numFanTris = desc.fVertexCount - 2;
                // By forcing this to become indexed we are adding a constraint to the maximum
                // number of vertices.
                if (desc.fVertexCount > (SkTo<int>(UINT16_MAX) + 1)) {
                    invalidate();
                    return;
                }
            }
            if (numFanTris <= 0) {
                invalidate();
                return;
            }
            fISize = safe.mul(numFanTris, 3 * sizeof(uint16_t));
        }

        const size_t arrays = safe.add(fVSize, safe.add(fTSize, safe.add(fCSize, fISize)));
        fTotal = safe.add(sizeof(SkVertices), arrays);

        if (safe.ok()) {
            fArrays = fVSize + fTSize + fCSize + fISize;  // just the sum of the arrays
        } else {
            invalidate();
        }
    }

    bool isValid() const {
        SkASSERT(fTotal >= fVSize);
        const bool valid = fVSize > 0;
        return valid;
    }

    void invalidate() {
        fTotal = 0;
        fArrays = 0;
        fVSize = 0;
        fTSize = 0;
        fCSize = 0;
        fISize = 0;
        fBuilderTriFanISize = 0;
    }

    size_t fTotal = 0;   // size of entire SkVertices allocation (obj + arrays)
    size_t fArrays = 0;  // size of all the data arrays (V + T + C + I)
    size_t fVSize = 0;
    size_t fTSize = 0;
    size_t fCSize = 0;
    size_t fISize = 0;

    // For indexed tri-fans this is the number of amount of space fo indices needed in the builder
    // before conversion to indexed triangles (or zero if not indexed or not a triangle fan).
    size_t fBuilderTriFanISize = 0;
};

SkVertices::Builder::Builder(VertexMode mode, int vertexCount, int indexCount,
                             uint32_t builderFlags) {
    const bool hasTexs = (builderFlags & SkVertices::kHasTexCoords_BuilderFlag) != 0;
    const bool hasColors = (builderFlags & SkVertices::kHasColors_BuilderFlag) != 0;
    const Desc desc{mode, vertexCount, indexCount, hasTexs, hasColors};
    init(desc);
}

SkVertices::Builder::Builder(const Desc& desc) {
    init(desc);
}

// Clamps count indices from source into destination, which may be source itself, to maxIndex.
static void clamp_indices(uint16_t* destination, const uint16_t* source, size_t count,
                          uint16_t maxIndex) {
    size_t i = 0;
    for (; i + 8 <= count; i += 8) {
        const skvx::ushort8 ind8 = skvx::ushort8::Load(source + i);
        const skvx::ushort8 clamped_ind8 = skvx::min(ind8, maxIndex);
        clamped_ind8.store(destination + i);
    }
    if (i + 4 <= count) {
        const skvx::ushort4 ind4 = skvx::ushort4::Load(source + i);
        const skvx::ushort4 clamped_ind4 = skvx::min(ind4, maxIndex);
        clamped_ind4.store(destination + i);

        i += 4;
    }
    if (i + 2 <= count) {
        const skvx::ushort2 ind2 = skvx::ushort2::Load(source + i);
        const skvx::ushort2 clamped_ind2 = skvx::min(ind2, maxIndex);
        clamped_ind2.store(destination + i);

        i += 2;
    }
    if (i < count) {
        destination[i] = std::min(source[i], maxIndex);
        i += 1;
    }
    SkASSERT(i == count);
}

// The largest index valid for vertexCount (at least 1) vertices; uint16_t indices cannot name
// more than 65,536.
static uint16_t max_index_for(int vertexCount) {
    const int largest = std::min(vertexCount - 1, static_cast<int>(UINT16_MAX));
    const uint16_t maxIndex = static_cast<uint16_t>(largest);
    return maxIndex;
}

// Returns the array at *cursor, or null for an empty array, and advances the cursor past it.
static char* take_array(char** cursor, size_t bytes) {
    char* array = bytes ? *cursor : nullptr;
    *cursor += bytes;
    return array;
}

void SkVertices::Builder::init(const Desc& desc) {
    const Sizes sizes(desc);
    if (!sizes.isValid()) {
        SkASSERT(!isValid());
        return;
    }

    void* storage = ::operator new (sizes.fTotal);
    if (sizes.fBuilderTriFanISize) {
        fIntermediateFanIndices.reset(new uint8_t[sizes.fBuilderTriFanISize]);
    }

    fVertices.reset(new (storage) SkVertices);
    SkVertices& vertices = *fVertices;

    // The arrays follow the object in the same allocation.
    char* cursor = static_cast<char*>(storage) + sizeof(SkVertices);
    vertices.fPositions = reinterpret_cast<SkPoint*>(take_array(&cursor, sizes.fVSize));
    vertices.fTexs      = reinterpret_cast<SkPoint*>(take_array(&cursor, sizes.fTSize));
    vertices.fColors    = reinterpret_cast<SkColor*>(take_array(&cursor, sizes.fCSize));
    vertices.fIndices   = reinterpret_cast<uint16_t*>(take_array(&cursor, sizes.fISize));

    vertices.fVertexCount = desc.fVertexCount;
    vertices.fIndexCount  = desc.fIndexCount;
    vertices.fMode        = desc.fMode;

    // We defer assigning fBounds and fUniqueID until detach() is called
}

sk_sp<SkVertices> SkVertices::Builder::detach() {
    if (!fVertices) {
        return nullptr;
    }
    SkVertices& vertices = *fVertices;
    const SkSpan<const SkPoint> positions(vertices.fPositions,
                                          static_cast<size_t>(vertices.fVertexCount));
    vertices.fBounds = SkRect::BoundsOrEmpty(positions);
    // Builder users write indices directly; draws index the vertex arrays with them, so every
    // index is clamped to the vertex range before the vertices are published.
    const uint16_t maxIndex = max_index_for(vertices.fVertexCount);
    if (vertices.fMode != kTriangleFan_VertexMode && vertices.fIndexCount > 0) {
        clamp_indices(vertices.fIndices, vertices.fIndices,
                      static_cast<size_t>(vertices.fIndexCount), maxIndex);
    }
    if (vertices.fMode == kTriangleFan_VertexMode) {
        if (fIntermediateFanIndices) {
            SkASSERT(vertices.fIndexCount);
            uint16_t* tempIndices = indices();
            clamp_indices(tempIndices, tempIndices, static_cast<size_t>(vertices.fIndexCount),
                          maxIndex);
            for (int t = 0; t < vertices.fIndexCount - 2; ++t) {
                vertices.fIndices[3 * t + 0] = tempIndices[0];
                vertices.fIndices[3 * t + 1] = tempIndices[t + 1];
                vertices.fIndices[3 * t + 2] = tempIndices[t + 2];
            }
            vertices.fIndexCount = 3 * (vertices.fIndexCount - 2);
        } else {
            SkASSERT(!vertices.fIndexCount);
            for (int t = 0; t < vertices.fVertexCount - 2; ++t) {
                vertices.fIndices[3 * t + 0] = 0;
                vertices.fIndices[3 * t + 1] = SkToU16(t + 1);
                vertices.fIndices[3 * t + 2] = SkToU16(t + 2);
            }
            vertices.fIndexCount = 3 * (vertices.fVertexCount - 2);
        }
        vertices.fMode = kTriangles_VertexMode;
    }
    vertices.fUniqueID = next_id();
    // Moving out leaves the builder empty; later calls return null.
    sk_sp<SkVertices> built = std::move(fVertices);
    return built;
}

SkPoint* SkVertices::Builder::positions() {
    SkPoint* positions = fVertices ? (*fVertices).fPositions : nullptr;
    return positions;
}

SkPoint* SkVertices::Builder::texCoords() {
    SkPoint* texs = fVertices ? (*fVertices).fTexs : nullptr;
    return texs;
}

SkColor* SkVertices::Builder::colors() {
    SkColor* colors = fVertices ? (*fVertices).fColors : nullptr;
    return colors;
}

uint16_t* SkVertices::Builder::indices() {
    if (!fVertices) {
        return nullptr;
    }
    if (fIntermediateFanIndices) {
        uint16_t* fanIndices = reinterpret_cast<uint16_t*>(fIntermediateFanIndices.get());
        return fanIndices;
    }
    return (*fVertices).fIndices;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

sk_sp<SkVertices> SkVertices::MakeCopy(VertexMode mode, int vertexCount,
                                       const SkPoint pos[], const SkPoint texs[],
                                       const SkColor colors[],
                                       int indexCount, const uint16_t indices[]) {
    const Desc desc{mode, vertexCount, indexCount, texs != nullptr, colors != nullptr};
    Builder builder(desc);
    if (!builder.isValid()) {
        return nullptr;
    }

    const Sizes sizes(desc);
    SkASSERT(sizes.isValid());
    sk_careful_memcpy(builder.positions(), pos, sizes.fVSize);
    sk_careful_memcpy(builder.texCoords(), texs, sizes.fTSize);
    sk_careful_memcpy(builder.colors(), colors, sizes.fCSize);

    // The builder can update the number of indices.
    const size_t isize = (mode == kTriangleFan_VertexMode) ? sizes.fBuilderTriFanISize
                                                           : sizes.fISize;
    const size_t icount = isize / sizeof(uint16_t);

    // detach() clamps the copied indices to the vertex range.
    SkASSERT(vertexCount > 0);
    sk_careful_memcpy(builder.indices(), indices, icount * sizeof(uint16_t));

    return builder.detach();
}

size_t SkVertices::approximateSize() const {
    const size_t bytes = getSizes().fTotal;
    return bytes;
}

SkVertices::Sizes SkVertices::getSizes() const {
    const Desc desc{fMode, fVertexCount, fIndexCount, fTexs != nullptr, fColors != nullptr};
    const Sizes sizes(desc);
    SkASSERT(sizes.isValid());
    return sizes;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void SkVertices::operator delete(void* p) {
    ::operator delete(p);
}
