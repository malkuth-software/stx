/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPathTypes.h"
#include "include/private/SkAssert.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPathRaw.h"

// The segment-mask bit contributed by each verb; moves and closes contribute none.
static unsigned verb_segment_mask(SkPathVerb verb) {
    switch (verb) {
        case SkPathVerb::kMove:  return 0;
        case SkPathVerb::kLine:  return kLine_SkPathSegmentMask;
        case SkPathVerb::kQuad:  return kQuad_SkPathSegmentMask;
        case SkPathVerb::kConic: return kConic_SkPathSegmentMask;
        case SkPathVerb::kCubic: return kCubic_SkPathSegmentMask;
        case SkPathVerb::kClose: return 0;
    }
    SkASSERT(false);
    return 0;
}

uint8_t SkPathPriv::ComputeSegmentMask(SkSpan<const SkPathVerb> verbs) {
    unsigned mask = 0;
    for (const SkPathVerb v : verbs) {
        mask |= verb_segment_mask(v);
    }
    const uint8_t segmentMask = SkTo<uint8_t>(mask);
    return segmentMask;
}

std::optional<SkRect> SkPathRaw::isRect() const {
    const std::optional<SkPathPriv::RectContour> rc =
            SkPathPriv::IsRectContour(fPoints, fVerbs, fSegmentMask, false);
    if (!rc.has_value()) {
        return {};
    }
    return rc.value().fRect;
}
