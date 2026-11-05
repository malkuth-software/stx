// Differential corpus for points, rectangles and rounded rectangles.
// The shared corpus avoids inputs whose original behavior was undefined; the
// candidate-only checks cover the corrected saturation and overflow cases.

#include "include/core/SkMatrix.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "src/core/SkBuffer.h"
#include "src/core/SkPointPriv.h"
#include "src/core/SkRRectPriv.h"
#include "src/core/SkRectPriv.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {

struct Output {
    std::vector<uint8_t> bytes{};
    size_t cases = 0;
};

template <typename T>
void append(Output& output, const T& value) {
    uint8_t raw[sizeof(T)] = {};
    std::memcpy(raw, &value, sizeof(T));
    output.bytes.insert(output.bytes.end(), raw, raw + sizeof(T));
}

void append_bool(Output& output, bool value) {
    const uint8_t byte = value ? 1 : 0;
    output.bytes.push_back(byte);
}

void append_string(Output& output, const SkString& text) {
    const uint8_t* begin = reinterpret_cast<const uint8_t*>(text.c_str());
    append(output, static_cast<uint64_t>(text.size()));
    output.bytes.insert(output.bytes.end(), begin, begin + text.size());
}

// Deterministic 64-bit linear congruential sequence; the same in both builds.
struct Sequence {
    uint64_t state = 0x9E3779B97F4A7C15ull;

    uint32_t next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const uint32_t value = static_cast<uint32_t>(state >> 32);
        return value;
    }

    size_t pick(size_t count) {
        const size_t index = static_cast<size_t>(next()) % count;
        return index;
    }
};

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kMax = std::numeric_limits<float>::max();
constexpr float kMin = std::numeric_limits<float>::min();
constexpr float kDenormal = std::numeric_limits<float>::denorm_min();

const std::array<float, 30> kFloats{
    0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 1.5f, 2.5f, -2.5f, 3.0f,
    7.25f, 100.0f, -100.0f, 0.49999997f, 1e-7f, 1e-30f, kDenormal, kMin, 1e20f, -1e20f,
    3e38f, kMax, -kMax, kInf, -kInf, kNaN, 16777216.0f, 16777217.0f, 2147483648.0f, -2147483904.0f,
};

// Coordinates whose pairwise differences fit in int32_t.
const std::array<int32_t, 13> kSafeInts{
    -(1 << 29), -100000, -1000, -7, -1, 0, 1, 2, 7, 1000, 100000, (1 << 29) - 1, 1 << 29,
};

// Every coordinate class, including the representable extremes.
const std::array<int32_t, 17> kInts{
    std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min() + 1,
    -(1 << 29), -100000, -1000, -7, -1, 0, 1, 2, 7, 1000, 100000, 1 << 29,
    std::numeric_limits<int32_t>::max() - 1, std::numeric_limits<int32_t>::max(), 3,
};

float pick_float(Sequence& sequence) {
    const float value = kFloats[sequence.pick(kFloats.size())];
    return value;
}

// Mostly finite ordinary values, so that sorted non-empty shapes are common.
float pick_ordinary(Sequence& sequence) {
    const uint32_t choice = sequence.next() % 8;
    if (choice == 0) {
        const float special = pick_float(sequence);
        return special;
    }
    const int32_t whole = static_cast<int32_t>(sequence.next() % 401) - 200;
    const float fraction = static_cast<float>(sequence.next() % 8) * 0.125f;
    const float value = static_cast<float>(whole) + fraction;
    return value;
}

SkRect pick_rect(Sequence& sequence) {
    const SkRect rect = SkRect::MakeLTRB(pick_ordinary(sequence), pick_ordinary(sequence),
                                         pick_ordinary(sequence), pick_ordinary(sequence));
    return rect;
}

SkIRect pick_irect(Sequence& sequence, bool safe) {
    SkIRect rect{};
    if (safe) {
        rect = SkIRect::MakeLTRB(kSafeInts[sequence.pick(kSafeInts.size())],
                                 kSafeInts[sequence.pick(kSafeInts.size())],
                                 kSafeInts[sequence.pick(kSafeInts.size())],
                                 kSafeInts[sequence.pick(kSafeInts.size())]);
        return rect;
    }
    rect = SkIRect::MakeLTRB(kInts[sequence.pick(kInts.size())], kInts[sequence.pick(kInts.size())],
                             kInts[sequence.pick(kInts.size())], kInts[sequence.pick(kInts.size())]);
    return rect;
}

void append_irect(Output& output, const SkIRect& rect) {
    append(output, rect.fLeft);
    append(output, rect.fTop);
    append(output, rect.fRight);
    append(output, rect.fBottom);
}

void append_rect(Output& output, const SkRect& rect) {
    append(output, rect.fLeft);
    append(output, rect.fTop);
    append(output, rect.fRight);
    append(output, rect.fBottom);
}

void append_point(Output& output, const SkPoint& point) {
    append(output, point.fX);
    append(output, point.fY);
}

void append_rrect(Output& output, const SkRRect& rrect) {
    append_rect(output, rrect.rect());
    for (const SkVector& radius : rrect.radii()) {
        append_point(output, radius);
    }
    append(output, static_cast<int32_t>(rrect.getType()));
    append_bool(output, rrect.isValid());
}

void irect_corpus(Output& output) {
    Sequence sequence{};
    for (int trial = 0; trial < 4000; ++trial) {
        const SkIRect a = pick_irect(sequence, false);
        const SkIRect b = pick_irect(sequence, false);
        const int32_t dx = kInts[sequence.pick(kInts.size())];
        const int32_t dy = kInts[sequence.pick(kInts.size())];
        append_irect(output, a);
        append_bool(output, a.isEmpty());
        append_bool(output, a.isEmpty64());
        append(output, a.width64());
        append(output, a.height64());
        append(output, a.width());
        append(output, a.height());
        append(output, a.size().width());
        append(output, a.topLeft().fX);
        append_irect(output, a.makeSorted());
        SkIRect sorted = a;
        sorted.sort();
        append_irect(output, sorted);
        append_irect(output, a.makeOffset(dx, dy));
        append_irect(output, a.makeInset(dx, dy));
        append_irect(output, a.makeOutset(dx, dy));
        SkIRect moved = a;
        moved.offset(dx, dy);
        append_irect(output, moved);
        SkIRect placed = a;
        placed.offsetTo(dx, dy);
        append_irect(output, placed);
        SkIRect inner = a;
        inner.inset(dx, dy);
        append_irect(output, inner);
        if (dx != std::numeric_limits<int32_t>::min() &&
            dy != std::numeric_limits<int32_t>::min()) {
            SkIRect outer = a;
            outer.outset(dx, dy);
            append_irect(output, outer);
        }
        SkIRect adjusted = a;
        adjusted.adjust(dx, dy, dy, dx);
        append_irect(output, adjusted);
        append_bool(output, a.contains(dx, dy));
        append_irect(output, SkIRect::MakeXYWH(dx, dy, a.fRight, a.fBottom));

        SkIRect overlap = a;
        const bool intersects = overlap.intersect(b);
        append_bool(output, intersects);
        append_irect(output, overlap);
        append_bool(output, SkIRect::Intersects(a, b));
        SkIRect joined = a;
        joined.join(b);
        append_irect(output, joined);
        append_bool(output, a.contains(b));
        append_bool(output, a == b);
        append_bool(output, a != b);
        if (a.fLeft < a.fRight && a.fTop < a.fBottom && b.fLeft < b.fRight &&
            b.fTop < b.fBottom) {
            append_bool(output, a.containsNoEmptyCheck(b));
        }
        output.cases += 1;
    }
    for (int trial = 0; trial < 4000; ++trial) {
        const bool safe = (trial % 2) == 0;
        const SkIRect a = pick_irect(sequence, safe);
        const SkIRect b = pick_irect(sequence, safe);
        SkIRect difference{};
        const bool exact = SkRectPriv::Subtract(a, b, &difference);
        append_bool(output, exact);
        append_irect(output, difference);
        output.cases += 1;
    }
}

void rect_corpus(Output& output) {
    Sequence sequence{};
    for (int trial = 0; trial < 6000; ++trial) {
        const SkRect a = pick_rect(sequence);
        const SkRect b = pick_rect(sequence);
        const float dx = pick_float(sequence);
        const float dy = pick_ordinary(sequence);
        append_rect(output, a);
        append_bool(output, a.isEmpty());
        append_bool(output, a.isSorted());
        append_bool(output, a.isFinite());
        append(output, a.width());
        append(output, a.height());
        append(output, a.centerX());
        append(output, a.centerY());
        append_point(output, a.center());
        append_point(output, a.TL());
        append_point(output, a.BR());
        const std::array<SkPoint, 4> clockwise = a.toQuad(SkPathDirection::kCW);
        const std::array<SkPoint, 4> counter = a.toQuad(SkPathDirection::kCCW);
        for (int i = 0; i < 4; ++i) {
            append_point(output, clockwise[i]);
            append_point(output, counter[i]);
        }
        SkPoint legacy[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
        a.toQuad(legacy);
        append_point(output, legacy[3]);
        append_rect(output, a.makeSorted());
        SkRect sorted = a;
        sorted.sort();
        append_rect(output, sorted);
        append_rect(output, a.makeOffset(dx, dy));
        append_rect(output, a.makeInset(dx, dy));
        append_rect(output, a.makeOutset(dx, dy));
        SkRect moved = a;
        moved.offset(dx, dy);
        append_rect(output, moved);
        SkRect placed = a;
        placed.offsetTo(dx, dy);
        append_rect(output, placed);
        SkRect inner = a;
        inner.inset(dx, dy);
        append_rect(output, inner);
        SkRect outer = a;
        outer.outset(dx, dy);
        append_rect(output, outer);
        append_irect(output, a.round());
        append_irect(output, a.roundOut());
        append_irect(output, a.roundIn());
        SkRect roundedOut{};
        a.roundOut(&roundedOut);
        append_rect(output, roundedOut);
        append_bool(output, a.contains(dx, dy));
        SkRect between{};
        between.set(SkPoint::Make(dx, dy), SkPoint::Make(a.fRight, a.fTop));
        append_rect(output, between);
        SkRect sized{};
        sized.setXYWH(dx, dy, a.fRight, a.fBottom);
        append_rect(output, sized);
        append_rect(output, SkRect::MakeXYWH(dx, dy, a.fLeft, a.fTop));
        append_bool(output, SkRectPriv::FitsInFixed(a));
        append(output, SkRectPriv::HalfWidth(a));
        append(output, SkRectPriv::HalfHeight(a));

        SkRect overlap = a;
        const bool intersects = overlap.intersect(b);
        append_bool(output, intersects);
        append_rect(output, overlap);
        SkRect pairOverlap = SkRect::MakeEmpty();
        const bool pairIntersects = pairOverlap.intersect(a, b);
        append_bool(output, pairIntersects);
        append_rect(output, pairOverlap);
        append_bool(output, a.intersects(b));
        append_bool(output, SkRect::Intersects(a, b));
        SkRect joined = a;
        joined.join(b);
        append_rect(output, joined);
        if (!b.isEmpty()) {
            SkRect nonEmptyJoin = a;
            nonEmptyJoin.joinNonEmptyArg(b);
            append_rect(output, nonEmptyJoin);
        }
        SkRect possiblyEmptyJoin = a;
        possiblyEmptyJoin.joinPossiblyEmptyRect(b);
        append_rect(output, possiblyEmptyJoin);
        append_bool(output, a.contains(b));
        const SkIRect rounded = b.round();
        append_bool(output, a.contains(rounded));
        append_bool(output, rounded.contains(a));
        append_bool(output, a == b);
        append_bool(output, a != b);
        output.cases += 1;
    }
    for (int trial = 0; trial < 3000; ++trial) {
        const size_t count = sequence.pick(10);
        std::array<SkPoint, 9> storage{};
        for (size_t i = 0; i < count; ++i) {
            storage[i] = SkPoint::Make(pick_ordinary(sequence), pick_ordinary(sequence));
        }
        const SkSpan<const SkPoint> points(storage.data(), count);
        const std::optional<SkRect> bounds = SkRect::Bounds(points);
        append_bool(output, bounds.has_value());
        if (bounds.has_value()) {
            append_rect(output, bounds.value());
        }
        SkRect checked{};
        append_bool(output, checked.setBoundsCheck(points));
        append_rect(output, checked);
        SkRect unchecked{};
        unchecked.setBoundsNoCheck(points);
        append_rect(output, unchecked);
        append_rect(output, SkRect::BoundsOrEmpty(points));
        output.cases += 1;
    }
    for (int trial = 0; trial < 64; ++trial) {
        const SkRect rect = pick_rect(sequence);
        append_string(output, rect.dumpToString(false));
        append_string(output, rect.dumpToString(true));
        output.cases += 1;
    }
    append_rect(output, SkRectPriv::MakeLargeS32());
}

void point_corpus(Output& output) {
    Sequence sequence{};
    for (const float x : kFloats) {
        for (const float y : kFloats) {
            const SkPoint p = SkPoint::Make(x, y);
            const SkPoint q = SkPoint::Make(pick_float(sequence), pick_ordinary(sequence));
            const SkPoint r = SkPoint::Make(pick_ordinary(sequence), pick_float(sequence));
            const float length = pick_float(sequence);
            append_point(output, p);
            append(output, SkPoint::Length(x, y));
            append(output, p.length());
            append(output, p.distanceToOrigin());
            SkPoint normalized = p;
            append_bool(output, normalized.normalize());
            append_point(output, normalized);
            SkPoint resized = p;
            append_bool(output, resized.setLength(length));
            append_point(output, resized);
            SkPoint fromParts{0, 0};
            append_bool(output, fromParts.setLength(q.fX, q.fY, length));
            append_point(output, fromParts);
            SkPoint unit{0, 0};
            append_bool(output, unit.setNormalize(x, y));
            append_point(output, unit);
            SkPoint measured = p;
            append(output, SkPoint::Normalize(&measured));
            append_point(output, measured);
            SkPoint scaled{0, 0};
            p.scale(length, &scaled);
            append_point(output, scaled);
            SkPoint scaledInPlace = p;
            scaledInPlace.scale(length);
            append_point(output, scaledInPlace);
            SkPoint negated = p;
            negated.negate();
            append_point(output, negated);
            append_point(output, -p);
            append_point(output, p * length);
            SkPoint product = p;
            product *= length;
            append_point(output, product);
            append_point(output, p + q);
            append_point(output, p - q);
            append_bool(output, p.isFinite());
            append_bool(output, p.isZero());
            append_bool(output, p.equals(q.fX, q.fY));
            append_bool(output, p == q);
            append_bool(output, p != q);
            append(output, SkPoint::Distance(p, q));
            append(output, SkPoint::DotProduct(p, q));
            append(output, SkPoint::CrossProduct(p, q));
            append(output, p.dot(q));
            append(output, p.cross(q));
            append_bool(output, SkPointPriv::CanNormalize(x, y));
            append_bool(output, SkPointPriv::EqualsWithinTolerance(p, q));
            append_bool(output, SkPointPriv::EqualsWithinTolerance(p, q, length));
            append(output, SkPointPriv::DistanceToSqd(p, q));
            append(output, SkPointPriv::LengthSqd(p));
            append(output, SkPointPriv::DistanceToLineSegmentBetweenSqd(p, q, r));
            append(output, SkPointPriv::DistanceToLineSegmentBetween(r, p, q));
            SkPoint clockwise{0, 0};
            SkPointPriv::RotateCW(p, &clockwise);
            append_point(output, clockwise);
            SkPoint counter = p;
            SkPointPriv::RotateCCW(&counter);
            append_point(output, counter);
            SkPoint fast = p;
            append_bool(output, SkPointPriv::SetLengthFast(&fast, length));
            append_point(output, fast);
            const SkPoint triple[3] = {p, q, r};
            append_bool(output, SkPointPriv::AreFinite(triple, 3));
            append_bool(output, SkPointPriv::AreFinite(triple, 1));
            SkPoint offsets[2] = {p, q};
            SkPoint::Offset(offsets, 2, r);
            append_point(output, offsets[1]);
            output.cases += 1;
        }
    }
    for (const int32_t x : kSafeInts) {
        for (const int32_t y : kInts) {
            const SkIPoint p = SkIPoint::Make(x, y);
            const SkIPoint q = SkIPoint::Make(y, kInts[sequence.pick(kInts.size())]);
            const SkIPoint sum = p + q;
            const SkIPoint difference = p - q;
            SkIPoint accumulated = p;
            accumulated += q;
            SkIPoint reduced = p;
            reduced -= q;
            append(output, sum.fX);
            append(output, sum.fY);
            append(output, difference.fX);
            append(output, difference.fY);
            append(output, accumulated.fY);
            append(output, reduced.fY);
            if (y != std::numeric_limits<int32_t>::min()) {
                const SkIPoint negated = -p;
                append(output, negated.fX);
                append(output, negated.fY);
            }
            append_bool(output, p.isZero());
            append_bool(output, p == q);
            append_bool(output, p != q);
            append_bool(output, p.equals(y, x));
            SkPoint converted{0, 0};
            converted.iset(p);
            append_point(output, converted);
            output.cases += 1;
        }
    }
}

std::vector<SkMatrix> transforms() {
    std::vector<SkMatrix> matrices{};
    matrices.push_back(SkMatrix::I());
    matrices.push_back(SkMatrix::Translate(3.5f, -2.25f));
    matrices.push_back(SkMatrix::Scale(2, 3));
    matrices.push_back(SkMatrix::Scale(-1, 1));
    matrices.push_back(SkMatrix::Scale(1, -1));
    matrices.push_back(SkMatrix::Scale(-2, -0.5f));
    matrices.push_back(SkMatrix::Scale(0, 1));
    matrices.push_back(SkMatrix::Scale(1e30f, 1e30f));
    matrices.push_back(SkMatrix::RotateDeg(90));
    matrices.push_back(SkMatrix::RotateDeg(270));
    matrices.push_back(SkMatrix::RotateDeg(180));
    matrices.push_back(SkMatrix::RotateDeg(30));
    SkMatrix skew = SkMatrix::I();
    skew.setSkew(0.25f, 0);
    matrices.push_back(skew);
    SkMatrix flipped = SkMatrix::RotateDeg(90);
    flipped.postScale(-1.5f, 2);
    flipped.postTranslate(7, -9);
    matrices.push_back(flipped);
    return matrices;
}

void rrect_corpus(Output& output) {
    Sequence sequence{};
    const std::vector<SkMatrix> matrices = transforms();
    for (int trial = 0; trial < 6000; ++trial) {
        const SkRect rect = pick_rect(sequence);
        SkRRect rrect{};
        const uint32_t shape = sequence.next() % 5;
        if (shape == 0) {
            rrect.setRectXY(rect, pick_ordinary(sequence), pick_ordinary(sequence));
        } else if (shape == 1) {
            rrect.setNinePatch(rect, pick_ordinary(sequence), pick_ordinary(sequence),
                               pick_ordinary(sequence), pick_ordinary(sequence));
        } else if (shape == 2) {
            SkVector radii[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
            for (int i = 0; i < 4; ++i) {
                radii[i] = SkVector::Make(pick_ordinary(sequence), pick_ordinary(sequence));
            }
            rrect.setRectRadii(rect, radii);
        } else if (shape == 3) {
            rrect.setOval(rect);
        } else {
            rrect = SkRRect::MakeRect(rect);
        }
        append_rrect(output, rrect);
        append(output, rrect.width());
        append(output, rrect.height());
        append_point(output, rrect.getSimpleRadii());
        for (int sample = 0; sample < 6; ++sample) {
            const SkPoint point = SkPoint::Make(pick_ordinary(sequence), pick_ordinary(sequence));
            append_bool(output, rrect.contains(point));
        }
        const SkRect probe = pick_rect(sequence).makeSorted();
        append_bool(output, rrect.contains(probe));
        const SkRect& bounds = rrect.getBounds();
        append_bool(output, rrect.contains(bounds.makeInset(0.25f, 0.25f)));
        append_bool(output, rrect.contains(bounds.center()));

        const float dx = pick_ordinary(sequence);
        const float dy = pick_ordinary(sequence);
        SkRRect inner{};
        rrect.inset(dx, dy, &inner);
        append_rrect(output, inner);
        SkRRect outer{};
        rrect.outset(dx, dy, &outer);
        append_rrect(output, outer);
        SkRRect inPlace = rrect;
        inPlace.inset(dx, dy);
        append_rrect(output, inPlace);
        SkRRect outPlace = rrect;
        outPlace.outset(dx, dy);
        append_rrect(output, outPlace);
        append_rrect(output, rrect.makeOffset(dx, dy));
        SkRRect moved = rrect;
        moved.offset(dx, dy);
        append_rrect(output, moved);

        const SkMatrix& matrix = matrices[sequence.pick(matrices.size())];
        const std::optional<SkRRect> mapped = rrect.transform(matrix);
        append_bool(output, mapped.has_value());
        if (mapped.has_value()) {
            append_rrect(output, mapped.value());
        }
        SkRRect mappedOut{};
        append_bool(output, rrect.transform(matrix, &mappedOut));
        append_rrect(output, mappedOut);

        uint8_t memory[SkRRect::kSizeInMemory] = {};
        append(output, static_cast<uint64_t>(rrect.writeToMemory(memory)));
        output.bytes.insert(output.bytes.end(), memory, memory + SkRRect::kSizeInMemory);
        SkRRect reread{};
        append(output, static_cast<uint64_t>(reread.readFromMemory(memory, sizeof(memory))));
        append_rrect(output, reread);
        append_bool(output, reread == rrect);
        append_bool(output, reread != rrect);
        append(output, static_cast<uint64_t>(reread.readFromMemory(memory, sizeof(memory) - 1)));

        uint8_t serialized[SkRRect::kSizeInMemory] = {};
        SkWBuffer writer(serialized, sizeof(serialized));
        SkRRectPriv::WriteToBuffer(rrect, &writer);
        append(output, static_cast<uint64_t>(writer.pos()));
        SkRBuffer reader(serialized, sizeof(serialized));
        SkRRect fromBuffer{};
        append_bool(output, SkRRectPriv::ReadFromBuffer(&reader, &fromBuffer));
        append_rrect(output, fromBuffer);
        SkRBuffer shortReader(serialized, sizeof(serialized) - 4);
        SkRRect untouched = SkRRect::MakeOval(SkRect::MakeWH(4, 2));
        append_bool(output, SkRRectPriv::ReadFromBuffer(&shortReader, &untouched));
        append_rrect(output, untouched);

        // Arbitrary serialized words, including non-finite and unsorted rectangles.
        float words[12] = {};
        for (int i = 0; i < 12; ++i) {
            words[i] = (i % 3 == 0) ? pick_float(sequence) : pick_ordinary(sequence);
        }
        SkRRect parsed{};
        append(output, static_cast<uint64_t>(parsed.readFromMemory(words, sizeof(words))));
        append_rrect(output, parsed);
        output.cases += 1;
    }
    Sequence dumps{};
    for (int trial = 0; trial < 32; ++trial) {
        const SkRRect rrect = SkRRect::MakeRectXY(pick_rect(dumps), pick_ordinary(dumps),
                                                  pick_ordinary(dumps));
        append_string(output, rrect.dumpToString(false));
        append_string(output, rrect.dumpToString(true));
        output.cases += 1;
    }
}

uint64_t fnv1a(const std::vector<uint8_t>& bytes) {
    uint64_t hash = 14695981039346656037ull;
    for (const uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

#ifndef STX_SKIA_REFERENCE
template <typename T>
concept TemporaryRRectRect = requires(T& value) { std::move(value).rect(); };
template <typename T>
concept TemporaryRRectBounds = requires(T& value) { std::move(value).getBounds(); };
template <typename T>
concept TemporaryRRectRadii = requires(T& value) { std::move(value).radii(); };
template <typename T>
concept HasRectFan = requires { &T::SetRectFan; };
template <typename T>
concept HasQuadContainment = requires { &T::QuadContainsRectMask; };
template <typename T>
concept HasConservativeIntersect = requires { &T::ConservativeIntersect; };
static_assert(!TemporaryRRectRect<SkRRect>);
static_assert(!TemporaryRRectBounds<SkRRect>);
static_assert(!TemporaryRRectRadii<SkRRect>);
static_assert(!HasRectFan<SkPointPriv>);
static_assert(!HasQuadContainment<SkRectPriv>);
static_assert(!HasConservativeIntersect<SkRRectPriv>);

bool corrected_behavior() {
    const int32_t minimum = std::numeric_limits<int32_t>::min();
    const int32_t maximum = std::numeric_limits<int32_t>::max();

    // Outsetting by the most negative offset saturates instead of negating it.
    SkIRect outer = SkIRect::MakeLTRB(0, 0, 10, 10);
    outer.outset(minimum, minimum);
    if (outer != SkIRect::MakeLTRB(maximum, maximum, minimum + 10, minimum + 10)) return false;
    if (outer != SkIRect::MakeLTRB(0, 0, 10, 10).makeOutset(minimum, minimum)) return false;

    // Integer point negation saturates.
    const SkIPoint negated = -SkIPoint::Make(minimum, 5);
    if (negated.fX != maximum || negated.fY != -5) return false;

    // The result may alias an input.
    SkIRect aliased = SkIRect::MakeLTRB(0, 0, 10, 10);
    if (SkRectPriv::Subtract(aliased, SkIRect::MakeLTRB(2, 2, 4, 4), &aliased)) return false;
    if (SkIRect::Intersects(aliased, SkIRect::MakeLTRB(2, 2, 4, 4))) return false;

    // An empty point list reads no points.
    if (!SkPointPriv::AreFinite(nullptr, 0)) return false;
    return true;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
#ifndef STX_SKIA_REFERENCE
    if (!corrected_behavior()) {
        std::printf("geometry primitives: corrected behavior FAIL\n");
        return 1;
    }
#endif
    Output output{};
    irect_corpus(output);
    rect_corpus(output);
    point_corpus(output);
    rrect_corpus(output);
    if (argc == 2) {
        FILE* file = std::fopen(argv[1], "wb");
        if (file == nullptr) return 2;
        const size_t written = std::fwrite(output.bytes.data(), 1, output.bytes.size(), file);
        const int closed = std::fclose(file);
        if (written != output.bytes.size() || closed != 0) return 2;
    }
    const unsigned long long hash = fnv1a(output.bytes);
    std::printf("geometry primitives: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
