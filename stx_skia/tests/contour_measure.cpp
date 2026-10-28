// Differential corpus for contour measurement: contour iteration over lines, quadratics,
// conics and cubics at several resolutions, lengths, positions and tangents, matrices,
// extracted segments, per-verb measures and the SkPathMeasure facade.

#include "differential_corpus.h"

#include "include/core/SkContourMeasure.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathMeasure.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSpan.h"
#include "src/core/SkPathMeasurePriv.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

const std::array<float, 10> kSpecials{
    0.0f, -0.0f, 1e-7f, 1e-30f, 1e20f, -1e20f, 3e38f, corpus::kDenormal, 16777217.0f, 1e-4f,
};

const std::array<float, 8> kWeights{
    0.5f, 0.70710678f, 1.0f, 2.0f, 1e-6f, 1e6f, 0.0f, 10.0f,
};

const std::array<float, 7> kResScales{
    1.0f, 0.5f, 4.0f, 1000.0f, 0.0f, corpus::kNaN, 1e-30f,
};

// Mostly ordinary values in [-64, 64] at eighth steps, occasionally a special value.
float pick_coordinate(Sequence& sequence, bool allowSpecial) {
    if (allowSpecial && sequence.next() % 16 == 0) {
        const float special = kSpecials[sequence.pick(kSpecials.size())];
        return special;
    }
    const int32_t whole = static_cast<int32_t>(sequence.next() % 129) - 64;
    const float fraction = static_cast<float>(sequence.next() % 8) * 0.125f;
    const float value = static_cast<float>(whole) + fraction;
    return value;
}

SkPoint pick_point(Sequence& sequence, bool allowSpecial, const SkPoint& previous) {
    if (sequence.next() % 9 == 0) {
        return previous;
    }
    const SkPoint point{pick_coordinate(sequence, allowSpecial),
                        pick_coordinate(sequence, allowSpecial)};
    return point;
}

SkPath random_path(Sequence& sequence, bool allowSpecial) {
    SkPathBuilder builder{};
    const int contours = 1 + static_cast<int>(sequence.next() % 3);
    SkPoint last{0, 0};
    for (int contour = 0; contour < contours; ++contour) {
        last = pick_point(sequence, allowSpecial, last);
        builder.moveTo(last);
        const int verbs = static_cast<int>(sequence.next() % 6);
        for (int verb = 0; verb < verbs; ++verb) {
            const uint32_t kind = sequence.next() % 4;
            const SkPoint p1 = pick_point(sequence, allowSpecial, last);
            const SkPoint p2 = pick_point(sequence, allowSpecial, p1);
            const SkPoint p3 = pick_point(sequence, allowSpecial, p2);
            if (kind == 0) {
                builder.lineTo(p1);
                last = p1;
            } else if (kind == 1) {
                builder.quadTo(p1, p2);
                last = p2;
            } else if (kind == 2) {
                builder.conicTo(p1, p2, kWeights[sequence.pick(kWeights.size())]);
                last = p2;
            } else {
                builder.cubicTo(p1, p2, p3);
                last = p3;
            }
        }
        const uint32_t closing = sequence.next() % 4;
        if (closing == 0) {
            builder.close();
        } else if (closing == 1) {
            builder.close();
            builder.close();
        }
    }
    if (sequence.next() % 8 == 0) {
        // A trailing move with nothing after it.
        builder.moveTo(pick_point(sequence, allowSpecial, last));
    }
    const SkPath path = builder.detach();
    return path;
}

void append_point(Output& output, const SkPoint& point) {
    append(output, point.fX);
    append(output, point.fY);
}

void append_path(Output& output, const SkPath& path) {
    const SkSpan<const SkPathVerb> verbs = path.verbs();
    const SkSpan<const SkPoint> points = path.points();
    const SkSpan<const float> weights = path.conicWeights();
    append(output, verbs.size());
    for (const SkPathVerb verb : verbs) {
        append(output, static_cast<uint8_t>(verb));
    }
    append(output, points.size());
    for (const SkPoint& point : points) {
        append_point(output, point);
    }
    append(output, weights.size());
    for (const float weight : weights) {
        append(output, weight);
    }
}

void append_matrix(Output& output, const SkMatrix& matrix) {
    for (int i = 0; i < 9; ++i) {
        append(output, matrix.get(i));
    }
}

void append_contour(Output& output, Sequence& sequence, const SkContourMeasure& contour) {
    const float length = contour.length();
    append(output, length);
    append_bool(output, contour.isClosed());

    const std::array<float, 11> fractions{
        0.0f, 0.125f, 0.25f, 0.33333334f, 0.5f, 0.75f, 0.99999994f, 1.0f, -0.5f, 1.5f, 0.6f,
    };
    for (const float fraction : fractions) {
        const float distance = length * fraction;
        SkPoint position{-7, -7};
        SkVector tangent{-7, -7};
        append_bool(output, contour.getPosTan(distance, &position, &tangent));
        append_point(output, position);
        append_point(output, tangent);
        SkPoint positionOnly{-7, -7};
        append_bool(output, contour.getPosTan(distance, &positionOnly, nullptr));
        append_point(output, positionOnly);
        SkVector tangentOnly{-7, -7};
        append_bool(output, contour.getPosTan(distance, nullptr, &tangentOnly));
        append_point(output, tangentOnly);
    }
    SkPoint unused{-7, -7};
    append_bool(output, contour.getPosTan(corpus::kNaN, &unused, nullptr));
    append_point(output, unused);
    append_bool(output, contour.getPosTan(corpus::kInf, &unused, nullptr));
    append_point(output, unused);

    const std::array<SkContourMeasure::MatrixFlags, 3> flags{
        SkContourMeasure::kGetPosition_MatrixFlag, SkContourMeasure::kGetTangent_MatrixFlag,
        SkContourMeasure::kGetPosAndTan_MatrixFlag,
    };
    for (const SkContourMeasure::MatrixFlags flag : flags) {
        SkMatrix matrix = SkMatrix::Scale(5, 7);
        append_bool(output, contour.getMatrix(length * 0.4f, &matrix, flag));
        append_matrix(output, matrix);
    }
    append_bool(output, contour.getMatrix(length * 0.4f, nullptr));

    for (int i = 0; i < 6; ++i) {
        float startD = length * static_cast<float>(sequence.next() % 17) / 16.0f;
        float stopD = length * static_cast<float>(sequence.next() % 17) / 16.0f;
        if (i == 0) {
            startD = -1;
            stopD = length + 1;
        } else if (i == 1) {
            stopD = startD;
        } else if (i == 2) {
            startD = corpus::kNaN;
        }
        for (int moveTo = 0; moveTo < 2; ++moveTo) {
            SkPathBuilder builder{};
            if (i == 3) {
                builder.moveTo(1, 2);
                builder.lineTo(3, 4);
            }
            append_bool(output, contour.getSegment(startD, stopD, &builder, moveTo == 1));
            const SkPath segment = builder.detach();
            append_path(output, segment);
        }
    }

    size_t verbCount = 0;
    for (const SkContourMeasure::VerbMeasure measure : contour) {
        append(output, measure.fDistance);
        append(output, static_cast<uint8_t>(measure.fVerb));
        append(output, measure.fPts.size());
        for (const SkPoint& point : measure.fPts) {
            append_point(output, point);
        }
        ++verbCount;
    }
    append(output, verbCount);
}

void contour_iteration_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 1800; ++i) {
        const SkPath path = random_path(sequence, i % 4 == 0);
        const bool forceClosed = sequence.next() % 3 == 0;
        const float resScale = i % 5 == 0 ? kResScales[sequence.pick(kResScales.size())] : 1.0f;
        SkContourMeasureIter iter(path, forceClosed, resScale);
        int contours = 0;
        sk_sp<SkContourMeasure> contour = iter.next();
        while (contour) {
            append_contour(output, sequence, *contour);
            ++contours;
            contour = iter.next();
        }
        append(output, contours);
        append_bool(output, iter.next() != nullptr);
        ++output.cases;
    }
}

void path_measure_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 1200; ++i) {
        const SkPath path = random_path(sequence, i % 4 == 0);
        const bool forceClosed = i % 2 == 0;
        SkPathMeasure measure(path, forceClosed, i % 7 == 0 ? 3.0f : 1.0f);
        int contours = 0;
        do {
            const float length = measure.getLength();
            append(output, length);
            append_bool(output, measure.isClosed());
            append(output, SkPathMeasurePriv::CountSegments(measure));
            SkPoint position{-7, -7};
            SkVector tangent{-7, -7};
            append_bool(output, measure.getPosTan(length * 0.3f, &position, &tangent));
            append_point(output, position);
            append_point(output, tangent);
            SkMatrix matrix = SkMatrix::I();
            append_bool(output, measure.getMatrix(length * 0.6f, &matrix,
                                                  SkPathMeasure::kGetPosAndTan_MatrixFlag));
            append_matrix(output, matrix);
            SkPathBuilder builder{};
            append_bool(output, measure.getSegment(length * 0.2f, length * 0.9f, &builder, true));
            const SkPath segment = builder.detach();
            append_path(output, segment);
            append_bool(output, measure.currentMeasure() != nullptr);
            ++contours;
        } while (measure.nextContour() && contours < 8);
        append(output, contours);

        // Moving keeps the remaining contours; resetting starts over or clears.
        SkPathMeasure moved(std::move(measure));
        append(output, moved.getLength());
        SkPathMeasure assigned{};
        assigned = std::move(moved);
        append(output, assigned.getLength());
        assigned.setPath(&path, !forceClosed);
        append(output, assigned.getLength());
        append_bool(output, assigned.isClosed());
        assigned.setPath(nullptr, forceClosed);
        append(output, assigned.getLength());
        append_bool(output, assigned.nextContour());
        SkPoint unused{-7, -7};
        append_bool(output, assigned.getPosTan(0, &unused, nullptr));
        append_point(output, unused);
        ++output.cases;
    }

    const SkPathMeasure empty{};
    append(output, SkPathMeasurePriv::CountSegments(empty));
    SkContourMeasureIter none{};
    append_bool(output, none.next() != nullptr);
    SkContourMeasureIter reused(SkPath::Rect(SkRect::MakeLTRB(0, 0, 3, 4)), false);
    SkContourMeasureIter target{};
    target = std::move(reused);
    const sk_sp<SkContourMeasure> rect = target.next();
    append_bool(output, rect != nullptr);
    if (rect) {
        append(output, (*rect).length());
    }
    target.reset(SkPath::Line({0, 0}, {corpus::kNaN, 1}), false);
    append_bool(output, target.next() != nullptr);
    ++output.cases;
}

void contour_corpus(Output& output) {
    Sequence sequence{};
    contour_iteration_corpus(output, sequence);
    path_measure_corpus(output, sequence);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
    Output output{};
    contour_corpus(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("contour measure: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
