// Differential corpus for 3x3 matrices: classification, construction, concatenation,
// inversion, point/vector/rectangle mapping, polygon and rectangle fitting, scale
// decomposition, serialization and the sprite test. The shared corpus avoids the
// out-of-range float-to-int conversion the original sprite test performed.

#include "differential_corpus.h"

#include "include/core/SkM44.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPoint3.h"
#include "include/core/SkRSXform.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSize.h"
#include "src/core/SkMatrixPriv.h"
#include "src/core/SkMatrixUtils.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

const std::array<float, 24> kSpecials{
    0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 2.0f, -3.0f, 1e-7f, 1e-30f, corpus::kDenormal,
    corpus::kFloatMin, 1e20f, -1e20f, 3e38f, corpus::kFloatMax, -corpus::kFloatMax,
    corpus::kInf, -corpus::kInf, corpus::kNaN, 16777217.0f, 2147483648.0f, -2147483904.0f,
    0.001f, 1e-4f,
};

float pick_special(Sequence& sequence) {
    const float value = kSpecials[sequence.pick(kSpecials.size())];
    return value;
}

// Mostly ordinary values in [-64, 64] at eighth steps, occasionally a special value.
float pick_ordinary(Sequence& sequence) {
    if (sequence.next() % 10 == 0) {
        const float special = pick_special(sequence);
        return special;
    }
    const int32_t whole = static_cast<int32_t>(sequence.next() % 129) - 64;
    const float fraction = static_cast<float>(sequence.next() % 8) * 0.125f;
    const float value = static_cast<float>(whole) + fraction;
    return value;
}

float pick_small(Sequence& sequence) {
    const int32_t steps = static_cast<int32_t>(sequence.next() % 33) - 16;
    const float value = static_cast<float>(steps) * 0.0009765625f;
    return value;
}

void append_matrix(Output& output, const SkMatrix& matrix) {
    for (int i = 0; i < 9; ++i) {
        append(output, matrix[i]);
    }
    append(output, static_cast<int32_t>(matrix.getType()));
    append_bool(output, matrix.rectStaysRect());
}

void append_point(Output& output, const SkPoint& point) {
    append(output, point.fX);
    append(output, point.fY);
}

void append_rect(Output& output, const SkRect& rect) {
    append(output, rect.fLeft);
    append(output, rect.fTop);
    append(output, rect.fRight);
    append(output, rect.fBottom);
}

std::vector<SkMatrix> named_matrices() {
    std::vector<SkMatrix> matrices{};
    matrices.push_back(SkMatrix::I());
    matrices.push_back(SkMatrix::Translate(3.5f, -2.25f));
    matrices.push_back(SkMatrix::Translate(SkIVector::Make(7, -9)));
    matrices.push_back(SkMatrix::Scale(2, 3));
    matrices.push_back(SkMatrix::Scale(-1, 1));
    matrices.push_back(SkMatrix::Scale(0, 2));
    matrices.push_back(SkMatrix::Scale(1, 1));
    matrices.push_back(SkMatrix::Scale(1e-20f, 1e-20f));
    matrices.push_back(SkMatrix::Scale(1e30f, -1e30f));
    matrices.push_back(SkMatrix::ScaleTranslate(-0.5f, 4, 10, -0.0f));
    matrices.push_back(SkMatrix::RotateDeg(30));
    matrices.push_back(SkMatrix::RotateDeg(90));
    matrices.push_back(SkMatrix::RotateDeg(180));
    matrices.push_back(SkMatrix::RotateDeg(270));
    matrices.push_back(SkMatrix::RotateDeg(45, SkPoint::Make(10, -5)));
    matrices.push_back(SkMatrix::RotateRad(1.25f));
    matrices.push_back(SkMatrix::Skew(0.25f, -0.5f));
    matrices.push_back(SkMatrix::MakeAll(1, 0, 0, 0, 1, 0, 0.001f, 0, 1));
    matrices.push_back(SkMatrix::MakeAll(2, 0.5f, 3, -0.25f, 1.5f, -7, 0, 0.002f, 1));
    matrices.push_back(SkMatrix::MakeAll(4, 0, 8, 0, 6, -2, 0, 0, 2));
    matrices.push_back(SkMatrix::MakeAll(0, 1, 0, 1, 0, 0, 0, 0, 1));
    matrices.push_back(SkMatrix::MakeAll(0, -2, 5, 3, 0, 1, 0, 0, 1));
    matrices.push_back(SkMatrix::MakeAll(1, 2, 3, 2, 4, 6, 0, 0, 1));
    matrices.push_back(SkMatrix::MakeAll(corpus::kNaN, 0, 0, 0, 1, 0, 0, 0, 1));
    matrices.push_back(SkMatrix::MakeAll(1, 0, corpus::kInf, 0, 1, 0, 0, 0, 1));
    matrices.push_back(SkMatrix::MakeAll(1, 0, 0, 0, 1, 0, 0, 0, 0));
    SkMatrix sinCos{};
    sinCos.setSinCos(0.6f, 0.8f, 2, 3);
    matrices.push_back(sinCos);
    SkMatrix rsx{};
    rsx.setRSXform(SkRSXform::Make(0.8f, -0.6f, 11, 13));
    matrices.push_back(rsx);
    SkMatrix pivoted{};
    pivoted.setScale(2, -3, 4, 5);
    matrices.push_back(pivoted);
    SkMatrix skewPivot{};
    skewPivot.setSkew(0.5f, 0.125f, -3, 7);
    matrices.push_back(skewPivot);
    return matrices;
}

SkMatrix random_matrix(Sequence& sequence) {
    const uint32_t shape = sequence.next() % 6;
    SkMatrix matrix{};
    if (shape == 0) {
        matrix = SkMatrix::Translate(pick_ordinary(sequence), pick_ordinary(sequence));
    } else if (shape == 1) {
        matrix = SkMatrix::ScaleTranslate(pick_ordinary(sequence), pick_ordinary(sequence),
                                          pick_ordinary(sequence), pick_ordinary(sequence));
    } else if (shape == 2 || shape == 3) {
        matrix.setAll(pick_ordinary(sequence), pick_ordinary(sequence), pick_ordinary(sequence),
                      pick_ordinary(sequence), pick_ordinary(sequence), pick_ordinary(sequence),
                      0, 0, 1);
    } else if (shape == 4) {
        matrix.setAll(pick_ordinary(sequence), pick_ordinary(sequence), pick_ordinary(sequence),
                      pick_ordinary(sequence), pick_ordinary(sequence), pick_ordinary(sequence),
                      pick_small(sequence), pick_small(sequence), 1);
    } else {
        matrix.setAll(pick_special(sequence), pick_ordinary(sequence), pick_special(sequence),
                      pick_ordinary(sequence), pick_special(sequence), pick_ordinary(sequence),
                      pick_small(sequence), pick_small(sequence), pick_ordinary(sequence));
    }
    return matrix;
}

// The original sprite test converted translations to int; keep the shared corpus to values
// that conversion defined.
bool sprite_translation_defined(const SkMatrix& matrix) {
    const float limit = 2147483648.0f;
    const float tx = matrix.getTranslateX();
    const float ty = matrix.getTranslateY();
    const bool defined = tx >= -limit && tx < limit && ty >= -limit && ty < limit;
    return defined;
}

void classify(Output& output, const SkMatrix& source) {
    // Classification fills the cached type; work on a copy so later cases see the same state.
    const SkMatrix m = source;
    append_matrix(output, m);
    append_bool(output, m.isIdentity());
    append_bool(output, m.isScaleTranslate());
    append_bool(output, m.isTranslate());
    append_bool(output, m.preservesAxisAlignment());
    append_bool(output, m.hasPerspective());
    append_bool(output, m.isFinite());
    append_bool(output, m.isSimilarity());
    append_bool(output, m.isSimilarity(0.25f));
    append_bool(output, m.preservesRightAngles());
    append(output, m.getMinScale());
    append(output, m.getMaxScale());
    SkScalar scales[2] = {7, 7};
    append_bool(output, m.getMinMaxScales(scales));
    append(output, scales[0]);
    append(output, scales[1]);
    SkSize scale = SkSize::Make(0, 0);
    SkMatrix remaining = SkMatrix::I();
    append_bool(output, m.decomposeScale(&scale, &remaining));
    append(output, scale.width());
    append(output, scale.height());
    append_matrix(output, remaining);
    SkScalar affine[6] = {9, 9, 9, 9, 9, 9};
    append_bool(output, m.asAffine(affine));
    for (const SkScalar value : affine) {
        append(output, value);
    }
    append(output, SkMatrixPriv::ComputeResScaleForStroking(m));
    const std::optional<SkMatrix> inverse = m.invert();
    append_bool(output, inverse.has_value());
    if (inverse.has_value()) {
        append_matrix(output, inverse.value());
    }
    SkMatrix inverseOut = SkMatrix::Scale(5, 5);
    append_bool(output, m.invert(&inverseOut));
    append_matrix(output, inverseOut);
    SkMatrix normalized = m;
    normalized.normalizePerspective();
    append_matrix(output, normalized);
}

void map(Output& output, const SkMatrix& m, Sequence& sequence) {
    std::array<SkPoint, 9> points{};
    for (SkPoint& point : points) {
        point = SkPoint::Make(pick_ordinary(sequence), pick_ordinary(sequence));
    }
    for (size_t count = 0; count <= points.size(); ++count) {
        std::array<SkPoint, 9> mapped{};
        for (SkPoint& point : mapped) {
            point = SkPoint::Make(-1, -1);
        }
        const SkSpan<const SkPoint> source(points.data(), count);
        m.mapPoints(SkSpan<SkPoint>(mapped.data(), count), source);
        std::array<SkPoint, 9> inPlace = points;
        m.mapPoints(SkSpan<SkPoint>(inPlace.data(), count));
        std::array<SkPoint, 9> vectors = points;
        m.mapVectors(SkSpan<SkVector>(vectors.data(), count));
        for (size_t i = 0; i < points.size(); ++i) {
            append_point(output, mapped[i]);
            append_point(output, inPlace[i]);
            append_point(output, vectors[i]);
        }
    }
    // A destination shorter than the source maps only the shorter count.
    std::array<SkPoint, 3> shortOutput{};
    m.mapPoints(shortOutput, points);
    append_point(output, shortOutput[2]);
    const SkPoint mappedPoint = m.mapPoint(points[0]);
    append_point(output, mappedPoint);
    if (!m.hasPerspective()) {
        append_point(output, m.mapPointAffine(points[1]));
    }
    append_point(output, m.mapOrigin());
    append_point(output, m.mapVector(points[2]));
    append_point(output, m.mapVector(points[3].fX, points[3].fY));
    append(output, m.mapRadius(pick_ordinary(sequence)));

    const std::array<SkPoint3, 3> homogeneous{
        SkPoint3::Make(points[0].fX, points[0].fY, 1),
        SkPoint3::Make(points[1].fX, points[1].fY, pick_ordinary(sequence)),
        SkPoint3::Make(points[2].fX, points[2].fY, 0),
    };
    std::array<SkPoint3, 3> mappedHomogeneous{};
    m.mapHomogeneousPoints(mappedHomogeneous, homogeneous);
    std::array<SkPoint3, 3> inPlaceHomogeneous = homogeneous;
    m.mapHomogeneousPoints(inPlaceHomogeneous, inPlaceHomogeneous);
    std::array<SkPoint3, 3> fromPoints{};
    m.mapPointsToHomogeneous(fromPoints, SkSpan<const SkPoint>(points.data(), 3));
    for (int i = 0; i < 3; ++i) {
        append(output, mappedHomogeneous[i]);
        append(output, inPlaceHomogeneous[i]);
        append(output, fromPoints[i]);
    }
    append(output, m.mapHomogeneousPoint(homogeneous[1]));
    append(output, m.mapPointToHomogeneous(points[4]));

    const std::array<SkRect, 5> rects{
        SkRect::MakeLTRB(points[0].fX, points[0].fY, points[1].fX, points[1].fY),
        SkRect::MakeXYWH(-3, 2, 10, 4),
        SkRect::MakeLTRB(5, 5, -5, -5),
        SkRect::MakeLTRB(0, 0, corpus::kNaN, 1),
        SkRect::MakeEmpty(),
    };
    for (const SkRect& rect : rects) {
        SkRect mappedRect = SkRect::MakeWH(-1, -1);
        append_bool(output, m.mapRect(&mappedRect, rect));
        append_rect(output, mappedRect);
        append_rect(output, m.mapRect(rect));
        SkRect inPlaceRect = rect;
        append_bool(output, m.mapRect(&inPlaceRect));
        append_rect(output, inPlaceRect);
        SkPoint quad[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
        m.mapRectToQuad(quad, rect);
        for (const SkPoint& corner : quad) {
            append_point(output, corner);
        }
        if (m.isScaleTranslate()) {
            SkRect scaleTranslated{};
            m.mapRectScaleTranslate(&scaleTranslated, rect);
            append_rect(output, scaleTranslated);
        }
    }
    SkPoint applied[5] = {points[0], points[1], points[2], points[3], points[4]};
    const SkMatrixPriv::MapPtsProc proc = SkMatrixPriv::GetMapPtsProc(m);
    proc(m, applied, applied, 5);
    append_point(output, applied[4]);
}

void combine(Output& output, const SkMatrix& a, const SkMatrix& b, Sequence& sequence) {
    SkMatrix concat{};
    concat.setConcat(a, b);
    append_matrix(output, concat);
    append_matrix(output, SkMatrix::Concat(b, a));
    append_matrix(output, a * b);
    SkMatrix selfConcat = a;
    selfConcat.setConcat(selfConcat, selfConcat);
    append_matrix(output, selfConcat);
    SkMatrix pre = a;
    pre.preConcat(b);
    append_matrix(output, pre);
    SkMatrix post = a;
    post.postConcat(b);
    append_matrix(output, post);
    append_bool(output, a == b);
    append_bool(output, a != b);
    append_bool(output, a == a);

    const float x = pick_ordinary(sequence);
    const float y = pick_ordinary(sequence);
    const float px = pick_ordinary(sequence);
    const float py = pick_ordinary(sequence);
    const float degrees = pick_ordinary(sequence);
    SkMatrix edits = a;
    edits.preTranslate(x, y);
    append_matrix(output, edits);
    edits = a;
    edits.postTranslate(x, y);
    append_matrix(output, edits);
    edits = a;
    edits.preScale(x, y);
    append_matrix(output, edits);
    edits = a;
    edits.preScale(x, y, px, py);
    append_matrix(output, edits);
    edits = a;
    edits.preScale(1, 1);
    append_matrix(output, edits);
    edits = a;
    edits.postScale(x, y);
    append_matrix(output, edits);
    edits = a;
    edits.postScale(x, y, px, py);
    append_matrix(output, edits);
    edits = a;
    edits.preRotate(degrees);
    append_matrix(output, edits);
    edits = a;
    edits.preRotate(degrees, px, py);
    append_matrix(output, edits);
    edits = a;
    edits.postRotate(degrees);
    append_matrix(output, edits);
    edits = a;
    edits.postRotate(degrees, px, py);
    append_matrix(output, edits);
    edits = a;
    edits.preSkew(x * 0.125f, y * 0.125f);
    append_matrix(output, edits);
    edits = a;
    edits.preSkew(x, y, px, py);
    append_matrix(output, edits);
    edits = a;
    edits.postSkew(x, y);
    append_matrix(output, edits);
    edits = a;
    edits.postSkew(x, y, px, py);
    append_matrix(output, edits);
    edits = a;
    append_bool(output, SkMatrixPriv::PostIDiv(&edits, static_cast<int>(sequence.next() % 9) - 4,
                                               static_cast<int>(sequence.next() % 9) - 4));
    append_matrix(output, edits);
    edits = a;
    edits[SkMatrix::kMSkewX] = x;
    append_matrix(output, edits);
    edits.set(SkMatrix::kMPersp1, pick_small(sequence));
    append_matrix(output, edits);
    edits.setScaleY(y);
    edits.dirtyMatrixTypeCache();
    append_matrix(output, edits);

    SkMatrix rotated{};
    rotated.setRotate(degrees);
    append_matrix(output, rotated);
    rotated.setRotate(degrees, px, py);
    append_matrix(output, rotated);
    SkMatrix scaled{};
    scaled.setScale(x, y, px, py);
    append_matrix(output, scaled);
    SkMatrix translated{};
    translated.setTranslate(SkVector::Make(x, y));
    append_matrix(output, translated);
}

void fit(Output& output, Sequence& sequence) {
    for (int trial = 0; trial < 1500; ++trial) {
        const SkRect src = SkRect::MakeLTRB(pick_ordinary(sequence), pick_ordinary(sequence),
                                            pick_ordinary(sequence), pick_ordinary(sequence));
        const SkRect dst = SkRect::MakeLTRB(pick_ordinary(sequence), pick_ordinary(sequence),
                                            pick_ordinary(sequence), pick_ordinary(sequence));
        for (int mode = 0; mode < 4; ++mode) {
            const SkMatrix::ScaleToFit stf = static_cast<SkMatrix::ScaleToFit>(mode);
            const std::optional<SkMatrix> fitted = SkMatrix::Rect2Rect(src, dst, stf);
            append_bool(output, fitted.has_value());
            if (fitted.has_value()) {
                append_matrix(output, fitted.value());
            }
            append_matrix(output, SkMatrix::RectToRectOrIdentity(src, dst, stf));
            SkMatrix legacy = SkMatrix::Scale(3, 3);
            append_bool(output, legacy.setRectToRect(src, dst, stf));
            append_matrix(output, legacy);
            append_matrix(output, SkMatrix::RectToRect(src, dst, stf));
        }
        std::array<SkPoint, 4> from{};
        std::array<SkPoint, 4> to{};
        for (int i = 0; i < 4; ++i) {
            from[i] = SkPoint::Make(pick_ordinary(sequence), pick_ordinary(sequence));
            to[i] = SkPoint::Make(pick_ordinary(sequence), pick_ordinary(sequence));
        }
        if (trial % 7 == 0) {
            // A square to a quadrilateral, the common perspective case.
            from = {SkPoint::Make(0, 0), SkPoint::Make(1, 0), SkPoint::Make(1, 1),
                    SkPoint::Make(0, 1)};
        }
        for (size_t count = 0; count <= 4; ++count) {
            const std::optional<SkMatrix> poly =
                    SkMatrix::PolyToPoly(SkSpan<const SkPoint>(from.data(), count),
                                         SkSpan<const SkPoint>(to.data(), count));
            append_bool(output, poly.has_value());
            if (poly.has_value()) {
                append_matrix(output, poly.value());
            }
            SkMatrix setPoly = SkMatrix::Scale(2, 2);
            append_bool(output, setPoly.setPolyToPoly(SkSpan<const SkPoint>(from.data(), count),
                                                      SkSpan<const SkPoint>(to.data(), count)));
            append_matrix(output, setPoly);
        }
        const std::optional<SkMatrix> mismatched =
                SkMatrix::PolyToPoly(SkSpan<const SkPoint>(from.data(), 3),
                                     SkSpan<const SkPoint>(to.data(), 2));
        append_bool(output, mismatched.has_value());
        output.cases += 1;
    }
}

void serialize(Output& output, const SkMatrix& m) {
    uint8_t memory[9 * sizeof(SkScalar)] = {};
    append(output, static_cast<uint64_t>(SkMatrixPriv::WriteToMemory(m, nullptr)));
    append(output, static_cast<uint64_t>(SkMatrixPriv::WriteToMemory(m, memory)));
    output.bytes.insert(output.bytes.end(), memory, memory + sizeof(memory));
    SkMatrix reread = SkMatrix::Scale(4, 4);
    append(output, static_cast<uint64_t>(SkMatrixPriv::ReadFromMemory(&reread, memory,
                                                                     sizeof(memory))));
    append_matrix(output, reread);
    SkMatrix untouched = SkMatrix::Scale(4, 4);
    append(output, static_cast<uint64_t>(SkMatrixPriv::ReadFromMemory(&untouched, memory,
                                                                     sizeof(memory) - 1)));
    append_matrix(output, untouched);
    SkScalar values[9] = {};
    m.get9(values);
    SkMatrix fromValues{};
    fromValues.set9(values);
    append_matrix(output, fromValues);
    SkScalar affine[6] = {1, 2, 3, 4, 5, 6};
    SkMatrix fromAffine{};
    fromAffine.setAffine(affine);
    append_matrix(output, fromAffine);
    SkMatrix::SetAffineIdentity(affine);
    for (const SkScalar value : affine) {
        append(output, value);
    }
}

void sprite(Output& output, const SkMatrix& m) {
    const std::array<SkISize, 3> sizes{SkISize::Make(10, 7), SkISize::Make(0, 5),
                                       SkISize::Make(1000, 1)};
    const std::array<SkSamplingOptions, 3> samplings{
        SkSamplingOptions(SkFilterMode::kNearest), SkSamplingOptions(SkFilterMode::kLinear),
        SkSamplingOptions(SkCubicResampler::Mitchell())};
    const bool linearDefined = sprite_translation_defined(m);
    for (const SkISize& size : sizes) {
        for (size_t s = 0; s < samplings.size(); ++s) {
            if (s == 1 && !linearDefined) {
                continue;
            }
            append_bool(output, SkTreatAsSprite(m, size, samplings[s], false));
            append_bool(output, SkTreatAsSprite(m, size, samplings[s], true));
        }
    }
}

void matrix_corpus(Output& output) {
    Sequence sequence{};
    std::vector<SkMatrix> matrices = named_matrices();
    const size_t namedCount = matrices.size();
    for (int i = 0; i < 1200; ++i) {
        matrices.push_back(random_matrix(sequence));
    }
    // Integral and half-pixel translations with small scales exercise the sprite test.
    for (int i = 0; i < 64; ++i) {
        const float tx = static_cast<float>(static_cast<int32_t>(sequence.next() % 41) - 20) * 0.5f;
        const float ty = static_cast<float>(static_cast<int32_t>(sequence.next() % 41) - 20);
        const float scale = (i % 4 == 0) ? 1.0f : 1.0f + static_cast<float>(i % 3) * 0.0625f;
        matrices.push_back(SkMatrix::ScaleTranslate(scale, scale, tx, ty));
    }
    for (size_t i = 0; i < matrices.size(); ++i) {
        const SkMatrix& m = matrices[i];
        classify(output, m);
        map(output, m, sequence);
        const SkMatrix& other = matrices[(i * 7 + 3) % matrices.size()];
        combine(output, m, other, sequence);
        if (i < namedCount) {
            for (size_t j = 0; j < namedCount; ++j) {
                SkMatrix pair{};
                pair.setConcat(m, matrices[j]);
                append_matrix(output, pair);
            }
        }
        serialize(output, m);
        sprite(output, m);
        output.cases += 1;
    }
    fit(output, sequence);
}

#ifndef STX_SKIA_REFERENCE
template <typename T>
concept HasStrideMapping = requires { &T::MapPointsWithStride; };
template <typename T>
concept HasInverseMapRect = requires { &T::InverseMapRect; };
template <typename T>
concept HasNearlyAffine = requires { &T::NearlyAffine; };
template <typename T>
concept TemporaryM44Values = requires(T& value) { T::M44ColMajor(SkM44()); };
static_assert(!HasStrideMapping<SkMatrixPriv>);
static_assert(!HasInverseMapRect<SkMatrixPriv>);
static_assert(!HasNearlyAffine<SkMatrixPriv>);
static_assert(!TemporaryM44Values<SkMatrixPriv>);

bool corrected_behavior() {
    // Translations outside int are not whole pixels for linear sampling; the original
    // converted them to int, which is undefined.
    const SkSamplingOptions linear(SkFilterMode::kLinear);
    const SkISize size = SkISize::Make(4, 4);
    if (SkTreatAsSprite(SkMatrix::Translate(1e20f, 0), size, linear, false)) return false;
    if (SkTreatAsSprite(SkMatrix::Translate(0, -3e9f), size, linear, true)) return false;
    if (SkTreatAsSprite(SkMatrix::Translate(corpus::kNaN, 0), size, linear, false)) return false;
    if (SkTreatAsSprite(SkMatrix::Translate(corpus::kInf, 0), size, linear, false)) return false;
    // In-range whole and half pixels keep their original answers.
    if (!SkTreatAsSprite(SkMatrix::Translate(-2147483520.0f, 3), size, linear, false)) return false;
    if (SkTreatAsSprite(SkMatrix::Translate(0.5f, 3), size, linear, false)) return false;

    // An affine mapping of a single point leaves the upper vector lanes zero; the result
    // depends only on that point.
    const SkMatrix affine = SkMatrix::MakeAll(2, 1, 3, -1, 4, 5, 0, 0, 1);
    const SkPoint one[1] = {SkPoint::Make(1, 2)};
    SkPoint mapped[1] = {{0, 0}};
    affine.mapPoints(mapped, one);
    if (mapped[0] != SkPoint::Make(7, 12)) return false;
    return true;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
#ifndef STX_SKIA_REFERENCE
    if (!corrected_behavior()) {
        std::printf("matrix transforms: corrected behavior FAIL\n");
        return 1;
    }
#endif
    Output output{};
    matrix_corpus(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("matrix transforms: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
