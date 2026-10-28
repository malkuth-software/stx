// Differential corpus for the patch utilities behind SkCanvas::drawPatch: levels of detail for
// random Coons patches under several matrices, including non-finite and degenerate patches,
// and the full vertices built for computed and explicit levels, with and without colours and
// texture coordinates, in three colour spaces. Both builds must produce identical bytes
// (patch-utils.raw). Levels whose vertex count passes INT_MAX go to patch-utils-large.raw,
// which is meant to differ: the reference refuses them and the candidate reduces them like
// any other large level. Patches large enough to overflow the level of detail itself are
// checked by the candidate alone; the reference's arithmetic is undefined there.

#include "differential_corpus.h"

#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSize.h"
#include "include/core/SkVertices.h"
#include "src/core/SkVerticesPriv.h"
#include "src/utils/SkPatchUtils.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <climits>
#include <cstdio>
#include <utility>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

using Cubics = std::array<SkPoint, 12>;

// The reference takes the matrix by pointer; the candidate by reference.
SkISize level_of_detail(const Cubics& cubics, const SkMatrix& matrix) {
#ifdef STX_SKIA_REFERENCE
    const SkISize lod = SkPatchUtils::GetLevelOfDetail(cubics.data(), &matrix);
#else
    const SkISize lod = SkPatchUtils::GetLevelOfDetail(cubics.data(), matrix);
#endif
    return lod;
}

template <typename T>
void append_array(Output& output, const T* values, int count) {
    for (int i = 0; i < count; ++i) append(output, values[i]);
}

void record_vertices(Output& output, const sk_sp<SkVertices>& vertices) {
    append_bool(output, vertices != nullptr);
    if (vertices == nullptr) return;
    const SkVerticesPriv info = (*vertices).priv();
    append(output, static_cast<int>(info.mode()));
    append(output, info.vertexCount());
    append(output, info.indexCount());
    append_bool(output, info.hasTexCoords());
    append_bool(output, info.hasColors());
    append_array(output, info.positions(), info.vertexCount());
    if (info.hasTexCoords()) append_array(output, info.texCoords(), info.vertexCount());
    if (info.hasColors()) append_array(output, info.colors(), info.vertexCount());
    if (info.hasIndices()) append_array(output, info.indices(), info.indexCount());
}

float coordinate(Sequence& sequence, float span) {
    const int quarters = static_cast<int>(sequence.next() % 8001u) - 4000;
    const float unit = static_cast<float>(quarters) / 4000.0f;
    return unit * span;
}

Cubics random_cubics(Sequence& sequence, float span) {
    Cubics cubics{};
    for (SkPoint& point : cubics) {
        const float x = coordinate(sequence, span);
        const float y = coordinate(sequence, span);
        point = SkPoint{x, y};
    }
    return cubics;
}

std::array<SkMatrix, 7> matrices() {
    SkMatrix skew = SkMatrix::I();
    skew.setSkew(0.25f, -0.5f);
    SkMatrix perspective = SkMatrix::I();
    perspective.setPerspX(0.001f);
    perspective.setPerspY(-0.0005f);
    const std::array<SkMatrix, 7> result{SkMatrix::I(), SkMatrix::Scale(2.0f, 0.5f),
                                         SkMatrix::RotateDeg(30.0f), SkMatrix::Translate(7, -3),
                                         skew, perspective, SkMatrix::Scale(0.01f, 0.01f)};
    return result;
}

std::array<sk_sp<SkColorSpace>, 3> color_spaces() {
    const std::array<sk_sp<SkColorSpace>, 3> result{
            nullptr, SkColorSpace::MakeSRGB(),
            SkColorSpace::MakeRGB(SkNamedTransferFn::kLinear, SkNamedGamut::kSRGB)};
    return result;
}

void record_patch(Output& output, Sequence& sequence, const Cubics& cubics, const SkMatrix& matrix,
                  const sk_sp<SkColorSpace>& space) {
    const SkISize lod = level_of_detail(cubics, matrix);
    append(output, lod.width());
    append(output, lod.height());
    const std::array<SkColor, 4> colors{static_cast<SkColor>(sequence.next()),
                                        static_cast<SkColor>(sequence.next()),
                                        static_cast<SkColor>(sequence.next()),
                                        static_cast<SkColor>(sequence.next())};
    const std::array<SkPoint, 4> texs{SkPoint{0, 0}, SkPoint{17, 0}, SkPoint{17, 13},
                                      SkPoint{0, 13}};
    const bool withColors = sequence.pick(2) == 0;
    const bool withTexs = sequence.pick(2) == 0;
    const sk_sp<SkVertices> vertices = SkPatchUtils::MakeVertices(
            cubics.data(), withColors ? colors.data() : nullptr, withTexs ? texs.data() : nullptr,
            lod.width(), lod.height(), space.get());
    record_vertices(output, vertices);
    ++output.cases;
}

void shared_corpus(Output& output) {
    Sequence sequence{};
    const std::array<SkMatrix, 7> transforms = matrices();
    const std::array<sk_sp<SkColorSpace>, 3> spaces = color_spaces();
    const std::array<float, 4> spans{50.0f, 500.0f, 20000.0f, 0.5f};
    for (const float span : spans) {
        for (int i = 0; i < 60; ++i) {
            const Cubics cubics = random_cubics(sequence, span);
            const SkMatrix& matrix = transforms[static_cast<size_t>(i) % transforms.size()];
            const sk_sp<SkColorSpace>& space = spaces[static_cast<size_t>(i) % spaces.size()];
            record_patch(output, sequence, cubics, matrix, space);
        }
    }
    // Degenerate and non-finite patches.
    Cubics point{};
    for (SkPoint& p : point) p = SkPoint{3, 4};
    record_patch(output, sequence, point, SkMatrix::I(), spaces[0]);
    const std::array<float, 3> bad{corpus::kNaN, corpus::kInf, -corpus::kInf};
    for (const float value : bad) {
        for (size_t at = 0; at < 12; at += 5) {
            Cubics cubics = random_cubics(sequence, 50.0f);
            cubics[at].fX = value;
            record_patch(output, sequence, cubics, SkMatrix::I(), spaces[1]);
        }
    }
    // Explicit levels, including the reduction for more than 10000 vertices or 200 steps, and
    // the refusals for levels below one.
    const std::array<std::pair<int, int>, 12> levels{
            std::pair{1, 1},     std::pair{2, 7},     std::pair{8, 8},       std::pair{63, 100},
            std::pair{199, 1},   std::pair{200, 200}, std::pair{201, 2},     std::pair{300, 10},
            std::pair{2000, 3},  std::pair{0, 5},     std::pair{5, -1},      std::pair{100, 101}};
    for (const std::pair<int, int> level : levels) {
        for (int flags = 0; flags < 4; ++flags) {
            const Cubics cubics = random_cubics(sequence, 100.0f);
            const std::array<SkColor, 4> colors{SK_ColorRED, SK_ColorGREEN, SK_ColorBLUE,
                                                SkColorSetARGB(128, 255, 255, 0)};
            const std::array<SkPoint, 4> texs{SkPoint{0, 0}, SkPoint{1, 0}, SkPoint{1, 1},
                                              SkPoint{0, 1}};
            const sk_sp<SkColorSpace>& space = spaces[static_cast<size_t>(flags) % spaces.size()];
            const sk_sp<SkVertices> vertices = SkPatchUtils::MakeVertices(
                    cubics.data(), (flags & 1) != 0 ? colors.data() : nullptr,
                    (flags & 2) != 0 ? texs.data() : nullptr, level.first, level.second,
                    space.get());
            record_vertices(output, vertices);
            ++output.cases;
        }
    }
}

// Levels from patches a few million units across, whose vertex counts pass INT_MAX, and explicit
// levels up to 2^30, where the reference's arithmetic is still defined.
void large_corpus(Output& output) {
    Sequence sequence{};
    const std::array<float, 3> spans{3.0e6f, 1.0e7f, 8.0e7f};
    for (const float span : spans) {
        for (int i = 0; i < 6; ++i) {
            const Cubics cubics = random_cubics(sequence, span);
            const SkISize lod = level_of_detail(cubics, SkMatrix::I());
            append(output, lod.width());
            append(output, lod.height());
            const sk_sp<SkVertices> vertices = SkPatchUtils::MakeVertices(
                    cubics.data(), nullptr, nullptr, lod.width(), lod.height(), nullptr);
            record_vertices(output, vertices);
            ++output.cases;
        }
    }
    const std::array<std::pair<int, int>, 4> levels{std::pair{50000, 50000}, std::pair{1 << 30, 3},
                                                    std::pair{7, 1 << 30},
                                                    std::pair{1 << 30, 1 << 30}};
    for (const std::pair<int, int> level : levels) {
        const Cubics cubics = random_cubics(sequence, 100.0f);
        const sk_sp<SkVertices> vertices = SkPatchUtils::MakeVertices(
                cubics.data(), nullptr, nullptr, level.first, level.second, nullptr);
        record_vertices(output, vertices);
        ++output.cases;
    }
}

#ifndef STX_SKIA_REFERENCE
int vertex_count(const sk_sp<SkVertices>& vertices) {
    return vertices == nullptr ? 0 : (*vertices).priv().vertexCount();
}

// Huge finite patches saturate their level of detail, and every level up to INT_MAX tessellates
// into at most 200 steps in total.
bool large_levels_tessellate() {
    Cubics huge{};
    for (size_t i = 0; i < huge.size(); ++i) {
        huge[i] = SkPoint{static_cast<float>(i % 4) * 1.0e20f, static_cast<float>(i / 4) * 1.0e20f};
    }
    const SkISize lod = level_of_detail(huge, SkMatrix::I());
    if (lod.width() < 8 || lod.height() < 8) return false;
    const sk_sp<SkVertices> fromHuge = SkPatchUtils::MakeVertices(
            huge.data(), nullptr, nullptr, lod.width(), lod.height(), nullptr);
    if (vertex_count(fromHuge) == 0 || vertex_count(fromHuge) > 201 * 201) return false;
    const std::array<std::pair<int, int>, 4> levels{std::pair{INT_MAX, INT_MAX},
                                                    std::pair{INT_MAX, 1}, std::pair{1, INT_MAX},
                                                    std::pair{INT_MAX - 1, 300}};
    Sequence sequence{};
    for (const std::pair<int, int> level : levels) {
        const Cubics cubics = random_cubics(sequence, 100.0f);
        const sk_sp<SkVertices> vertices = SkPatchUtils::MakeVertices(
                cubics.data(), nullptr, nullptr, level.first, level.second, nullptr);
        const int count = vertex_count(vertices);
        if (count == 0 || count > 201 * 201) return false;
    }
    // A non-finite edge still gives {0, 0}, which tessellation refuses.
    Cubics bad = huge;
    bad[5].fY = corpus::kInf;
    const SkISize refused = level_of_detail(bad, SkMatrix::I());
    if (refused.width() != 0 || refused.height() != 0) return false;
    return SkPatchUtils::MakeVertices(bad.data(), nullptr, nullptr, 0, 0, nullptr) == nullptr;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 1 || argc > 3) return 2;
#ifndef STX_SKIA_REFERENCE
    if (!large_levels_tessellate()) {
        std::puts("patch utils: large levels did not tessellate");
        return 1;
    }
#endif
    Output output{};
    Output large{};
    shared_corpus(output);
    large_corpus(large);
    if (argc >= 2 && !corpus::write_file(argv[1], output)) return 2;
    if (argc == 3 && !corpus::write_file(argv[2], large)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    const unsigned long long largeHash = corpus::fnv1a(large.bytes);
    std::printf("patch utils: %zu cases, %zu bytes, hash=%llu; large %zu cases, hash=%llu PASS\n",
                output.cases, output.bytes.size(), hash, large.cases, largeHash);
    return 0;
}
