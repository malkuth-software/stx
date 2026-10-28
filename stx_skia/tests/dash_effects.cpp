// Differential corpus for dashing and path-effect composition: dash construction and
// validation, dash parameters, filtering random and special paths with and without a cull
// rectangle, the point fast path, sum and compose effects, and their flattened bytes.

#include "differential_corpus.h"

#include "include/core/SkData.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSpan.h"
#include "include/core/SkStrokeRec.h"
#include "include/effects/SkDashPathEffect.h"
#include "src/core/SkPathEffectBase.h"
#include "src/utils/SkDashPathPriv.h"

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

float pick_coordinate(Sequence& sequence, bool allowSpecial) {
    if (allowSpecial && sequence.next() % 20 == 0) {
        const std::array<float, 6> specials{0.0f, -0.0f, 1e-7f, 1e20f, -1e20f, 1e-4f};
        const float special = specials[sequence.pick(specials.size())];
        return special;
    }
    const int32_t whole = static_cast<int32_t>(sequence.next() % 257) - 128;
    const float fraction = static_cast<float>(sequence.next() % 8) * 0.125f;
    const float value = static_cast<float>(whole) + fraction;
    return value;
}

SkPoint pick_point(Sequence& sequence, bool allowSpecial, const SkPoint& previous) {
    const uint32_t choice = sequence.next() % 10;
    if (choice == 0) {
        return previous;
    }
    if (choice == 1) {
        // Axis-aligned with the previous point.
        const SkPoint aligned{previous.fX, pick_coordinate(sequence, allowSpecial)};
        return aligned;
    }
    if (choice == 2) {
        const SkPoint aligned{pick_coordinate(sequence, allowSpecial), previous.fY};
        return aligned;
    }
    const SkPoint point{pick_coordinate(sequence, allowSpecial),
                        pick_coordinate(sequence, allowSpecial)};
    return point;
}

SkPath random_path(Sequence& sequence, bool allowSpecial) {
    const uint32_t shape = sequence.next() % 8;
    if (shape == 0) {
        const SkPoint a{pick_coordinate(sequence, false), pick_coordinate(sequence, false)};
        const SkPoint b = pick_point(sequence, allowSpecial, a);
        const SkPath line = SkPath::Line(a, b);
        return line;
    }
    if (shape == 1) {
        const float l = pick_coordinate(sequence, false);
        const float t = pick_coordinate(sequence, false);
        const SkRect rect = SkRect::MakeLTRB(l, t, l + static_cast<float>(sequence.next() % 90),
                                             t + static_cast<float>(sequence.next() % 90));
        const SkPathDirection dir = sequence.next() % 2 == 0 ? SkPathDirection::kCW
                                                             : SkPathDirection::kCCW;
        const SkPath rectPath = SkPath::Rect(rect, dir, sequence.next() % 4);
        return rectPath;
    }
    SkPathBuilder builder{};
    const int contours = 1 + static_cast<int>(sequence.next() % 2);
    SkPoint last{0, 0};
    for (int contour = 0; contour < contours; ++contour) {
        last = pick_point(sequence, allowSpecial, last);
        builder.moveTo(last);
        const int verbs = 1 + static_cast<int>(sequence.next() % 5);
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
                builder.conicTo(p1, p2, 0.70710678f);
                last = p2;
            } else {
                builder.cubicTo(p1, p2, p3);
                last = p3;
            }
        }
        if (sequence.next() % 3 == 0) {
            builder.close();
        }
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
    append(output, static_cast<uint8_t>(path.getFillType()));
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

void append_rec(Output& output, const SkStrokeRec& rec) {
    append(output, static_cast<uint8_t>(rec.getStyle()));
    append(output, rec.getWidth());
    append(output, rec.getMiter());
    append(output, static_cast<uint8_t>(rec.getCap()));
    append(output, static_cast<uint8_t>(rec.getJoin()));
}

std::vector<float> random_intervals(Sequence& sequence) {
    std::vector<float> intervals{};
    const uint32_t shape = sequence.next() % 10;
    if (shape == 0) {
        intervals = {5.0f};                     // odd count: invalid
    } else if (shape == 1) {
        intervals = {4.0f, -1.0f};              // negative: invalid
    } else if (shape == 2) {
        intervals = {0.0f, 0.0f};               // zero length: invalid
    } else if (shape == 3) {
        intervals = {0.0f, 6.0f};               // dots
    } else if (shape == 4) {
        intervals = {3.0f, 3.0f};               // the point fast path
    } else if (shape == 5) {
        intervals = {0.25f, 0.75f};             // short dashes
    } else {
        const int pairs = 1 + static_cast<int>(sequence.next() % 3);
        for (int i = 0; i < 2 * pairs; ++i) {
            intervals.push_back(static_cast<float>(sequence.next() % 33) * 0.5f);
        }
    }
    return intervals;
}

float random_phase(Sequence& sequence) {
    const std::array<float, 9> phases{
        0.0f, 1.0f, 2.5f, -3.0f, 25.0f, -1000.0f, 1e7f, corpus::kNaN, 0.5f,
    };
    const float phase = phases[sequence.pick(phases.size())];
    return phase;
}

SkStrokeRec random_rec(Sequence& sequence) {
    SkPaint paint{};
    paint.setStyle(sequence.next() % 6 == 0 ? SkPaint::kFill_Style : SkPaint::kStroke_Style);
    const std::array<float, 5> widths{0.0f, 1.0f, 2.0f, 8.0f, 0.5f};
    paint.setStrokeWidth(widths[sequence.pick(widths.size())]);
    paint.setStrokeCap(static_cast<SkPaint::Cap>(sequence.next() % SkPaint::kCapCount));
    paint.setStrokeJoin(static_cast<SkPaint::Join>(sequence.next() % SkPaint::kJoinCount));
    paint.setStrokeMiter(sequence.next() % 2 == 0 ? 4.0f : 1.0f);
    const SkStrokeRec rec(paint);
    return rec;
}

void dash_parameter_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 2000; ++i) {
        const std::vector<float> intervals = random_intervals(sequence);
        const float phase = random_phase(sequence);
        const SkSpan<const float> span(intervals.data(), intervals.size());
        const bool valid = SkDashPath::ValidDashPath(phase, span);
        append_bool(output, valid);
        if (valid) {
            float initialLength = -7;
            size_t initialIndex = 99;
            float intervalLength = -7;
            float adjustedPhase = -7;
            SkDashPath::CalcDashParameters(phase, span, &initialLength, &initialIndex,
                                           &intervalLength, &adjustedPhase);
            append(output, initialLength);
            append(output, initialIndex);
            append(output, intervalLength);
            append(output, adjustedPhase);
        }
        const sk_sp<SkPathEffect> effect = SkDashPathEffect::Make(span, phase);
        append_bool(output, effect != nullptr);
        if (effect) {
            const std::optional<SkPathEffectBase::DashInfo> info = (*as_PEB(effect)).asADash();
            append_bool(output, info.has_value());
            if (info.has_value()) {
                append(output, info.value().fPhase);
                append(output, info.value().fIntervals.size());
                for (const float interval : info.value().fIntervals) {
                    append(output, interval);
                }
            }
        }
        ++output.cases;
    }
}

void dash_filter_corpus(Output& output, Sequence& sequence) {
    const std::array<SkRect, 3> culls{
        SkRect::MakeLTRB(-20, -20, 20, 20), SkRect::MakeLTRB(0, 0, 200, 10),
        SkRect::MakeLTRB(-1e4f, -1e4f, 1e4f, 1e4f),
    };
    for (int i = 0; i < 2500; ++i) {
        const SkPath path = random_path(sequence, i % 6 == 0);
        std::vector<float> intervals = random_intervals(sequence);
        const float phase = random_phase(sequence);
        const sk_sp<SkPathEffect> effect =
                SkDashPathEffect::Make(SkSpan<const float>(intervals.data(), intervals.size()),
                                       phase);
        append_bool(output, effect != nullptr);
        if (!effect) {
            ++output.cases;
            continue;
        }
        for (int variant = 0; variant < 3; ++variant) {
            SkStrokeRec rec = random_rec(sequence);
            const SkRect* cull = variant == 0 ? nullptr : &culls[sequence.pick(culls.size())];
            SkPathBuilder builder{};
            const bool filtered = (*effect).filterPath(&builder, path, &rec, cull,
                                                       SkMatrix::I());
            append_bool(output, filtered);
            append_rec(output, rec);
            const SkPath dashed = builder.detach();
            append_path(output, dashed);
        }
        SkStrokeRec plain = random_rec(sequence);
        SkPathBuilder plainBuilder{};
        append_bool(output, (*effect).filterPath(&plainBuilder, path, &plain));
        const SkPath plainDashed = plainBuilder.detach();
        append_path(output, plainDashed);

        // The internal filter without stroke-record application.
        SkStrokeRec rec = random_rec(sequence);
        const std::optional<SkPathEffectBase::DashInfo> info = (*as_PEB(effect)).asADash();
        if (info.has_value()) {
            float initialLength = 0;
            size_t initialIndex = 0;
            float intervalLength = 0;
            float adjustedPhase = 0;
            SkDashPath::CalcDashParameters(info.value().fPhase, info.value().fIntervals,
                                           &initialLength, &initialIndex, &intervalLength,
                                           &adjustedPhase);
            SkPathBuilder internal{};
            append_bool(output, SkDashPath::InternalFilter(
                    &internal, path, &rec, nullptr, info.value().fIntervals, initialLength,
                    static_cast<int32_t>(initialIndex), intervalLength, adjustedPhase,
                    SkDashPath::StrokeRecApplication::kDisallow));
            append_rec(output, rec);
            const SkPath internalPath = internal.detach();
            append_path(output, internalPath);
            SkPathBuilder viaInfo{};
            SkStrokeRec infoRec = random_rec(sequence);
            append_bool(output, SkDashPath::FilterDashPath(&viaInfo, path, &infoRec, nullptr,
                                                           info.value()));
            const SkPath infoPath = viaInfo.detach();
            append_path(output, infoPath);
        }
        ++output.cases;
    }
}

void dash_points_corpus(Output& output, Sequence& sequence) {
    const std::array<SkMatrix, 4> matrices{
        SkMatrix::I(), SkMatrix::Scale(2, 3), SkMatrix::Translate(5, -7),
        SkMatrix::RotateDeg(30),
    };
    for (int i = 0; i < 1500; ++i) {
        const float on = static_cast<float>(1 + sequence.next() % 6);
        const std::array<float, 2> even{on, sequence.next() % 4 == 0 ? on + 1 : on};
        const float phase = static_cast<float>(sequence.next() % 13) - 4;
        const sk_sp<SkPathEffect> effect =
                SkDashPathEffect::Make(SkSpan<const float>(even.data(), even.size()), phase);
        const SkPoint a{pick_coordinate(sequence, false), pick_coordinate(sequence, false)};
        const uint32_t orientation = sequence.next() % 3;
        SkPoint b{a.fX + pick_coordinate(sequence, false), a.fY};
        if (orientation == 1) {
            b = SkPoint{a.fX, a.fY + pick_coordinate(sequence, false)};
        } else if (orientation == 2) {
            b = SkPoint{a.fX + 7, a.fY + 3};
        }
        const SkPath line = SkPath::Line(a, b);
        const SkStrokeRec rec = random_rec(sequence);
        const SkRect cull = SkRect::MakeLTRB(-30, -30, 30 + static_cast<float>(i % 200), 30);
        const SkMatrix& matrix = matrices[sequence.pick(matrices.size())];
        SkPathEffectBase::PointData data{};
        const bool asPoints = (*as_PEB(effect)).asPoints(&data, line, rec, matrix, &cull);
        append_bool(output, asPoints);
        if (asPoints) {
            append(output, data.fFlags);
            append(output, data.fNumPoints);
            append_point(output, data.fSize);
            for (const SkPoint& point : data.points()) {
                append_point(output, point);
            }
            append_path(output, data.fFirst);
            append_path(output, data.fLast);
        }
        ++output.cases;
    }
}

void composition_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 800; ++i) {
        std::vector<float> first = random_intervals(sequence);
        std::vector<float> second = random_intervals(sequence);
        const sk_sp<SkPathEffect> a =
                SkDashPathEffect::Make(SkSpan<const float>(first.data(), first.size()),
                                       random_phase(sequence));
        const sk_sp<SkPathEffect> b =
                SkDashPathEffect::Make(SkSpan<const float>(second.data(), second.size()),
                                       random_phase(sequence));
        const sk_sp<SkPathEffect> sum = SkPathEffect::MakeSum(a, b);
        const sk_sp<SkPathEffect> compose = SkPathEffect::MakeCompose(a, b);
        append_bool(output, sum != nullptr);
        append_bool(output, compose != nullptr);
        append_bool(output, sum == a || sum == b);
        append_bool(output, compose == a || compose == b);
        const SkPath path = random_path(sequence, false);
        const std::array<const SkPathEffect*, 2> effects{sum.get(), compose.get()};
        for (const SkPathEffect* effect : effects) {
            if (effect == nullptr) {
                continue;
            }
            SkStrokeRec rec = random_rec(sequence);
            SkPathBuilder builder{};
            append_bool(output, (*effect).filterPath(&builder, path, &rec));
            append_rec(output, rec);
            const SkPath filtered = builder.detach();
            append_path(output, filtered);
            append_bool(output, (*effect).needsCTM());
            SkRect bounds = SkRect::MakeLTRB(1, 2, 3, 4);
            append_bool(output, (*as_PEB(effect)).computeFastBounds(&bounds));

            // The reference build can decode by name; the candidate rejects named decoding,
            // so only the flattened bytes are compared.
            const sk_sp<SkData> data = (*effect).serialize();
            append_bool(output, data != nullptr);
            if (data) {
                append(output, (*data).size());
                const uint8_t* bytes = static_cast<const uint8_t*>((*data).data());
                for (size_t index = 0; index < (*data).size(); ++index) {
                    append(output, bytes[index]);
                }
            }
        }
        ++output.cases;
    }
}

void dense_dash_corpus(Output& output) {
    // Dash counts above the limit give up and clear the destination.
    const std::array<float, 2> dense{1e-3f, 1e-3f};
    const sk_sp<SkPathEffect> effect =
            SkDashPathEffect::Make(SkSpan<const float>(dense.data(), dense.size()), 0);
    // The first line needs five million dashes; the second fifteen thousand.
    const std::array<SkPath, 2> paths{SkPath::Line({0, 0}, {1e4f, 0}),
                                      SkPath::Line({0, 0}, {18, 24})};
    for (const SkPath& path : paths) {
        SkPaint paint{};
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(2);
        SkStrokeRec rec(paint);
        SkPathBuilder builder{};
        builder.moveTo(1, 1);
        append_bool(output, (*effect).filterPath(&builder, path, &rec));
        append_rec(output, rec);
        const SkPath dashed = builder.detach();
        append(output, dashed.countPoints());
        ++output.cases;
    }
}

#ifndef STX_SKIA_REFERENCE
bool corrected_behavior() {
    // The point fast path accepts a null result for axis-aligned lines; the original wrote
    // the point size through the null pointer first.
    const std::array<float, 2> intervals{2, 2};
    const sk_sp<SkPathEffect> effect =
            SkDashPathEffect::Make(SkSpan<const float>(intervals.data(), intervals.size()), 0);
    SkPaint paint{};
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(3);
    const SkStrokeRec rec(paint);
    const SkRect cull = SkRect::MakeLTRB(-100, -100, 100, 100);
    const SkPath horizontal = SkPath::Line({0, 0}, {20, 0});
    const SkPath vertical = SkPath::Line({0, 0}, {0, 20});
    if (!(*as_PEB(effect)).asPoints(nullptr, horizontal, rec, SkMatrix::I(), &cull)) return false;
    if (!(*as_PEB(effect)).asPoints(nullptr, vertical, rec, SkMatrix::I(), &cull)) return false;

    // Point results own their storage; moving transfers it and reuse replaces it.
    SkPathEffectBase::PointData data{};
    if (!(*as_PEB(effect)).asPoints(&data, horizontal, rec, SkMatrix::I(), &cull)) return false;
    const size_t count = data.points().size();
    if (count == 0) return false;
    if (!(*as_PEB(effect)).asPoints(&data, vertical, rec, SkMatrix::I(), &cull)) return false;
    if (data.points().size() != count) return false;
    SkPathEffectBase::PointData moved(std::move(data));
    if (moved.points().size() != count) return false;
    return true;
}
#endif

void dash_corpus(Output& output) {
    Sequence sequence{};
    dash_parameter_corpus(output, sequence);
    dash_filter_corpus(output, sequence);
    dash_points_corpus(output, sequence);
    composition_corpus(output, sequence);
    dense_dash_corpus(output);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
#ifndef STX_SKIA_REFERENCE
    if (!corrected_behavior()) {
        std::printf("dash effects: corrected behavior FAIL\n");
        return 1;
    }
#endif
    Output output{};
    dash_corpus(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("dash effects: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
