/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkPathPriv_DEFINED
#define SkPathPriv_DEFINED

#include "include/core/SkArc.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSpan.h"
#include "include/core/SkTypes.h"
#include "include/private/SkDebug.h"
#include "include/private/SkIDChangeListener.h"
#include "include/private/SkPathRef.h"
#include "src/core/SkPathData.h"
#include "src/core/SkPathEnums.h"
#include "src/core/SkPathRaw.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>

class SkMatrix;
class SkRRect;

static_assert(0 == static_cast<int>(SkPathFillType::kWinding), "fill_type_mismatch");
static_assert(1 == static_cast<int>(SkPathFillType::kEvenOdd), "fill_type_mismatch");
static_assert(2 == static_cast<int>(SkPathFillType::kInverseWinding), "fill_type_mismatch");
static_assert(3 == static_cast<int>(SkPathFillType::kInverseEvenOdd), "fill_type_mismatch");

class SkPathPriv {
public:
    enum class RRectAsEnum {
        kRect, kOval, kRRect,
    };

    // The simplest shape that draws a rounded rectangle, and the start index for that shape.
    struct SimplifiedRRect {
        RRectAsEnum fShape;
        unsigned    fStartIndex;
    };

    static SimplifiedRRect SimplifyRRect(const SkRRect& rr, unsigned startIndex) {
        if (rr.isRect() || rr.isEmpty()) {
            const SimplifiedRRect rect{RRectAsEnum::kRect, (startIndex + 1) / 2};
            return rect;
        }
        if (rr.isOval()) {
            const SimplifiedRRect oval{RRectAsEnum::kOval, startIndex / 2};
            return oval;
        }
        const SimplifiedRRect rrect{RRectAsEnum::kRRect, startIndex};
        return rrect;
    }

    static SkPathConvexity ComputeConvexity(SkSpan<const SkPoint> pts,
                                            SkSpan<const SkPathVerb> verbs,
                                            SkSpan<const float> conicWeights);

    static SkPathConvexity TransformConvexity(const SkMatrix&, SkSpan<const SkPoint>,
                                              SkPathConvexity);

    static uint8_t ComputeSegmentMask(SkSpan<const SkPathVerb>);

    /* Note: does NOT use convexity in the raw, so it need not be resolved,
     *       if converting from builder or path.
     */
    static bool Contains(const SkPathRaw&, SkPoint);

    // skbug.com/40041027: Not a perfect solution for W plane clipping, but 1/16384 is a
    // reasonable limit (roughly 5e-5)
    inline static constexpr SkScalar kW0PlaneDistance = 1.f / (1 << 14);

    static SkPathFirstDirection AsFirstDirection(SkPathDirection dir) {
        // since we agree numerically for the values in Direction, we can just cast.
        const SkPathFirstDirection first = static_cast<SkPathFirstDirection>(dir);
        return first;
    }

    /**
     *  Return the opposite of the specified direction. kUnknown is its own
     *  opposite.
     */
    static SkPathFirstDirection OppositeFirstDirection(SkPathFirstDirection dir) {
        switch (dir) {
            case SkPathFirstDirection::kCW:
                return SkPathFirstDirection::kCCW;
            case SkPathFirstDirection::kCCW:
                return SkPathFirstDirection::kCW;
            case SkPathFirstDirection::kUnknown:
                return SkPathFirstDirection::kUnknown;
        }
        // An out-of-range direction is treated as unknown.
        return SkPathFirstDirection::kUnknown;
    }

    /**
     *  Tries to compute the direction of the outer-most non-degenerate
     *  contour. If it can be computed, return that direction. If it cannot be determined,
     *  or the contour is known to be convex, return kUnknown. If the direction was determined,
     *  it is cached to make subsequent calls return quickly.
     */
    static SkPathFirstDirection ComputeFirstDirection(const SkPathRaw&);
    static SkPathFirstDirection ComputeFirstDirection(const SkPath&);

    static bool IsClosedSingleContour(SkSpan<const SkPathVerb> verbs) {
        if (verbs.empty()) {
            return false;
        }

        int moveCount = 0;
        for (size_t i = 0; i < verbs.size(); ++i) {
            const SkPathVerb verb = verbs[i];
            if (verb == SkPathVerb::kMove) {
                ++moveCount;
                if (moveCount > 1) {
                    return false;
                }
            } else if (verb == SkPathVerb::kClose) {
                const bool lastVerb = i + 1 == verbs.size();
                return lastVerb;
            }
        }
        return false;
    }

    static bool IsClosedSingleContour(const SkPath& path) {
        const bool single = IsClosedSingleContour(path.verbs());
        return single;
    }

    /*
     *  Returns the index of the last moveTo() point, based on the verbs.
     *  If verbs is empty / ptCount == 0, then this returns -1.
     */
    static int FindLastMoveToIndex(SkSpan<const SkPathVerb> verbs, const size_t ptCount);

    // Winding direction and start index of a transformed oval or rounded rectangle.
    struct DirAndStart {
        SkPathDirection fDirection;
        unsigned        fStartIndex;
    };

    /*
     *  If we're transforming a known shape (oval or rrect), this computes what happens to its
     *  - winding direction
     *  - start index
     */
    static DirAndStart TransformDirAndStart(const SkMatrix&, bool isRRect, SkPathDirection dir,
                                            unsigned start);

    static void AddGenIDChangeListener(const SkPath&, sk_sp<SkIDChangeListener>);

    /**
     * This returns the info for a rect that has a move followed by 3 or 4 lines and a close. If
     * 'isSimpleFill' is true, an uncloseed rect will also be accepted as long as it starts and
     * ends at the same corner. This does not permit degenerate line or point rectangles.
     */
    static std::optional<SkPathRectInfo> IsSimpleRect(const SkPath& path, bool isSimpleFill);

    // Asserts the path contour was built from RRect, so it does not return
    // an optional. This exists so path's can have a flag that they are really
    // a RRect, without having to actually store the 4 radii... since those can
    // be deduced from the contour itself.
    //
    static SkRRect DeduceRRectFromContour(const SkRect& bounds,
                                          SkSpan<const SkPoint>, SkSpan<const SkPathVerb>);

    /**
     * Creates a path from arc params using the semantics of SkCanvas::drawArc. This function
     * assumes empty ovals and zero sweeps have already been filtered out.
     */
    static SkPath CreateDrawArcPath(const SkArc& arc, bool isFillNoPathEffect);

    /**
     * Determines whether an arc produced by CreateDrawArcPath will be convex. Assumes a non-empty
     * oval.
     */
    static bool DrawArcIsConvex(SkScalar sweepAngle, SkArc::Type arcType, bool isFillNoPathEffect);

    /**
      * Iterates through a raw range of path verbs, points, and conics. All values are returned
      * unaltered.
    */
    using RangeIter = SkPath::RangeIter;
    // One verb with its points (including the previous segment's end point) and weights.
    using VerbRecord = SkPath::VerbRecord;

    /**
     * Iterable object for traversing verbs, points, and conic weights in a path:
     *
     *   for (const SkPathPriv::VerbRecord record : SkPathPriv::Iterate(skPath)) {
     *       ...
     *   }
     *
     * The iteration borrows the path's storage; the path must outlive it.
     */
    struct Iterate {
    public:
        Iterate(SkPath&&) = delete;
        Iterate(const SkPath& path)
            : Iterate(path.verbs(), path.points().data(), path.conicWeights().data())
        {
            // Don't allow iteration through non-finite points.
            if (!path.isFinite()) {
                fVerbsBegin = fVerbsEnd;
            }
        }
        Iterate(SkSpan<const SkPathVerb> verbs, const SkPoint* points, const SkScalar* weights)
            : fVerbsBegin(verbs.data())
            , fVerbsEnd(verbs.data() + verbs.size())
            , fPoints(points)
            , fWeights(weights)
        {}
        SkPath::RangeIter begin() {
            const SkPath::RangeIter first(fVerbsBegin, fPoints, fWeights);
            return first;
        }
        SkPath::RangeIter end() {
            const SkPath::RangeIter last(fVerbsEnd, nullptr, nullptr);
            return last;
        }
    private:
        const SkPathVerb* fVerbsBegin = nullptr;
        const SkPathVerb* fVerbsEnd = nullptr;
        const SkPoint* fPoints = nullptr;
        const SkScalar* fWeights = nullptr;
    };

    // returns Empty() if there are no points
    static SkRect ComputeTightBounds(SkSpan<const SkPoint> points,
                                     SkSpan<const SkPathVerb> verbs,
                                     SkSpan<const float> conicWeights);

    /** Returns the oval info if this path was created as an oval or circle, else returns {}.
     */
    static std::optional<SkPathOvalInfo> IsOval(const SkPath& path) {
        const std::optional<SkPathOvalInfo> info = path.getOvalInfo();
        return info;
    }

    /** Returns the rrect info if this path was created as one, else returns {}.
     */
    static std::optional<SkPathRRectInfo> IsRRect(const SkPath& path) {
        const std::optional<SkPathRRectInfo> info = path.getRRectInfo();
        return info;
    }

    /**
     *  Sometimes in the drawing pipeline, we have to perform math on path coordinates, even after
     *  the path is in device-coordinates. Tessellation and clipping are two examples. Usually this
     *  is pretty modest, but it can involve subtracting/adding coordinates, or multiplying by
     *  small constants (e.g. 2,3,4). To try to preflight issues where these optionations could turn
     *  finite path values into infinities (or NaNs), we allow the upper drawing code to reject
     *  the path if its bounds (in device coordinates) is too close to max float.
     */
    static bool TooBigForMath(const SkRect& bounds) {
        // This value is just a guess. smaller is safer, but we don't want to reject largish paths
        // that we don't have to.
        constexpr SkScalar scale_down_to_allow_for_small_multiplies = 0.25f;
        constexpr SkScalar max = SK_ScalarMax * scale_down_to_allow_for_small_multiplies;

        // use ! expression so we return true if bounds contains NaN
        const bool tooBig = !(bounds.fLeft >= -max && bounds.fTop >= -max &&
                              bounds.fRight <= max && bounds.fBottom <= max);
        return tooBig;
    }

    // Returns number of valid points for each SkPath::Iter verb (SkPath::Verb values,
    // including kDone). An invalid verb has no points.
    static int PtsInIter(unsigned verb) {
        switch (verb) {
            case static_cast<unsigned>(SkPathVerb::kMove):  return 1;  // pts[0]
            case static_cast<unsigned>(SkPathVerb::kLine):  return 2;  // pts[0..1]
            case static_cast<unsigned>(SkPathVerb::kQuad):  return 3;  // pts[0..2]
            case static_cast<unsigned>(SkPathVerb::kConic): return 3;  // pts[0..2]
            case static_cast<unsigned>(SkPathVerb::kCubic): return 4;  // pts[0..3]
            default:
                SkASSERT(verb <= static_cast<unsigned>(SkPath::kDone_Verb));
                return 0;  // kClose, kDone
        }
    }

    static int PtsInIter(SkPathVerb verb) {
        const int count = PtsInIter(static_cast<unsigned>(verb));
        return count;
    }

    // Returns number of valid points for each verb, not including the "starter"
    // point that the Iterator adds for line/quad/conic/cubic. An invalid verb has no points.
    static int PtsInVerb(unsigned verb) {
        switch (verb) {
            case static_cast<unsigned>(SkPathVerb::kMove):  return 1;  // pts[0]
            case static_cast<unsigned>(SkPathVerb::kLine):  return 1;  // pts[0..1]
            case static_cast<unsigned>(SkPathVerb::kQuad):  return 2;  // pts[0..2]
            case static_cast<unsigned>(SkPathVerb::kConic): return 2;  // pts[0..2]
            case static_cast<unsigned>(SkPathVerb::kCubic): return 3;  // pts[0..3]
            default:
                SkASSERT(verb <= static_cast<unsigned>(SkPath::kDone_Verb));
                return 0;  // kClose, kDone
        }
    }

    static int PtsInVerb(SkPathVerb verb) {
        const int count = PtsInVerb(static_cast<unsigned>(verb));
        return count;
    }

    static bool IsAxisAligned(SkSpan<const SkPoint>);

    static bool AllPointsEq(SkSpan<const SkPoint> pts) {
        for (size_t i = 1; i < pts.size(); ++i) {
            if (pts[0] != pts[i]) {
                return false;
            }
        }
        return true;
    }

    struct RectContour {
        SkRect          fRect;
        bool            fIsClosed;
        SkPathDirection fDirection;
        size_t          fPointsConsumed;
        size_t          fVerbsConsumed;
    };
    static std::optional<RectContour> IsRectContour(SkSpan<const SkPoint> ptSpan,
                                                    SkSpan<const SkPathVerb> vbSpan,
                                                    uint32_t segmentMask,
                                                    bool allowPartial);

    /** Returns true if SkPath is equivalent to nested SkRect pair when filled.
     If false, rect and dirs are unchanged.
     If true, rect and dirs are written to if not nullptr:
     setting rect[0] to outer SkRect, and rect[1] to inner SkRect;
     setting dirs[0] to SkPathDirection of outer SkRect, and dirs[1] to SkPathDirection of
     inner SkRect.

     @param rect  storage for SkRect pair; may be nullptr
     @param dirs  storage for SkPathDirection pair; may be nullptr
     @return      true if SkPath contains nested SkRect pair
     */
    static bool IsNestedFillRects(const SkPathRaw&, SkRect rect[2],
                                  SkPathDirection dirs[2] = nullptr);

    static bool IsNestedFillRects(const SkPath& path, SkRect rect[2],
                                  SkPathDirection dirs[2] = nullptr) {
        const std::optional<SkPathRaw> raw = Raw(path, SkResolveConvexity::kNo);
        const bool nested = raw.has_value() && IsNestedFillRects(raw.value(), rect, dirs);
        return nested;
    }


    static bool IsInverseFillType(SkPathFillType fill) {
        const bool inverse = (static_cast<int>(fill) & 2) != 0;
        return inverse;
    }

    /**
     *  If needed (to not blow-up under a perspective matrix), clip the path, returning the
     *  answer in "result", and return true.
     *
     *  Note result might be empty (if the path was completely clipped out).
     *
     *  If no clipping is needed, returns false and "result" is left unchanged.
     */
    static bool PerspectiveClip(const SkPath& src, const SkMatrix&, SkPath* result);

    /**
     * Gets the number of GenIDChangeListeners. If another thread has access to this path then
     * this may be stale before return and only indicates that the count was the return value
     * at some point during the execution of the function.
     */
    static int GenIDChangeListenersCount(const SkPath&);

    static SkPathConvexity GetConvexity(const SkPath& path) {
        const SkPathConvexity convexity = path.getConvexity();
        return convexity;
    }
    static SkPathConvexity GetConvexityOrUnknown(const SkPath& path) {
        const SkPathConvexity convexity = path.getConvexityOrUnknown();
        return convexity;
    }
    static void SetConvexity(const SkPath& path, SkPathConvexity c) {
        path.setConvexity(c);
    }
    static void ForceComputeConvexity(const SkPath& path) {
        path.setConvexity(SkPathConvexity::kUnknown);
        (void)path.isConvex();
    }

    static SkPathConvexity GetConvexityOrUnknown(const SkPathData& pdata) {
        const SkPathConvexity convexity = pdata.getConvexityOrUnknown();
        return convexity;
    }

    static void ReverseAddPath(SkPathBuilder* builder, const SkPath& reverseMe) {
        (*builder).privateReverseAddPath(reverseMe);
    }

    static void ReversePathTo(SkPathBuilder* builder, const SkPath& reverseMe) {
        (*builder).privateReversePathTo(reverseMe);
    }

    static SkPath ReversePath(const SkPath& reverseMe) {
        SkPathBuilder bu{};
        bu.privateReverseAddPath(reverseMe);
        const SkPath reversed = bu.detach();
        return reversed;
    }

    // Borrows the builder's verbs until the builder next changes.
    static SkSpan<const SkPathVerb> GetVerbs(const SkPathBuilder& builder) {
        const SkSpan<const SkPathVerb> verbs(builder.fVerbs);
        return verbs;
    }
    static SkSpan<const SkPathVerb> GetVerbs(const SkPathBuilder&&) = delete;

    static int CountVerbs(const SkPathBuilder& builder) {
        const int count = builder.fVerbs.size();
        return count;
    }

    // The raw view borrows the path's storage; the path must outlive it.
    static std::optional<SkPathRaw> Raw(const SkPath& path, SkResolveConvexity rc) {
        const std::optional<SkPathRaw> raw = path.raw(rc);
        return raw;
    }
    static std::optional<SkPathRaw> Raw(const SkPath&&, SkResolveConvexity) = delete;

    // The raw view borrows the builder's arrays until the builder next changes.
    static std::optional<SkPathRaw> Raw(const SkPathBuilder& builder, SkResolveConvexity rc) {
        const std::optional<SkRect> bounds = builder.computeFiniteBounds();
        if (!bounds.has_value()) {
            return {};
        }

        SkPathConvexity convexity = builder.fConvexity;
        if (convexity == SkPathConvexity::kUnknown && rc == SkResolveConvexity::kYes) {
            convexity = SkPathPriv::ComputeConvexity(builder.fPts,
                                                     builder.fVerbs,
                                                     builder.fConicWeights);
        }

        const SkPathRaw raw{
            builder.points(),
            builder.verbs(),
            builder.conicWeights(),
            bounds.value(),
            builder.fillType(),
            convexity,
            SkTo<uint8_t>(builder.fSegmentMask),
        };
        return raw;
    }
    static std::optional<SkPathRaw> Raw(const SkPathBuilder&&, SkResolveConvexity) = delete;

    // Returns Empty if there are no points
    // Returns {} if the bounds are not finite
    static std::optional<SkRect> TrimmedBounds(SkSpan<const SkPoint> pts,
                                               SkSpan<const SkPathVerb> vbs) {
        // Does a trailing kMove verb contribute to the bounds?
        // - only if it is the only verb in the path
        // - otherwise we ignore it when computing bounds
        if (vbs.size() > 1 && vbs.back() == SkPathVerb::kMove) {
            SkASSERT(pts.size() > 0);
            // While trailing moves do not contribute to the bounds, we still reject them.
            if (!pts.back().isFinite()) {
                return {};
            }
            pts = pts.subspan(0, pts.size() - 1);
        }
        const std::optional<SkRect> bounds = SkRect::Bounds(pts);
        return bounds;
    }
};

// Lightweight variant of SkPath::Iter that only returns segments (e.g. lines/conics).
// Does not return kMove or kClose.
// Always "auto-closes" each contour.
// Roughly the same as SkPath::Iter(path, true), but does not return moves or closes
//
// The iterator borrows the path's storage; the path must outlive it. Returned points
// address either that storage or this iterator's scratch line.
class SkPathEdgeIter {
    const SkPathVerb* fVerbs = nullptr;
    const SkPathVerb* fVerbsStop = nullptr;
    const SkPoint*  fPts = nullptr;
    const SkPoint*  fMoveToPtr = nullptr;
    // Weights are read as fConicWeights[fConicIndex - 1] once a conic has been consumed.
    const SkScalar* fConicWeights = nullptr;
    size_t          fConicIndex = 0;
    SkPoint         fScratch[2] = {{0, 0}, {0, 0}};    // for auto-close lines
    bool            fNeedsCloseLine = false;
    bool            fNextIsNewContour = false;
    SkDEBUGCODE(bool fIsConic = false;)

public:
    SkPathEdgeIter(const SkPath& path);
    SkPathEdgeIter(const SkPath&&) = delete;
    SkPathEdgeIter(const SkPathRaw&);

    SkScalar conicWeight() const {
        SkASSERT(fIsConic);
        SkASSERT(fConicIndex > 0);
        return fConicWeights[fConicIndex - 1];
    }

    enum class Edge {
        kLine = (int)SkPathVerb::kLine,
        kQuad = (int)SkPathVerb::kQuad,
        kConic = (int)SkPathVerb::kConic,
        kCubic = (int)SkPathVerb::kCubic,
    };

    static SkPathVerb EdgeToVerb(Edge e) {
        const SkPathVerb verb = static_cast<SkPathVerb>(e);
        return verb;
    }

    struct Result {
        const SkPoint*  fPts;   // points for the segment, or null if done
        Edge            fEdge;
        bool            fIsNewContour;

        // Returns true when it holds an Edge, false when the path is done.
        explicit operator bool() const { return fPts != nullptr; }
    };

    Result next() {
        for (;;) {
            SkASSERT(fVerbs <= fVerbsStop);
            if (fVerbs == fVerbsStop) {
                if (fNeedsCloseLine) {
                    const Result closing = closeLine();
                    return closing;
                }
                const Result done{nullptr, Edge::kLine, false};
                return done;
            }

            SkDEBUGCODE(fIsConic = false;)

            const SkPathVerb verb = *fVerbs;
            ++fVerbs;
            switch (verb) {
                case SkPathVerb::kMove: {
                    if (fNeedsCloseLine) {
                        const Result closing = closeLine();
                        fMoveToPtr = fPts;
                        ++fPts;
                        return closing;
                    }
                    fMoveToPtr = fPts;
                    ++fPts;
                    fNextIsNewContour = true;
                    continue;
                }
                case SkPathVerb::kClose:
                    if (fNeedsCloseLine) {
                        const Result closing = closeLine();
                        return closing;
                    }
                    continue;
                case SkPathVerb::kLine:
                case SkPathVerb::kQuad:
                case SkPathVerb::kConic:
                case SkPathVerb::kCubic: {
                    const unsigned v = static_cast<unsigned>(verb);
                    // Actual edge. Line, quad, conic and cubic consume 1, 2, 2 and 3 points;
                    // only the conic consumes a weight.
                    const int pts_count = (v+2) / 2;
                    const int cws_count = (v & (v-1)) / 2;
                    SkASSERT(pts_count == SkPathPriv::PtsInIter(v) - 1);

                    fNeedsCloseLine = true;
                    fPts           += pts_count;
                    fConicIndex    += static_cast<size_t>(cws_count);

                    SkDEBUGCODE(fIsConic = (verb == SkPathVerb::kConic);)
                    SkASSERT(fIsConic == (cws_count > 0));

                    const bool isNewContour = fNextIsNewContour;
                    fNextIsNewContour = false;
                    const Result edge{&fPts[-(pts_count + 1)], static_cast<Edge>(v),
                                      isNewContour};
                    return edge;
                }
            }
            // Path storage holds only the six verbs; anything else ends iteration.
            const Result done{nullptr, Edge::kLine, false};
            return done;
        }
    }

private:
    // The line from the last point back to the contour's first point.
    Result closeLine() {
        fScratch[0] = fPts[-1];
        fScratch[1] = *fMoveToPtr;
        fNeedsCloseLine = false;
        fNextIsNewContour = true;
        const Result closing{fScratch, Edge::kLine, false};
        return closing;
    }
};

#endif
