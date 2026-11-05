/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkPathIter_DEFINED
#define SkPathIter_DEFINED

#include "include/core/SkPathTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkSpan.h"

#include <array>
#include <cstddef>
#include <optional>

class SK_API SkPathIter {
public:
    struct Rec {
        SkSpan<const SkPoint> fPoints;
        float                 fConicWeight;
        SkPathVerb            fVerb;

        float conicWeight() const {
            SkASSERT(fVerb == SkPathVerb::kConic);
            return fConicWeight;
        }
    };

    // The iterator borrows the three arrays; they must outlive it. A record's points borrow
    // either those arrays or (for a move or close) the iterator itself, until the next call.
    SkPathIter(SkSpan<const SkPoint> pts, SkSpan<const SkPathVerb> vbs, SkSpan<const float> cns)
        : fPoints(pts), fVerbs(vbs), fConics(cns)
    {
        // For compat older iterators, we trim off a trailing Move.
        // SkPathData is defined to never create this pattern, so perhaps in the future
        // this check can be removed (or replaced by an assert)
        if (!vbs.empty() && vbs.back() == SkPathVerb::kMove) {
            fVerbs = vbs.first(vbs.size() - 1);
        }
    }

    /*  Holds the current verb, and its associated points
     *  move:  pts[0]
     *  line:  pts[0..1]
     *  quad:  pts[0..2]
     *  conic: pts[0..2] fConicWeight
     *  cubic: pts[0..3]
     *  close: pts[0..1] ... as if close were a line from pts[0] to pts[1]
     */
    std::optional<Rec> next();

    std::optional<SkPathVerb> peekNextVerb() const {
        if (vIndex < fVerbs.size()) {
            const SkPathVerb verb = fVerbs[vIndex];
            return verb;
        }
        return {};
    }

private:
    size_t                   pIndex = 0;   // next point
    size_t                   vIndex = 0;   // next verb
    size_t                   cIndex = 0;   // next conic weight
    SkSpan<const SkPoint>    fPoints;
    SkSpan<const SkPathVerb> fVerbs;
    SkSpan<const float>      fConics;
    // [0] is the last point before a close; [1] is the current contour's first point.
    std::array<SkPoint, 2>   fClosePointStorage = {};
};

class SkPathContourIter {
public:
    struct Rec {
        SkSpan<const SkPoint>    fPoints;
        SkSpan<const SkPathVerb> fVerbs;
        SkSpan<const float>      fConics;
    };

    // Borrows the three arrays, which must outlive the iterator and its records.
    SkPathContourIter(SkSpan<const SkPoint> pts, SkSpan<const SkPathVerb> vbs,
                      SkSpan<const float> cns)
        : fPoints(pts), fVerbs(vbs), fConics(cns)
    {}

    std::optional<Rec> next();

private:
    SkSpan<const SkPoint>    fPoints;
    SkSpan<const SkPathVerb> fVerbs;
    SkSpan<const float>      fConics;
};

#endif
