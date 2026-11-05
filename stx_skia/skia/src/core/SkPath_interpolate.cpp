/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPath.h"
#include "include/core/SkSpan.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace {

template <typename T> bool span_equal(SkSpan<T> a, SkSpan<T> b) {
    if (a.size() != b.size()) {
        return false;
    }
    if (a.empty()) {
        return true;
    }
    const bool equal = (a.data() == b.data()) || std::equal(a.begin(), a.end(), b.begin());
    return equal;
}

// from + t * (to - from), per coordinate.
SkPoint lerp(SkPoint from, SkPoint to, float t) {
    const SkVector delta = to - from;
    const SkPoint mixed{from.fX + delta.fX * t, from.fY + delta.fY * t};
    return mixed;
}

// These can be copy/pasted into Chrome / Android
// - CanInterpolate()
// - Interpolate()
//
// That would allow us to remove the associated SkPath methods.
//
// Note: they require no private access to SkPath internals, and are not used/called by
//       anthing in Skia.

bool CanInterpolate(const SkPath& from, const SkPath& to) {
    const bool compatible = from.points().size() == to.points().size() &&
                            span_equal(from.verbs(), to.verbs()) &&
                            span_equal(from.conicWeights(), to.conicWeights());
    return compatible;
}

std::optional<SkPath> Interpolate(const SkPath& from, const SkPath& to, float t) {
    if (!CanInterpolate(from, to)) {
        return {};
    }

    // note, if from and to have different fill-types, that's up to the client to deal with.
    const SkPathFillType fillType = from.getFillType();

    const SkSpan<const SkPoint> fromPts = from.points();
    const SkSpan<const SkPoint> toPts = to.points();

    std::vector<SkPoint> dst(fromPts.size());

    for (size_t i = 0; i < fromPts.size(); ++i) {
        dst[i] = lerp(fromPts[i], toPts[i], t);
    }
    const SkPath path = SkPath::Raw(SkSpan<const SkPoint>(dst.data(), dst.size()), from.verbs(),
                                    from.conicWeights(), fillType);
    return path;
}

} // namespace

///////////////////////////////////////////////////////////////////////////////////////////////

bool SkPath::isInterpolatable(const SkPath& compare) const {
    const bool compatible = CanInterpolate(*this, compare);
    return compatible;
}

SkPath SkPath::makeInterpolate(const SkPath& ending, SkScalar weight) const {
    const std::optional<SkPath> result = Interpolate(*this, ending, 1 - weight);
    const SkPath path = result.value_or(SkPath());
    return path;
}

bool SkPath::interpolate(const SkPath& ending, SkScalar weight, SkPath* out) const {
    const std::optional<SkPath> result = Interpolate(*this, ending, 1 - weight);
    if (!result.has_value()) {
        return false;
    }
    *out = result.value();
    return true;
}
