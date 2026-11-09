// Differential corpus for path storage, construction, queries, iteration and
// serialization. Every builder operation, factory, shape query, iterator and the
// binary form is exercised over ordinary, degenerate and non-finite coordinates.

#include "differential_corpus.h"

#include "include/core/SkData.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathIter.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkStream.h"
#include "stream_sink.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPathRawShapes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

const std::array<float, 16> kSpecials{
    0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 1e-7f, 1e-30f, 1e20f, -1e20f, 3e38f,
    corpus::kFloatMax, corpus::kInf, -corpus::kInf, corpus::kNaN, 16777217.0f, 1e-4f,
};

float pick_special(Sequence& sequence) {
    const float value = kSpecials[sequence.pick(kSpecials.size())];
    return value;
}

// Mostly coordinates on an eighth-pixel grid in [-48, 48]; rarely a special value.
float pick_coordinate(Sequence& sequence, bool allowSpecial) {
    if (allowSpecial && sequence.next() % 24 == 0) {
        const float special = pick_special(sequence);
        return special;
    }
    const int32_t whole = static_cast<int32_t>(sequence.next() % 97) - 48;
    const float fraction = static_cast<float>(sequence.next() % 8) * 0.125f;
    const float value = static_cast<float>(whole) + fraction;
    return value;
}

SkPoint pick_point(Sequence& sequence, bool allowSpecial) {
    const SkPoint point = SkPoint::Make(pick_coordinate(sequence, allowSpecial),
                                        pick_coordinate(sequence, allowSpecial));
    return point;
}

float pick_weight(Sequence& sequence) {
    const std::array<float, 8> weights{0.5f, 0.70710678f, 1.0f, 2.0f, 0.0f, -1.0f,
                                       corpus::kInf, corpus::kNaN};
    const float weight = weights[sequence.pick(weights.size())];
    return weight;
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

void append_rrect(Output& output, const SkRRect& rrect) {
    append_rect(output, rrect.rect());
    for (const SkVector& radius : rrect.radii()) {
        append_point(output, radius);
    }
    append(output, static_cast<int32_t>(rrect.getType()));
}

void append_storage(Output& output, SkSpan<const SkPoint> points,
                    SkSpan<const SkPathVerb> verbs, SkSpan<const float> conics) {
    append(output, static_cast<uint64_t>(points.size()));
    append(output, static_cast<uint64_t>(verbs.size()));
    append(output, static_cast<uint64_t>(conics.size()));
    for (const SkPoint& point : points) {
        append_point(output, point);
    }
    for (const SkPathVerb verb : verbs) {
        append(output, static_cast<uint8_t>(verb));
    }
    for (const float weight : conics) {
        append(output, weight);
    }
}

void append_path(Output& output, const SkPath& path) {
    append_storage(output, path.points(), path.verbs(), path.conicWeights());
    append(output, static_cast<int32_t>(path.getFillType()));
    append_bool(output, path.isVolatile());
    append_bool(output, path.isFinite());
    append_bool(output, path.isValid());
    append_rect(output, path.getBounds());
    append(output, path.getSegmentMasks());
}

void append_builder(Output& output, const SkPathBuilder& builder) {
    append_storage(output, builder.points(), builder.verbs(), builder.conicWeights());
    append(output, static_cast<int32_t>(builder.fillType()));
    append_bool(output, builder.isFinite());
    append_bool(output, builder.isEmpty());
    append(output, builder.countPoints());
    append_rect(output, builder.computeBounds());
    const std::optional<SkRect> finite = builder.computeFiniteBounds();
    append_bool(output, finite.has_value());
    if (finite.has_value()) {
        append_rect(output, finite.value());
    }
    const std::optional<SkRect> tight = builder.computeTightBounds();
    append_bool(output, tight.has_value());
    if (tight.has_value()) {
        append_rect(output, tight.value());
    }
    const std::optional<SkPoint> last = builder.getLastPt();
    append_bool(output, last.has_value());
    if (last.has_value()) {
        append_point(output, last.value());
    }
}

// One random drawing operation, chosen from every builder entry point.
void random_operation(SkPathBuilder& builder, Sequence& sequence, bool allowSpecial) {
    const uint32_t op = sequence.next() % 30;
    const SkPoint a = pick_point(sequence, allowSpecial);
    const SkPoint b = pick_point(sequence, allowSpecial);
    const SkPoint c = pick_point(sequence, allowSpecial);
    const SkRect rect = SkRect::MakeLTRB(a.fX, a.fY, b.fX, b.fY);
    const SkPathDirection dir = (sequence.next() & 1) ? SkPathDirection::kCW
                                                      : SkPathDirection::kCCW;
    const unsigned start = sequence.next() % 9;
    switch (op) {
        case 0: builder.moveTo(a); break;
        case 1: builder.lineTo(a); break;
        case 2: builder.quadTo(a, b); break;
        case 3: builder.conicTo(a, b, pick_weight(sequence)); break;
        case 4: builder.cubicTo(a, b, c); break;
        case 5: builder.close(); break;
        case 6: builder.rMoveTo(a); break;
        case 7: builder.rLineTo(a); break;
        case 8: builder.rQuadTo(a, b); break;
        case 9: builder.rConicTo(a, b, pick_weight(sequence)); break;
        case 10: builder.rCubicTo(a, b, c); break;
        case 11: {
            const std::array<SkPoint, 3> polyline{a, b, c};
            builder.polylineTo(polyline);
            break;
        }
        case 12:
            builder.arcTo(rect, pick_coordinate(sequence, false) * 7.5f,
                          pick_coordinate(sequence, false) * 7.5f, (sequence.next() & 1) != 0);
            break;
        case 13: builder.arcTo(a, b, pick_coordinate(sequence, allowSpecial)); break;
        case 14:
            builder.arcTo(SkPoint::Make(c.fX * 0.5f, c.fY * 0.25f),
                          pick_coordinate(sequence, false) * 3.75f,
                          (sequence.next() & 1) ? SkPathBuilder::kLarge_ArcSize
                                                : SkPathBuilder::kSmall_ArcSize,
                          dir, b);
            break;
        case 15:
            builder.rArcTo(SkPoint::Make(c.fX, c.fY * 0.5f), pick_coordinate(sequence, false),
                           (sequence.next() & 1) ? SkPathBuilder::kLarge_ArcSize
                                                 : SkPathBuilder::kSmall_ArcSize,
                           dir, b);
            break;
        case 16:
            builder.addArc(rect, pick_coordinate(sequence, false) * 7.5f,
                           pick_coordinate(sequence, false) * 15.0f);
            break;
        case 17: builder.addLine(a, b); break;
        case 18: builder.addRect(rect, dir, start); break;
        case 19: builder.addOval(rect, dir, start); break;
        case 20: {
            const SkRRect rrect = SkRRect::MakeRectXY(rect.makeSorted(), c.fX * 0.25f,
                                                      c.fY * 0.25f);
            builder.addRRect(rrect, dir, start);
            break;
        }
        case 21: builder.addCircle(a, pick_coordinate(sequence, allowSpecial), dir); break;
        case 22: {
            const std::array<SkPoint, 4> polygon{a, b, c, pick_point(sequence, allowSpecial)};
            builder.addPolygon(SkSpan<const SkPoint>(polygon.data(), sequence.pick(5)),
                               (sequence.next() & 1) != 0);
            break;
        }
        case 23: builder.offset(a.fX, a.fY); break;
        case 24: {
            const SkMatrix matrix = SkMatrix::MakeAll(1, a.fX * 0.0625f, b.fX, a.fY * 0.0625f,
                                                      1, b.fY, 0, 0, 1);
            builder.transform(matrix);
            break;
        }
        case 25: builder.toggleInverseFillType(); break;
        case 26:
            if (builder.countPoints() > 0) {
                builder.setLastPt(a);
            }
            break;
        case 27:
            if (builder.countPoints() > 0) {
                builder.setPoint(sequence.pick(static_cast<size_t>(builder.countPoints())), b);
            }
            break;
        case 28: builder.incReserve(static_cast<int>(sequence.next() % 5),
                                    static_cast<int>(sequence.next() % 5),
                                    static_cast<int>(sequence.next() % 3));
            break;
        default: builder.setFillType(static_cast<SkPathFillType>(sequence.next() % 4)); break;
    }
}

std::vector<SkPath> factory_paths(Sequence& sequence) {
    std::vector<SkPath> paths{};
    paths.push_back(SkPath());
    paths.push_back(SkPath(SkPathFillType::kEvenOdd));
    const std::array<SkRect, 6> rects{
        SkRect::MakeLTRB(1, 2, 30, 20), SkRect::MakeLTRB(30, 20, 1, 2),
        SkRect::MakeLTRB(0, 0, 0, 10), SkRect::MakeLTRB(-1e20f, 0, 1e20f, 4),
        SkRect::MakeLTRB(0, 0, corpus::kNaN, 4), SkRect::MakeEmpty()};
    for (const SkRect& rect : rects) {
        for (unsigned start = 0; start < 9; ++start) {
            paths.push_back(SkPath::Rect(rect, SkPathDirection::kCW, start));
            paths.push_back(SkPath::Rect(rect, SkPathFillType::kInverseWinding,
                                         SkPathDirection::kCCW, start));
            paths.push_back(SkPath::Oval(rect, SkPathDirection::kCW, start));
            paths.push_back(SkPath::Oval(rect, SkPathDirection::kCCW, start));
            const SkRRect rrect = SkRRect::MakeRectXY(rect, 3, 2);
            paths.push_back(SkPath::RRect(rrect, SkPathDirection::kCW, start));
            paths.push_back(SkPath::RRect(rrect, SkPathDirection::kCCW, start));
        }
        paths.push_back(SkPath::Oval(rect));
        paths.push_back(SkPath::RRect(rect, 4, 6));
        paths.push_back(SkPath::RRect(SkRRect::MakeOval(rect)));
        paths.push_back(SkPath::RRect(SkRRect::MakeRect(rect)));
        SkVector radii[4] = {{1, 2}, {3, 4}, {0, 0}, {5, 1}};
        SkRRect complex{};
        complex.setRectRadii(rect, radii);
        paths.push_back(SkPath::RRect(complex));
    }
    paths.push_back(SkPath::Circle(5, 6, 7));
    paths.push_back(SkPath::Circle(5, 6, 0));
    paths.push_back(SkPath::Circle(5, 6, -1, SkPathDirection::kCCW));
    paths.push_back(SkPath::Line(SkPoint::Make(1, 2), SkPoint::Make(3, 4)));
    paths.push_back(SkPath::Line(SkPoint::Make(1, 2), SkPoint::Make(1, 2)));
    for (int trial = 0; trial < 40; ++trial) {
        std::array<SkPoint, 6> points{};
        for (SkPoint& point : points) {
            point = pick_point(sequence, true);
        }
        const size_t count = sequence.pick(points.size() + 1);
        paths.push_back(SkPath::Polygon(SkSpan<const SkPoint>(points.data(), count),
                                        (trial & 1) != 0, SkPathFillType::kEvenOdd,
                                        (trial & 2) != 0));
    }
    // Raw storage, including invalid verb sequences and mismatched counts.
    const std::array<SkPoint, 5> rawPoints{SkPoint::Make(0, 0), SkPoint::Make(4, 0),
                                           SkPoint::Make(4, 4), SkPoint::Make(0, 4),
                                           SkPoint::Make(2, 9)};
    const std::array<SkPathVerb, 5> rawVerbs{SkPathVerb::kMove, SkPathVerb::kLine,
                                             SkPathVerb::kConic, SkPathVerb::kLine,
                                             SkPathVerb::kClose};
    const std::array<float, 1> rawConics{0.5f};
    paths.push_back(SkPath::Raw(rawPoints, rawVerbs, rawConics, SkPathFillType::kWinding));
    paths.push_back(SkPath::Raw(rawPoints, rawVerbs, {}, SkPathFillType::kWinding));
    const std::array<SkPathVerb, 2> lineFirst{SkPathVerb::kLine, SkPathVerb::kLine};
    paths.push_back(SkPath::Raw(SkSpan<const SkPoint>(rawPoints.data(), 2), lineFirst, {},
                                SkPathFillType::kWinding));
    const std::array<uint8_t, 3> byteVerbs{0, 1, 5};
    paths.push_back(SkPath::Make(SkSpan<const SkPoint>(rawPoints.data(), 2), byteVerbs, {},
                                 SkPathFillType::kEvenOdd, true));
    return paths;
}

void query(Output& output, const SkPath& path, Sequence& sequence) {
    append_path(output, path);
    append_bool(output, path.isEmpty());
    append_bool(output, path.isConvex());
    append_bool(output, path.isInverseFillType());
    append_bool(output, path.isLastContourClosed());
    append(output, path.countPoints());
    append(output, path.countVerbs());
    append_rect(output, path.computeTightBounds());
    append(output, static_cast<uint64_t>(path.approximateBytesUsed() - sizeof(SkPath)));
    SkRect rect = SkRect::MakeWH(-1, -1);
    bool closed = false;
    SkPathDirection direction = SkPathDirection::kCW;
    append_bool(output, path.isRect(&rect, &closed, &direction));
    append_rect(output, rect);
    append_bool(output, closed);
    append(output, static_cast<int32_t>(direction));
    SkRect oval = SkRect::MakeWH(-1, -1);
    append_bool(output, path.isOval(&oval));
    append_rect(output, oval);
    SkRRect rrect{};
    append_bool(output, path.isRRect(&rrect));
    append_rrect(output, rrect);
    SkPoint line[2] = {{0, 0}, {0, 0}};
    append_bool(output, path.isLine(line));
    append_point(output, line[1]);
    const std::optional<SkPoint> last = path.getLastPt();
    append_bool(output, last.has_value());
    SkPoint legacyLast{7, 7};
    append_bool(output, path.getLastPt(&legacyLast));
    append_point(output, legacyLast);
    append_point(output, path.getPoint(static_cast<int>(sequence.next() % 7) - 1));
    std::array<SkPoint, 3> copiedPoints{};
    append(output, static_cast<uint64_t>(path.getPoints(copiedPoints)));
    append_point(output, copiedPoints[2]);
    std::array<uint8_t, 3> copiedVerbs{};
    append(output, static_cast<uint64_t>(path.getVerbs(copiedVerbs)));
    append(output, copiedVerbs[2]);
    for (int sample = 0; sample < 8; ++sample) {
        const SkPoint point = pick_point(sequence, sample == 7);
        append_bool(output, path.contains(point));
        append_bool(output, path.contains(point.fX + 0.5f, point.fY - 0.5f));
    }
    const SkRect probe = SkRect::MakeXYWH(pick_coordinate(sequence, false),
                                          pick_coordinate(sequence, false), 3, 2);
    append_bool(output, path.conservativelyContainsRect(probe));
    append_bool(output, path.conservativelyContainsRect(path.getBounds().makeInset(1, 1)));
}

void iterate(Output& output, const SkPath& path) {
    for (int forceClose = 0; forceClose < 2; ++forceClose) {
        SkPath::Iter iter(path, forceClose != 0);
        for (int step = 0; step < 64; ++step) {
            SkPoint pts[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
            const SkPath::Verb verb = iter.next(pts);
            append(output, static_cast<int32_t>(verb));
            if (verb == SkPath::kDone_Verb) {
                break;
            }
            for (const SkPoint& point : pts) {
                append_point(output, point);
            }
            // isCloseLine() is defined only after a line verb.
            if (verb == SkPath::kLine_Verb) {
                append_bool(output, iter.isCloseLine());
            }
            append_bool(output, iter.isClosedContour());
            if (verb == SkPath::kConic_Verb) {
                append(output, iter.conicWeight());
            }
        }
        SkPath::Iter recordIter(path, forceClose != 0);
        for (int step = 0; step < 64; ++step) {
            const std::optional<SkPath::IterRec> record = recordIter.next();
            append_bool(output, record.has_value());
            if (!record.has_value()) {
                break;
            }
            append(output, static_cast<uint8_t>(record.value().fVerb));
            for (const SkPoint& point : record.value().fPoints) {
                append_point(output, point);
            }
        }
    }
    SkPath::RawIter raw(path);
    for (int step = 0; step < 64; ++step) {
        append(output, static_cast<int32_t>(raw.peek()));
        SkPoint pts[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
        const SkPath::Verb verb = raw.next(pts);
        append(output, static_cast<int32_t>(verb));
        if (verb == SkPath::kDone_Verb) {
            break;
        }
        append_point(output, pts[0]);
        if (verb == SkPath::kConic_Verb) {
            append(output, raw.conicWeight());
        }
    }
    SkPathIter iter = path.iter();
    for (int step = 0; step < 64; ++step) {
        const std::optional<SkPathIter::Rec> record = iter.next();
        append_bool(output, record.has_value());
        if (!record.has_value()) {
            break;
        }
        append(output, static_cast<uint8_t>(record.value().fVerb));
        for (const SkPoint& point : record.value().fPoints) {
            append_point(output, point);
        }
        if (record.value().fVerb == SkPathVerb::kConic) {
            append(output, record.value().conicWeight());
        }
    }
}

void transform(Output& output, const SkPath& path, Sequence& sequence) {
    const std::array<SkMatrix, 5> matrices{
        SkMatrix::Translate(pick_coordinate(sequence, false), 2),
        SkMatrix::Scale(-2, 0.5f),
        SkMatrix::RotateDeg(30),
        SkMatrix::MakeAll(1, 0, 0, 0, 1, 0, 0.01f, 0, 1),
        SkMatrix::Scale(1e30f, 1e30f)};
    for (const SkMatrix& matrix : matrices) {
        const std::optional<SkPath> tried = path.tryMakeTransform(matrix);
        append_bool(output, tried.has_value());
        if (tried.has_value()) {
            append_path(output, tried.value());
        }
        append_path(output, path.makeTransform(matrix));
    }
    append_path(output, path.makeOffset(3, -4));
    append_path(output, path.makeScale(2, 3));
    const std::optional<SkPath> offset = path.tryMakeOffset(1e38f, 0);
    append_bool(output, offset.has_value());
    const std::optional<SkPath> scaled = path.tryMakeScale(0.5f, 0.25f);
    append_bool(output, scaled.has_value());
    append_path(output, path.makeFillType(SkPathFillType::kInverseEvenOdd));
    append_path(output, path.makeToggleInverseFillType());
    append_path(output, path.makeIsVolatile(true));
    SkPath toggled = path;
    toggled.toggleInverseFillType();
    toggled.setIsVolatile(true);
    append_path(output, toggled);
    SkPath swapped = SkPath::Circle(1, 1, 1);
    swapped.swap(toggled);
    append_path(output, swapped);
    append_path(output, toggled);
    toggled.reset();
    append_path(output, toggled);
    append_bool(output, path == swapped);
    append_bool(output, path != path.makeOffset(0, 0));
    append_bool(output, path == path.snapshot());
}

void serialize(Output& output, const SkPath& path) {
    const size_t size = path.writeToMemory(nullptr);
    append(output, static_cast<uint64_t>(size));
    std::vector<uint8_t> memory(size, 0xAB);
    append(output, static_cast<uint64_t>(path.writeToMemory(memory.data())));
    output.bytes.insert(output.bytes.end(), memory.begin(), memory.end());
    size_t bytesRead = 7;
    const std::optional<SkPath> read = SkPath::ReadFromMemory(memory.data(), memory.size(),
                                                              &bytesRead);
    append_bool(output, read.has_value());
    append(output, static_cast<uint64_t>(bytesRead));
    if (read.has_value()) {
        append_path(output, read.value());
        append_bool(output, read.value() == path);
    }
    for (size_t shortLength = 0; shortLength < size && shortLength < 40; shortLength += 3) {
        size_t shortRead = 7;
        const std::optional<SkPath> truncated =
                SkPath::ReadFromMemory(memory.data(), shortLength, &shortRead);
        append_bool(output, truncated.has_value());
        append(output, static_cast<uint64_t>(shortRead));
    }
    const sk_sp<SkData> data = path.serialize();
    append(output, static_cast<uint64_t>((*data).size()));
    ByteSink decimal{};
    path.dump(&decimal, false);
    ByteSink hex{};
    path.dump(&hex, true);
    output.bytes.insert(output.bytes.end(), decimal.bytes().begin(), decimal.bytes().end());
    output.bytes.insert(output.bytes.end(), hex.bytes().begin(), hex.bytes().end());
}

void interpolate(Output& output, const SkPath& a, const SkPath& b) {
    append_bool(output, a.isInterpolatable(b));
    SkPath blended{};
    append_bool(output, a.interpolate(b, 0.25f, &blended));
    append_path(output, blended);
    if (a.isInterpolatable(b)) {
        append_path(output, a.makeInterpolate(b, 0.75f));
    }
}

SkPath builder_from(Sequence& sequence) {
    SkPathBuilder builder{};
    const int operations = 1 + static_cast<int>(sequence.next() % 6);
    for (int i = 0; i < operations; ++i) {
        random_operation(builder, sequence, false);
    }
    const SkPath path = builder.detach();
    return path;
}

void builder_corpus(Output& output, Sequence& sequence) {
    for (int trial = 0; trial < 2500; ++trial) {
        const bool allowSpecial = (trial % 5) == 4;
        SkPathBuilder builder((trial & 1) ? SkPathFillType::kEvenOdd : SkPathFillType::kWinding);
        const int operations = 1 + static_cast<int>(sequence.next() % 12);
        for (int i = 0; i < operations; ++i) {
            random_operation(builder, sequence, allowSpecial);
        }
        append_builder(output, builder);
        const SkPath snapshot = builder.snapshot();
        query(output, snapshot, sequence);
        iterate(output, snapshot);
        if (trial % 4 == 0) {
            transform(output, snapshot, sequence);
            serialize(output, snapshot);
        }
        const SkMatrix matrix = SkMatrix::Scale(2, -1);
        append_path(output, builder.snapshot(&matrix));

        SkPathBuilder copy = builder;
        append_bool(output, copy == builder);
        copy.lineTo(1, 1);
        append_bool(output, copy != builder);
        SkPathBuilder moved = std::move(copy);
        append_builder(output, moved);
        SkPathBuilder fromPath(snapshot);
        append_builder(output, fromPath);
        SkPathBuilder assigned{};
        assigned = snapshot;
        append_bool(output, assigned == fromPath);

        // Appending a path in every mode, including the builder's own snapshot.
        SkPathBuilder appended = builder;
        appended.addPath(snapshot, 1.5f, -2.0f, SkPath::kAppend_AddPathMode);
        appended.addPath(snapshot, SkPath::kExtend_AddPathMode);
        appended.addPath(snapshot, SkMatrix::RotateDeg(90), SkPath::kAppend_AddPathMode);
        appended.addPath(appended.snapshot(), SkMatrix::Scale(0.5f, 0.5f),
                         SkPath::kExtend_AddPathMode);
        append_builder(output, appended);
        appended.addRaw(SkPathPriv::Raw(snapshot, SkResolveConvexity::kNo).value_or(
                                SkPathRaw::Empty()),
                        SkPathBuilder::Reserve::kExact);
        append_builder(output, appended);

        const SkPath detached = moved.detach();
        append_path(output, detached);
        append_builder(output, moved);
        const SkPath other = builder_from(sequence);
        interpolate(output, detached, other);
        interpolate(output, detached, detached.makeOffset(2, 3));
        output.cases += 1;
    }
}

void degenerate_corpus(Output& output, Sequence& sequence) {
    for (int trial = 0; trial < 2000; ++trial) {
        const SkPoint a = pick_point(sequence, true);
        SkPoint b = pick_point(sequence, true);
        SkPoint c = pick_point(sequence, true);
        const SkPoint d = pick_point(sequence, true);
        if (trial % 3 == 0) {
            b = a;
        }
        if (trial % 5 == 0) {
            c = SkPoint::Make(a.fX + 1e-6f, a.fY);
        }
        for (int exact = 0; exact < 2; ++exact) {
            append_bool(output, SkPath::IsLineDegenerate(a, b, exact != 0));
            append_bool(output, SkPath::IsQuadDegenerate(a, b, c, exact != 0));
            append_bool(output, SkPath::IsCubicDegenerate(a, b, c, d, exact != 0));
        }
        for (int pow2 = 0; pow2 < 4; ++pow2) {
            SkPoint quads[1 + 2 * 8] = {};
            const int count = SkPath::ConvertConicToQuads(a, b, c, pick_weight(sequence), quads,
                                                          pow2);
            append(output, count);
            for (const SkPoint& point : quads) {
                append_point(output, point);
            }
        }
        output.cases += 1;
    }
}

#ifndef STX_SKIA_REFERENCE
template <typename T>
concept TemporaryPathPoints = requires(T& value) { std::move(value).points(); };
template <typename T>
concept TemporaryPathBounds = requires(T& value) { std::move(value).getBounds(); };
template <typename T>
concept TemporaryBuilderVerbs = requires(T& value) { std::move(value).verbs(); };
template <typename T>
concept TemporaryIteration = requires(T& value) { SkPath::Iter(std::move(value), false); };
static_assert(!TemporaryPathPoints<SkPath>);
static_assert(!TemporaryPathBounds<SkPath>);
static_assert(!TemporaryBuilderVerbs<SkPathBuilder>);
static_assert(!TemporaryIteration<SkPath>);
static_assert(!std::is_copy_constructible_v<SkPathRawShapes::Rect>);
static_assert(!std::is_copy_constructible_v<SkPathRawShapes::Oval>);
static_assert(!std::is_copy_constructible_v<SkPathRawShapes::RRect>);

bool corrected_behavior() {
    // A misaligned serialized buffer is rejected instead of read through misaligned points.
    const std::array<SkPoint, 3> triangle{SkPoint::Make(0, 0), SkPoint::Make(4, 0),
                                          SkPoint::Make(0, 3)};
    const SkPath path = SkPath::Polygon(triangle, true);
    const size_t size = path.writeToMemory(nullptr);
    std::vector<uint8_t> storage(size + 1, 0);
    path.writeToMemory(storage.data() + 1);
    size_t bytesRead = 99;
    const std::optional<SkPath> misaligned =
            SkPath::ReadFromMemory(storage.data() + 1, size, &bytesRead);
    if (misaligned.has_value() || bytesRead != 0) return false;
    std::vector<uint8_t> aligned(size, 0);
    path.writeToMemory(aligned.data());
    const std::optional<SkPath> read = SkPath::ReadFromMemory(aligned.data(), size, &bytesRead);
    if (!read.has_value() || read.value() != path || bytesRead != size) return false;

    // Reversal of every contour form stays within the path's storage.
    SkPathBuilder reversed{};
    SkPathBuilder source{};
    source.moveTo(0, 0).lineTo(1, 0).conicTo(2, 0, 2, 1, 0.5f).close();
    source.moveTo(5, 5).cubicTo(6, 5, 6, 6, 5, 6);
    source.moveTo(9, 9);
    const SkPath sourcePath = source.detach();
    SkPathPriv::ReverseAddPath(&reversed, sourcePath);
    SkPathPriv::ReversePathTo(&reversed, sourcePath);
    if (reversed.countPoints() == 0) return false;
    return true;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
#ifndef STX_SKIA_REFERENCE
    if (!corrected_behavior()) {
        std::printf("path construction: corrected behavior FAIL\n");
        return 1;
    }
#endif
    Output output{};
    Sequence sequence{};
    const std::vector<SkPath> factories = factory_paths(sequence);
    for (const SkPath& path : factories) {
        query(output, path, sequence);
        iterate(output, path);
        transform(output, path, sequence);
        serialize(output, path);
        output.cases += 1;
    }
    builder_corpus(output, sequence);
    degenerate_corpus(output, sequence);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("path construction: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
