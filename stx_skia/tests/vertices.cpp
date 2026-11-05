// Differential corpus for SkVertices and VertState: MakeCopy over every vertex mode, vertex and
// index count, attribute combination and index pattern (including out-of-range indices, which
// MakeCopy clamps); Builder construction with and without indices; triangle-fan conversion and
// its vertex limit; and the triangles VertState yields for each result. Both builds must
// produce identical bytes.

#include "differential_corpus.h"

#include "include/core/SkColor.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkVertices.h"
#include "src/core/SkVertState.h"
#include "src/core/SkVerticesPriv.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using corpus::Output;
using corpus::append;
using corpus::append_bool;

template <typename T>
void append_array(Output& output, const T* values, int count) {
    for (int i = 0; i < count; ++i) {
        append(output, values[i]);
    }
}

// Records a vertices object (or its absence) and every triangle VertState yields from it.
void record(Output& output, const sk_sp<SkVertices>& vertices) {
    append_bool(output, vertices != nullptr);
    if (vertices == nullptr) {
        ++output.cases;
        return;
    }
    const SkVerticesPriv info = (*vertices).priv();
    append(output, static_cast<int>(info.mode()));
    append(output, info.vertexCount());
    append(output, info.indexCount());
    append_bool(output, info.hasTexCoords());
    append_bool(output, info.hasColors());
    append_bool(output, info.hasIndices());
    append(output, (*vertices).bounds());
    append(output, (*vertices).approximateSize());
    append_array(output, info.positions(), info.vertexCount());
    if (info.hasTexCoords()) append_array(output, info.texCoords(), info.vertexCount());
    if (info.hasColors()) append_array(output, info.colors(), info.vertexCount());
    if (info.hasIndices()) append_array(output, info.indices(), info.indexCount());

    VertState state(info.vertexCount(), info.indices(), info.indexCount());
    const VertState::Proc proc = state.chooseProc(info.mode());
    int triangles = 0;
    while (proc(&state)) {
        append(output, state.f0);
        append(output, state.f1);
        append(output, state.f2);
        ++triangles;
    }
    append(output, triangles);
    ++output.cases;
}

std::vector<SkPoint> make_points(int count, float scale) {
    std::vector<SkPoint> points(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float x = static_cast<float>((i * 37) % 101) * scale - 3.5f;
        const float y = static_cast<float>((i * 53) % 89) * scale + 1.25f;
        points[static_cast<size_t>(i)] = SkPoint{x, y};
    }
    return points;
}

std::vector<SkColor> make_colors(int count) {
    std::vector<SkColor> colors(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        colors[static_cast<size_t>(i)] = 0xFF000000u | static_cast<uint32_t>(i) * 2654435761u;
    }
    return colors;
}

void copy_corpus(Output& output) {
    const std::array<SkVertices::VertexMode, 3> modes{SkVertices::kTriangles_VertexMode,
                                                       SkVertices::kTriangleStrip_VertexMode,
                                                       SkVertices::kTriangleFan_VertexMode};
    const std::array<int, 8> vertexCounts{0, 1, 2, 3, 4, 7, 19, 300};
    const std::array<int, 8> indexCounts{0, 1, 2, 3, 5, 9, 16, 31};
    for (const SkVertices::VertexMode mode : modes) {
        for (const int vertexCount : vertexCounts) {
            const std::vector<SkPoint> positions = make_points(vertexCount, 0.75f);
            const std::vector<SkPoint> texs = make_points(vertexCount, 0.125f);
            const std::vector<SkColor> colors = make_colors(vertexCount);
            for (int attributes = 0; attributes < 4; ++attributes) {
                const SkPoint* tex = (attributes & 1) ? texs.data() : nullptr;
                const SkColor* color = (attributes & 2) ? colors.data() : nullptr;
                for (const int indexCount : indexCounts) {
                    // Every third index is out of range; MakeCopy clamps it.
                    std::vector<uint16_t> indices(static_cast<size_t>(indexCount) + 1);
                    for (int i = 0; i < indexCount; ++i) {
                        const int value = (i % 3 == 2) ? vertexCount + i : (i * 5) % (vertexCount + 1);
                        indices[static_cast<size_t>(i)] = static_cast<uint16_t>(value);
                    }
                    record(output, SkVertices::MakeCopy(mode, vertexCount, positions.data(), tex,
                                                        color, indexCount, indices.data()));
                }
            }
        }
    }
}

// Builder construction, as SkPatchUtils and SkDevice::drawAtlas use it, with in-range indices.
void builder_corpus(Output& output) {
    const std::array<SkVertices::VertexMode, 3> modes{SkVertices::kTriangles_VertexMode,
                                                       SkVertices::kTriangleStrip_VertexMode,
                                                       SkVertices::kTriangleFan_VertexMode};
    for (const SkVertices::VertexMode mode : modes) {
        for (const int vertexCount : {3, 6, 25}) {
            for (const int indexCount : {0, 3, 12}) {
                for (const uint32_t flags : {0u, 1u, 2u, 3u}) {
                    SkVertices::Builder builder(mode, vertexCount, indexCount, flags);
                    append_bool(output, builder.isValid());
                    if (!builder.isValid()) {
                        ++output.cases;
                        continue;
                    }
                    SkPoint* positions = builder.positions();
                    for (int i = 0; i < vertexCount; ++i) {
                        positions[i] = SkPoint{static_cast<float>(i), static_cast<float>(i * i % 7)};
                    }
                    if (SkPoint* texs = builder.texCoords()) {
                        for (int i = 0; i < vertexCount; ++i) {
                            texs[i] = SkPoint{static_cast<float>(i) * 0.5f, 1.0f};
                        }
                    }
                    if (SkColor* colors = builder.colors()) {
                        for (int i = 0; i < vertexCount; ++i) {
                            colors[i] = 0xFF102030u + static_cast<uint32_t>(i);
                        }
                    }
                    if (uint16_t* indices = builder.indices()) {
                        for (int i = 0; i < indexCount; ++i) {
                            indices[i] = static_cast<uint16_t>((i * 7) % vertexCount);
                        }
                    }
                    record(output, builder.detach());
                    // A builder detaches once.
                    append_bool(output, builder.detach() == nullptr);
                }
            }
        }
    }
}

// An unindexed fan becomes indexed triangles, so it is limited to 65,536 vertices.
void fan_limit_corpus(Output& output) {
    for (const int vertexCount : {65535, 65536, 65537}) {
        const std::vector<SkPoint> positions = make_points(vertexCount, 0.01f);
        const sk_sp<SkVertices> vertices = SkVertices::MakeCopy(
                SkVertices::kTriangleFan_VertexMode, vertexCount, positions.data(), nullptr,
                nullptr);
        append_bool(output, vertices != nullptr);
        if (vertices != nullptr) {
            const SkVerticesPriv info = (*vertices).priv();
            append(output, info.indexCount());
            const uint16_t* indices = info.indices();
            append(output, indices[info.indexCount() - 1]);
            append(output, (*vertices).approximateSize());
        }
        ++output.cases;
    }
}

#ifndef STX_SKIA_REFERENCE
// Builder users write indices directly; detach() clamps them to the vertex range.
bool builder_indices_are_clamped() {
    SkVertices::Builder triangles(SkVertices::kTriangles_VertexMode, 4, 6, 0);
    const std::array<uint16_t, 6> raw{0, 1, 9, 3, 65535, 2};
    for (size_t i = 0; i < raw.size(); ++i) triangles.indices()[i] = raw[i];
    for (int i = 0; i < 4; ++i) triangles.positions()[i] = SkPoint{static_cast<float>(i), 0};
    const sk_sp<SkVertices> clamped = triangles.detach();
    const std::array<uint16_t, 6> expected{0, 1, 3, 3, 3, 2};
    for (size_t i = 0; i < expected.size(); ++i) {
        if ((*clamped).priv().indices()[i] != expected[i]) return false;
    }
    // An indexed fan's indices are clamped before it becomes indexed triangles.
    SkVertices::Builder fan(SkVertices::kTriangleFan_VertexMode, 3, 4, 0);
    const std::array<uint16_t, 4> fanRaw{0, 7, 1, 2};
    for (size_t i = 0; i < fanRaw.size(); ++i) fan.indices()[i] = fanRaw[i];
    for (int i = 0; i < 3; ++i) fan.positions()[i] = SkPoint{0, static_cast<float>(i)};
    const sk_sp<SkVertices> fanVertices = fan.detach();
    const std::array<uint16_t, 6> fanExpected{0, 2, 1, 0, 1, 2};
    if ((*fanVertices).priv().indexCount() != 6) return false;
    for (size_t i = 0; i < fanExpected.size(); ++i) {
        if ((*fanVertices).priv().indices()[i] != fanExpected[i]) return false;
    }
    return true;
}

// With more than 65,536 vertices every uint16_t index is valid; none is clamped.
bool large_vertex_counts_keep_indices() {
    const int vertexCount = 70000;
    const std::vector<SkPoint> positions = make_points(vertexCount, 0.01f);
    const std::array<uint16_t, 3> indices{65535, 4464, 4463};
    const sk_sp<SkVertices> vertices = SkVertices::MakeCopy(
            SkVertices::kTriangles_VertexMode, vertexCount, positions.data(), nullptr, nullptr, 3,
            indices.data());
    if (vertices == nullptr) return false;
    for (size_t i = 0; i < indices.size(); ++i) {
        if ((*vertices).priv().indices()[i] != indices[i]) return false;
    }
    return true;
}

// Modes outside the enumeration and negative counts describe nothing. The enumeration has no
// fixed underlying type, so its values are 0 to 3; 3 is the one value it can hold that names no
// mode (forming a larger one is undefined).
bool invalid_descriptions_are_refused() {
    const SkVertices::VertexMode badMode = static_cast<SkVertices::VertexMode>(3);
    const std::array<SkPoint, 3> points{SkPoint{0, 0}, SkPoint{1, 0}, SkPoint{0, 1}};
    if (SkVertices::Builder(badMode, 3, 0, 0).isValid()) return false;
    if (SkVertices::MakeCopy(badMode, 3, points.data(), nullptr, nullptr) != nullptr) return false;
    if (SkVertices::Builder(SkVertices::kTriangles_VertexMode, -3, 0, 0).isValid()) return false;
    if (SkVertices::Builder(SkVertices::kTriangleFan_VertexMode, 3, INT32_MIN, 0).isValid()) {
        return false;
    }
    if (SkVertices::Builder(SkVertices::kTriangleFan_VertexMode, INT32_MIN, 0, 0).isValid()) {
        return false;
    }
    VertState state(3, nullptr, 0);
    const VertState::Proc proc = state.chooseProc(badMode);
    const bool noTriangles = proc != nullptr && !proc(&state);
    return noTriangles;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
    Output output{};
#ifndef STX_SKIA_REFERENCE
    if (!builder_indices_are_clamped()) { std::puts("vertices: builder clamping failed"); return 1; }
    if (!large_vertex_counts_keep_indices()) { std::puts("vertices: large counts failed"); return 1; }
    if (!invalid_descriptions_are_refused()) { std::puts("vertices: refusals failed"); return 1; }
#endif
    copy_corpus(output);
    builder_corpus(output);
    fan_limit_corpus(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("vertices: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
