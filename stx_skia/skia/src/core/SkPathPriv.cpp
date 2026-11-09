/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPathTypes.h"
#include "include/private/SkPathRef.h"
#include "include/private/SkTDArray.h"
#include "src/core/SkCubicClipper.h"
#include "src/core/SkGeometry.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPointPriv.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <optional>

int SkPathPriv::GenIDChangeListenersCount(const SkPath& path) {
    const int count = (*path.fPathData).genIDChangeListenerCount();
    return count;
}

void SkPathPriv::AddGenIDChangeListener(const SkPath& path, sk_sp<SkIDChangeListener> listener) {
    SkPathData* pdata = path.fPathData.get();
    // SkPath's error-singleton is never deleted, so we don't want to add any listeners to it.
    if (pdata != SkPath::PeekErrorSingleton()) {
        (*pdata).addGenIDChangeListener(std::move(listener));
    }
}

///////////////////////////////////////////////////////////////////////////

/*
 Determines if path is a rect by keeping track of changes in direction
 and looking for a loop either clockwise or counterclockwise.

 The direction is computed such that:
  0: vertical up
  1: horizontal left
  2: vertical down
  3: horizontal right

A rectangle cycles up/right/down/left or up/left/down/right.

The test fails if:
  The path is closed, and followed by a line.
  A second move creates a new endpoint.
  A diagonal line is parsed.
  There's more than four changes of direction.
  There's a discontinuity on the line (e.g., a move in the middle)
  The line reverses direction.
  The path contains a quadratic or cubic.
  The path contains fewer than four points.
 *The rectangle doesn't complete a cycle.
 *The final point isn't equal to the first point.

  *These last two conditions we relax if we have a 3-edge path that would
   form a rectangle if it were closed (as we do when we fill a path)

It's OK if the path has:
  Several colinear line segments composing a rectangle side.
  Single points on the rectangle side.

The direction takes advantage of the corners found since opposite sides
must travel in opposite directions.

FIXME: Allow colinear quads and cubics to be treated like lines.
FIXME: If the API passes fill-only, return true if the filled stroke
       is a rectangle, though the caller failed to close the path.

 directions values:
    0x1 is set if the segment is horizontal
    0x2 is set if the segment is moving to the right or down
 thus:
    two directions are opposites iff (dirA ^ dirB) == 0x2
    two directions are perpendicular iff (dirA ^ dirB) == 0x1

 */
static int rect_make_dir(SkScalar dx, SkScalar dy) {
    const int direction = ((0 != dx) << 0) | ((dx > 0 || dy > 0) << 1);
    return direction;
}

// Assuming one of the vectors is known to be axis-aligned, check whether the other one is
// orthogonal (and implicitly axis-aligned). I.e. we have exactly one vertical vector
// (fX == 0, fY != 0) and one horizontal vector (fX != 0, fY == 0). Bitwise operators avoid
// short-circuiting, which upstream measured as faster.
static bool axis_aligned_orthogonal(const SkVector& a, const SkVector& b) {
    const bool orthogonal = ((a.fX == 0) ^ (b.fX == 0)) &
                            ((a.fY == 0) ^ (b.fY == 0));
    return orthogonal;
}

// Quick check for a "trivial" rect, i.e. a rect created via SkPath::Rect(),
// SkPathBuilder::addRect(), etc.
static std::optional<SkPathPriv::RectContour> trivial_rect(SkSpan<const SkPoint> pts,
                                                           SkSpan<const SkPathVerb> vbs) {
    static constexpr std::array<SkPathVerb, 5> gTrivialVerbs = {
        SkPathVerb::kMove,
        SkPathVerb::kLine,
        SkPathVerb::kLine,
        SkPathVerb::kLine,
        SkPathVerb::kClose,
    };
    if (pts.size() != 4 ||
        vbs.size() != std::size(gTrivialVerbs) ||
        !std::equal(vbs.begin(), vbs.end(), std::begin(gTrivialVerbs))) {
        return std::nullopt;
    }

    const SkVector v0 = pts[1] - pts[0];
    const SkVector v1 = pts[2] - pts[1];
    const SkVector v2 = pts[3] - pts[2];
    const SkVector v3 = pts[0] - pts[3];

    // We have a rect iff the side vectors are axis aligned and form 3 right corners.
    // This can be further reduced to one axis aligned vector and 3 orthogonal vectors.
    // Note: bitwise operators are measurably faster in micro benchmarks (no short-circuiting?).
    if (!(
        // Axis-aligned v0.
        ((v0.fX == 0) ^ (v0.fY == 0)) &

        // Orthogonal vectors, in alternating dimensions.
        axis_aligned_orthogonal(v0, v1) &
        axis_aligned_orthogonal(v1, v2) &
        axis_aligned_orthogonal(v2, v3)
    )) {
        return std::nullopt;
    }

    const SkRect rect = SkRect::MakeLTRB(pts[0].fX, pts[0].fY, pts[2].fX, pts[2].fY).makeSorted();
    const SkPathDirection dir = SkPoint::CrossProduct(v0, v1) > 0
        ? SkPathDirection::kCW
        : SkPathDirection::kCCW;

    const SkPathPriv::RectContour contour{
        rect,
        true,
        dir,
        pts.size(),
        vbs.size(),
    };
    return contour;
}

std::optional<SkPathPriv::RectContour> SkPathPriv::IsRectContour(SkSpan<const SkPoint> ptSpan,
                                                                 SkSpan<const SkPathVerb> vbSpan,
                                                                 uint32_t segmentMask,
                                                                 bool allowPartial) {
    if (segmentMask != kLine_SkPathSegmentMask ||
        ptSpan.size() < 4 ||
        vbSpan.size() < 4) {
        return {};
    }

    const std::optional<SkPathPriv::RectContour> trivial = trivial_rect(ptSpan, vbSpan);
    if (trivial.has_value()) {
        return trivial;
    }

    size_t currVerb = 0;
    const size_t verbCnt = vbSpan.size();

    int corners = 0;
    SkPoint closeXY{0, 0};  // used to determine if final line falls on a diagonal
    SkPoint lineStart{0, 0};  // used to construct line from previous point
    const SkPoint* firstPt = nullptr; // first point in the rect (last of first moves)
    const SkPoint* lastPt = nullptr;  // last point in the rect (last of lines or first if closed)
    SkPoint firstCorner{0, 0};
    SkPoint thirdCorner{0, 0};
    // Walks the contour's points; the verbs come from path storage, so the counts agree.
    const SkPoint* pts = ptSpan.data();
    const SkPoint* savePts = nullptr; // used to allow caller to iterate through a pair of rects
    signed char directions[] = {-1, -1, -1, -1, -1};  // -1 to 3; -1 is uninitialized
    bool closedOrMoved = false;
    bool autoClose = false;
    bool insertClose = false;
    while (currVerb < verbCnt && (!allowPartial || !autoClose)) {
        // An inserted close is processed without consuming the verb at currVerb.
        const bool insertedClose = insertClose;
        const SkPathVerb verb = insertedClose ? SkPathVerb::kClose : vbSpan[currVerb];
        switch (verb) {
            case SkPathVerb::kClose:
                savePts = pts;
                autoClose = true;
                insertClose = false;
                [[fallthrough]];
            case SkPathVerb::kLine: {
                if (SkPathVerb::kClose != verb) {
                    lastPt = pts;
                }
                SkPoint lineEnd{0, 0};
                if (SkPathVerb::kClose == verb) {
                    lineEnd = *firstPt;
                } else {
                    lineEnd = *pts;
                    ++pts;
                }
                const SkVector lineDelta = lineEnd - lineStart;
                if (lineDelta.fX && lineDelta.fY) {
                    return {}; // diagonal
                }
                if (!lineDelta.isFinite()) {
                    return {}; // path contains infinity or NaN
                }
                if (lineStart == lineEnd) {
                    break; // single point on side OK
                }
                const int nextDirection = rect_make_dir(lineDelta.fX, lineDelta.fY); // 0 to 3
                if (0 == corners) {
                    directions[0] = static_cast<signed char>(nextDirection);
                    corners = 1;
                    closedOrMoved = false;
                    lineStart = lineEnd;
                    break;
                }
                if (closedOrMoved) {
                    return {}; // closed followed by a line
                }
                if (autoClose && nextDirection == directions[0]) {
                    break; // colinear with first
                }
                closedOrMoved = autoClose;
                if (directions[corners - 1] == nextDirection) {
                    if (3 == corners && SkPathVerb::kLine == verb) {
                        thirdCorner = lineEnd;
                    }
                    lineStart = lineEnd;
                    break; // colinear segment
                }
                directions[corners] = static_cast<signed char>(nextDirection);
                ++corners;
                // opposite lines must point in opposite directions; xoring them should equal 2
                switch (corners) {
                    case 2:
                        firstCorner = lineStart;
                        break;
                    case 3:
                        if ((directions[0] ^ directions[2]) != 2) {
                            return {};
                        }
                        thirdCorner = lineEnd;
                        break;
                    case 4:
                        if ((directions[1] ^ directions[3]) != 2) {
                            return {};
                        }
                        break;
                    default:
                        return {}; // too many direction changes
                }
                lineStart = lineEnd;
                break;
            }
            case SkPathVerb::kQuad:
            case SkPathVerb::kConic:
            case SkPathVerb::kCubic:
                return {}; // quadratic, cubic not allowed
            case SkPathVerb::kMove:
                if (allowPartial && !autoClose && directions[0] >= 0) {
                    // Close the partial rect first, then try this move again.
                    insertClose = true;
                    continue;
                }
                if (!corners) {
                    firstPt = pts;
                } else {
                    closeXY = *firstPt - *lastPt;
                    if (closeXY.fX && closeXY.fY) {
                        return {};   // we're diagonal, abort
                    }
                }
                lineStart = *pts;
                ++pts;
                closedOrMoved = true;
                break;
        }
        if (!insertedClose) {
            currVerb += 1;
        }
    }
    // Success if 4 corners and first point equals last
    if (corners < 3 || corners > 4) {
        return {};
    }
    // check if close generates diagonal
    closeXY = *firstPt - *lastPt;
    if (closeXY.fX && closeXY.fY) {
        return {};
    }

    SkRect cornerBounds{};
    cornerBounds.set(firstCorner, thirdCorner);
    const SkPathDirection direction = directions[0] == ((directions[1] + 1) & 3)
                                              ? SkPathDirection::kCW
                                              : SkPathDirection::kCCW;
    const size_t pointsConsumed = savePts ? static_cast<size_t>(savePts - ptSpan.data()) : 0;
    const SkPathPriv::RectContour contour{
        cornerBounds,
        autoClose,
        direction,
        pointsConsumed,
        currVerb,
    };
    return contour;
}

bool SkPathPriv::IsNestedFillRects(const SkPathRaw& raw, SkRect rects[2], SkPathDirection dirs[2]) {
    SkPathDirection testDirs[2] = {SkPathDirection::kCW, SkPathDirection::kCW};
    SkRect testRects[2] = {};

    SkSpan<const SkPoint> pts = raw.points();
    SkSpan<const SkPathVerb> vbs = raw.verbs();

    std::optional<SkPathPriv::RectContour> rc = IsRectContour(pts, vbs, raw.fSegmentMask, true);
    if (!rc.has_value()) {
        return false;
    }

    testDirs[0] = rc.value().fDirection;
    testRects[0] = rc.value().fRect;
    pts = pts.subspan(rc.value().fPointsConsumed);
    vbs = vbs.subspan(rc.value().fVerbsConsumed);

    rc = IsRectContour(pts, vbs, raw.fSegmentMask, false);
    if (rc.has_value()) {
        testDirs[1] = rc.value().fDirection;
        testRects[1] = rc.value().fRect;
        if (testRects[0].contains(testRects[1])) {
            if (rects) {
                rects[0] = testRects[0];
                rects[1] = testRects[1];
            }
            if (dirs) {
                dirs[0] = testDirs[0];
                dirs[1] = testDirs[1];
            }
            return true;
        }
        if (testRects[1].contains(testRects[0])) {
            if (rects) {
                rects[0] = testRects[1];
                rects[1] = testRects[0];
            }
            if (dirs) {
                dirs[0] = testDirs[1];
                dirs[1] = testDirs[0];
            }
            return true;
        }
    }
    return false;
}

bool SkPathPriv::DrawArcIsConvex(SkScalar sweepAngle,
                                 SkArc::Type arcType,
                                 bool isFillNoPathEffect) {
    if (isFillNoPathEffect && SkScalarAbs(sweepAngle) >= 360.f) {
        // This gets converted to an oval.
        return true;
    }
    if (arcType == SkArc::Type::kWedge) {
        // This is a pie wedge. It's convex if the angle is <= 180.
        const bool convexWedge = SkScalarAbs(sweepAngle) <= 180.f;
        return convexWedge;
    }
    // When the angle exceeds 360 this wraps back on top of itself. Otherwise it is a circle clipped
    // to a secant, i.e. convex.
    const bool convexArc = SkScalarAbs(sweepAngle) <= 360.f;
    return convexArc;
}

SkPath SkPathPriv::CreateDrawArcPath(const SkArc& arc, bool isFillNoPathEffect) {
    const SkRect oval = arc.fOval;
    SkScalar startAngle = arc.fStartAngle;
    SkScalar sweepAngle = arc.fSweepAngle;
    SkASSERT(!oval.isEmpty());
    SkASSERT(sweepAngle);
    // We cap the number of total rotations. This keeps the resulting paths simpler. More important,
    // it prevents values so large that the loops below never terminate (once ULP > 360).
    if (SkScalarAbs(sweepAngle) > 3600.0f) {
        sweepAngle = std::copysign(3600.0f, sweepAngle) + std::fmod(sweepAngle, 360.0f);
    }

    SkPathBuilder builder(SkPathFillType::kWinding);
    builder.setIsVolatile(true);

    if (isFillNoPathEffect && SkScalarAbs(sweepAngle) >= 360.f) {
        builder.addOval(oval);
        SkASSERT(DrawArcIsConvex(sweepAngle, SkArc::Type::kArc, isFillNoPathEffect));
        const SkPath ovalPath = builder.detach();
        return ovalPath;
    }

    if (arc.isWedge()) {
        builder.moveTo(oval.centerX(), oval.centerY());
    }
    const SkPathFirstDirection firstDir =
            sweepAngle > 0 ? SkPathFirstDirection::kCW : SkPathFirstDirection::kCCW;
    const bool convex = DrawArcIsConvex(sweepAngle, arc.fType, isFillNoPathEffect);
    // Arc to mods at 360 and drawArc is not supposed to.
    bool forceMoveTo = !arc.isWedge();
    while (sweepAngle <= -360.f) {
        builder.arcTo(oval, startAngle, -180.f, forceMoveTo);
        startAngle -= 180.f;
        builder.arcTo(oval, startAngle, -180.f, false);
        startAngle -= 180.f;
        forceMoveTo = false;
        sweepAngle += 360.f;
    }
    while (sweepAngle >= 360.f) {
        builder.arcTo(oval, startAngle, 180.f, forceMoveTo);
        startAngle += 180.f;
        builder.arcTo(oval, startAngle, 180.f, false);
        startAngle += 180.f;
        forceMoveTo = false;
        sweepAngle -= 360.f;
    }
    builder.arcTo(oval, startAngle, sweepAngle, forceMoveTo);
    if (arc.isWedge()) {
        builder.close();
    }

    const SkPath path = builder.detach();
    const SkPathConvexity convexity = convex ? SkPathFirstDirection_ToConvexity(firstDir)
                                             : SkPathConvexity::kConcave;
    path.setConvexity(convexity);
    return path;
}

///////////////////////////////////////////////////////////////////////////

static int sign(SkScalar x) {
    const int negative = x < 0;
    return negative;
}
static constexpr int kValueNeverReturnedBySign = 2;

enum DirChange {
    kUnknown_DirChange,
    kLeft_DirChange,
    kRight_DirChange,
    kStraight_DirChange,
    kBackwards_DirChange, // if double back, allow simple lines to be convex
    kInvalid_DirChange
};

// only valid for a single contour
struct Convexicator {

    /** The direction returned is only valid if the path is determined convex */
    SkPathFirstDirection getFirstDirection() const { return fFirstDirection; }

    void setMovePt(const SkPoint& pt) {
        fFirstPt = pt;
        fLastPt = pt;
        fExpectedDir = kInvalid_DirChange;
    }

    bool addPt(const SkPoint& pt) {
        if (fLastPt == pt) {
            return true;
        }
        // should only be true for first non-zero vector after setMovePt was called. It is possible
        // we doubled backed at the start so need to check if fLastVec is zero or not.
        if (fFirstPt == fLastPt && fExpectedDir == kInvalid_DirChange && fLastVec.equals(0,0)) {
            fLastVec = pt - fLastPt;
            fFirstVec = fLastVec;
        } else if (!addVec(pt - fLastPt)) {
            return false;
        }
        fLastPt = pt;
        return true;
    }

    // Counts sign changes of the edge vectors, including the edge that closes back to the first
    // point. More than three changes in x or y means the polygon cannot be convex.
    static bool IsConcaveBySign(SkSpan<const SkPoint> points) {
        const size_t count = points.size();
        if (count <= 3) {
            // point, line, or triangle are always convex
            return false;
        }

        const SkPoint firstPt = points[0];
        SkPoint currPt = firstPt;
        int dxes = 0;
        int dyes = 0;
        int lastSx = kValueNeverReturnedBySign;
        int lastSy = kValueNeverReturnedBySign;
        for (size_t i = 1; i <= count; ++i) {
            const SkPoint next = i < count ? points[i] : firstPt;
            const SkVector vec = next - currPt;
            if (!vec.isZero()) {
                // give up if vector construction failed
                if (!vec.isFinite()) {
                    return true;    // treat as concave
                }
                const int sx = sign(vec.fX);
                const int sy = sign(vec.fY);
                dxes += (sx != lastSx);
                dyes += (sy != lastSy);
                if (dxes > 3 || dyes > 3) {
                    return true;
                }
                lastSx = sx;
                lastSy = sy;
            }
            currPt = next;
        }
        return false;  // that is, it may be convex, don't know yet
    }

    bool close() {
        // If this was an explicit close, there was already a lineTo to fFirstPoint, so this
        // addPt() is a no-op. Otherwise, the addPt implicitly closes the contour. In either case,
        // we have to check the direction change along the first vector in case it is concave.
        const bool convex = addPt(fFirstPt) && addVec(fFirstVec);
        return convex;
    }

    bool isFinite() const {
        return fIsFinite;
    }

    int reversals() const {
        return fReversals;
    }

private:
    DirChange directionChange(const SkVector& curVec) {
        const SkScalar cross = SkPoint::CrossProduct(fLastVec, curVec);
        if (!SkIsFinite(cross)) {
            return kUnknown_DirChange;
        }
        if (cross == 0) {
            const DirChange straight = fLastVec.dot(curVec) < 0 ? kBackwards_DirChange
                                                                 : kStraight_DirChange;
            return straight;
        }
        const DirChange turn = 1 == SkScalarSignAsInt(cross) ? kRight_DirChange : kLeft_DirChange;
        return turn;
    }

    bool addVec(const SkVector& curVec) {
        const DirChange dir = directionChange(curVec);
        switch (dir) {
            case kLeft_DirChange:       // fall through
            case kRight_DirChange:
                if (kInvalid_DirChange == fExpectedDir) {
                    fExpectedDir = dir;
                    fFirstDirection = (kRight_DirChange == dir) ? SkPathFirstDirection::kCW
                                                                : SkPathFirstDirection::kCCW;
                } else if (dir != fExpectedDir) {
                    fFirstDirection = SkPathFirstDirection::kUnknown;
                    return false;
                }
                fLastVec = curVec;
                break;
            case kStraight_DirChange:
                break;
            case kBackwards_DirChange:
                //  allow path to reverse direction twice
                //    Given path.moveTo(0, 0); path.lineTo(1, 1);
                //    - 1st reversal: direction change formed by line (0,0 1,1), line (1,1 0,0)
                //    - 2nd reversal: direction change formed by line (1,1 0,0), line (0,0 1,1)
                fLastVec = curVec;
                ++fReversals;
                return fReversals < 3;
            case kUnknown_DirChange:
                fIsFinite = false;
                return false;
            case kInvalid_DirChange:
                SK_ABORT("Use of invalid direction change flag");
                break;
        }
        return true;
    }

    SkPoint              fFirstPt {0, 0};  // The first point of the contour, e.g. moveTo(x,y)
    SkVector             fFirstVec {0, 0}; // The direction leaving fFirstPt to the next vertex

    SkPoint              fLastPt {0, 0};   // The last point passed to addPt()
    SkVector             fLastVec {0, 0};  // The direction that brought the path to fLastPt

    DirChange            fExpectedDir { kInvalid_DirChange };
    SkPathFirstDirection fFirstDirection { SkPathFirstDirection::kUnknown };
    int                  fReversals { 0 };
    bool                 fIsFinite { true };
};

static void trim_trailing_moves(SkSpan<const SkPoint>& pts, SkSpan<const SkPathVerb>& vbs) {
    size_t vbCount = vbs.size();
    while (vbCount > 0 && vbs[vbCount - 1] == SkPathVerb::kMove) {
        vbCount -= 1;
    }
    // Each trailing move owns one point.
    const size_t delta = vbs.size() - vbCount;
    if (delta != 0) {
        SkASSERT(pts.size() >= delta);
        pts = pts.first(pts.size() - delta);
        vbs = vbs.first(vbs.size() - delta);
    }
}

SkPathConvexity SkPathPriv::ComputeConvexity(SkSpan<const SkPoint> points,
                                             SkSpan<const SkPathVerb> vbs,
                                             SkSpan<const float> conicWeights) {
    // callers need to give us finite values
    SkASSERT(SkRect::Bounds(points).has_value());

    trim_trailing_moves(points, vbs);

    if (vbs.empty()) {
        return SkPathConvexity::kConvex_Degenerate;
    }

    // Check to see if path changes direction more than three times as quick concave test
    if (Convexicator::IsConcaveBySign(points)) {
        return SkPathConvexity::kConcave;
    }

    int contourCount = 0;
    bool needsClose = false;
    Convexicator state{};

    SkPathIter iter(points, vbs, conicWeights);
    for (std::optional<SkPathIter::Rec> rec = iter.next(); rec.has_value(); rec = iter.next()) {
        const SkPathVerb verb = rec.value().fVerb;
        const SkSpan<const SkPoint> pts = rec.value().fPoints;

        // Looking for the last moveTo before non-move verbs start
        if (contourCount == 0) {
            if (verb == SkPathVerb::kMove) {
                state.setMovePt(pts[0]);
            } else {
                // Starting the actual contour, fall through to c=1 to add the points
                contourCount++;
                needsClose = true;
            }
        }
        // Accumulating points into the Convexicator until we hit a close or another move
        if (contourCount == 1) {
            if (verb == SkPathVerb::kClose || verb == SkPathVerb::kMove) {
                if (!state.close()) {
                    return SkPathConvexity::kConcave;
                }
                needsClose = false;
                contourCount++;
            } else {
                // lines add 1 point, cubics add 3, conics and quads add 2
                const int count = SkPathPriv::PtsInVerb(static_cast<unsigned>(verb));
                SkASSERT(count > 0);
                for (int i = 1; i <= count; ++i) {
                    if (!state.addPt(pts[i])) {
                        return SkPathConvexity::kConcave;
                    }
                }
            }
        } else {
            // The first contour has closed and anything other than spurious trailing moves means
            // there's multiple contours and the path can't be convex
            if (verb != SkPathVerb::kMove) {
                return SkPathConvexity::kConcave;
            }
        }
    }

    // If the path isn't explicitly closed do so implicitly
    if (needsClose && !state.close()) {
        return SkPathConvexity::kConcave;
    }

    const SkPathFirstDirection firstDir = state.getFirstDirection();
    if (firstDir == SkPathFirstDirection::kUnknown && state.reversals() >= 3) {
        return SkPathConvexity::kConcave;
    }
    const SkPathConvexity convexity = SkPathFirstDirection_ToConvexity(firstDir);
    return convexity;
}

///////////////////////////////////////////////////////////////////////////////

// returns cross product of (p1 - p0) and (p2 - p0)
static SkScalar cross_prod(const SkPoint& p0, const SkPoint& p1, const SkPoint& p2) {
    SkScalar cross = SkPoint::CrossProduct(p1 - p0, p2 - p0);
    // We may get 0 when the above subtracts underflow. We expect this to be
    // very rare and lazily promote to double.
    if (0 == cross) {
        const double p0x = SkScalarToDouble(p0.fX);
        const double p0y = SkScalarToDouble(p0.fY);

        const double p1x = SkScalarToDouble(p1.fX);
        const double p1y = SkScalarToDouble(p1.fY);

        const double p2x = SkScalarToDouble(p2.fX);
        const double p2y = SkScalarToDouble(p2.fY);

        cross = SkDoubleToScalar((p1x - p0x) * (p2y - p0y) -
                                 (p1y - p0y) * (p2x - p0x));

    }
    return cross;
}

// Returns the first pt with the maximum Y coordinate
static int find_max_y(const SkPoint pts[], int count) {
    SkASSERT(count > 0);
    SkScalar max = pts[0].fY;
    int firstIndex = 0;
    for (int i = 1; i < count; ++i) {
        const SkScalar y = pts[i].fY;
        if (y > max) {
            max = y;
            firstIndex = i;
        }
    }
    return firstIndex;
}

static int find_diff_pt(const SkPoint pts[], int index, int n, int inc) {
    int i = index;
    for (;;) {
        i = (i + inc) % n;
        if (i == index) {   // we wrapped around, so abort
            break;
        }
        if (pts[index] != pts[i]) { // found a different point, success!
            break;
        }
    }
    return i;
}

/**
 *  Starting at index, and moving forward (incrementing), find the xmin and
 *  xmax of the contiguous points that have the same Y.
 */
static int find_min_max_x_at_y(const SkPoint pts[], int index, int n,
                               int* maxIndexPtr) {
    const SkScalar y = pts[index].fY;
    SkScalar min = pts[index].fX;
    SkScalar max = min;
    int minIndex = index;
    int maxIndex = index;
    for (int i = index + 1; i < n; ++i) {
        if (pts[i].fY != y) {
            break;
        }
        const SkScalar x = pts[i].fX;
        if (x < min) {
            min = x;
            minIndex = i;
        } else if (x > max) {
            max = x;
            maxIndex = i;
        }
    }
    *maxIndexPtr = maxIndex;
    return minIndex;
}

static SkPathFirstDirection crossToDir(SkScalar cross) {
    const SkPathFirstDirection direction = cross > 0 ? SkPathFirstDirection::kCW
                                                     : SkPathFirstDirection::kCCW;
    return direction;
}

class ContourIter {
public:
    ContourIter(SkSpan<const SkPoint>, SkSpan<const SkPathVerb>, SkSpan<const float> conicWeights);

    bool done() const { return fDone; }
    // if !done() then these may be called
    int count() const { return fCurrPtCount; }
    const SkPoint* pts() const { return fCurrPt; }
    void next();

private:
    // Borrowed from the iterated path.
    int fCurrPtCount = 0;
    const SkPoint* fCurrPt = nullptr;
    const SkPathVerb* fCurrVerb = nullptr;
    const SkPathVerb* fStopVerbs = nullptr;
    bool fDone = false;
    SkDEBUGCODE(int fContourCounter = 0;)
};

// Conic weights do not affect contour extents; only points and verbs are walked.
ContourIter::ContourIter(SkSpan<const SkPoint> pts, SkSpan<const SkPathVerb> vbs,
                         SkSpan<const float>) {
    // An empty verb span leaves every cursor null.
    fStopVerbs = vbs.empty() ? nullptr : &vbs.back() + 1;
    fDone = false;
    fCurrPt = pts.data();
    fCurrVerb = vbs.data();
    fCurrPtCount = 0;
    SkDEBUGCODE(fContourCounter = 0;)
    next();
}

void ContourIter::next() {
    if (fCurrVerb >= fStopVerbs) {
        fDone = true;
    }
    if (fDone) {
        return;
    }

    // skip pts of prev contour
    fCurrPt += fCurrPtCount;

    SkASSERT(SkPathVerb::kMove == fCurrVerb[0]);
    int ptCount = 1;    // moveTo
    const SkPathVerb* verbs = fCurrVerb;

    for (++verbs; verbs < fStopVerbs; ++verbs) {
        const SkPathVerb verb = *verbs;
        if (verb == SkPathVerb::kMove) {
            // The next contour starts here.
            break;
        }
        switch (verb) {
            case SkPathVerb::kMove:
                break;
            case SkPathVerb::kLine:
                ptCount += 1;
                break;
            case SkPathVerb::kConic:
            case SkPathVerb::kQuad:
                ptCount += 2;
                break;
            case SkPathVerb::kCubic:
                ptCount += 3;
                break;
            case SkPathVerb::kClose:
                break;
        }
    }
    fCurrPtCount = ptCount;
    fCurrVerb = verbs;
    SkDEBUGCODE(++fContourCounter;)
}

/*
 *  We loop through all contours, and keep the computed cross-product of the
 *  contour that contained the global y-max. If we just look at the first
 *  contour, we may find one that is wound the opposite way (correctly) since
 *  it is the interior of a hole (e.g. 'o'). Thus we must find the contour
 *  that is outer most (or at least has the global y-max) before we can consider
 *  its cross product.
 */
SkPathFirstDirection SkPathPriv::ComputeFirstDirection(const SkPathRaw& raw) {
    ContourIter iter(raw.points(), raw.verbs(), raw.conics());

    // initialize with our logical y-min
    SkScalar ymax = raw.bounds().fTop;
    SkScalar ymaxCross = 0;

    for (; !iter.done(); iter.next()) {
        const int n = iter.count();
        if (n < 3) {
            continue;
        }

        const SkPoint* pts = iter.pts();
        SkScalar cross = 0;
        const int index = find_max_y(pts, n);
        if (pts[index].fY < ymax) {
            continue;
        }

        // If there is more than 1 distinct point at the y-max, we take the
        // x-min and x-max of them and just subtract to compute the dir.
        bool haveExtent = false;
        if (pts[(index + 1) % n].fY == pts[index].fY) {
            int maxIndex = index;
            const int minIndex = find_min_max_x_at_y(pts, index, n, &maxIndex);
            if (minIndex != maxIndex) {
                SkASSERT(pts[minIndex].fY == pts[index].fY);
                SkASSERT(pts[maxIndex].fY == pts[index].fY);
                SkASSERT(pts[minIndex].fX <= pts[maxIndex].fX);
                // we just subtract the indices, since we just want - or + to signal the
                // direction.
                cross = static_cast<SkScalar>(minIndex - maxIndex);
                haveExtent = true;
            }
        }
        if (!haveExtent) {
            // Find a next and prev index to use for the cross-product test,
            // but we try to find pts that form non-zero vectors from pts[index]
            //
            // Its possible that we can't find two non-degenerate vectors, so
            // we have to guard our search (e.g. all the pts could be in the
            // same place).

            // we pass n - 1 instead of -1 so we don't foul up % operator by
            // passing it a negative LH argument.
            const int prev = find_diff_pt(pts, index, n, n - 1);
            if (prev == index) {
                // completely degenerate, skip to next contour
                continue;
            }
            const int next = find_diff_pt(pts, index, n, 1);
            SkASSERT(next != index);
            cross = cross_prod(pts[prev], pts[index], pts[next]);
            // if we get a zero and the points are horizontal, then we look at the spread in
            // x-direction. We really should continue to walk away from the degeneracy until
            // there is a divergence.
            if (0 == cross && pts[prev].fY == pts[index].fY && pts[next].fY == pts[index].fY) {
                // construct the subtract so we get the correct Direction below
                cross = pts[index].fX - pts[next].fX;
            }
        }

        if (cross) {
            // record our best guess so far
            ymax = pts[index].fY;
            ymaxCross = cross;
        }
    }

    if (!ymaxCross) {
        return SkPathFirstDirection::kUnknown;
    }
    const SkPathFirstDirection direction = crossToDir(ymaxCross);
    return direction;
}

//////////////////////////////////////////////////////////////////////////////

// Horner evaluation of A*t^2 + B*t + C.
static float poly_eval(float A, float B, float C, float t) {
    const float value = (A * t + B) * t + C;
    return value;
}

// Horner evaluation of A*t^3 + B*t^2 + C*t + D.
static float poly_eval(float A, float B, float C, float D, float t) {
    const float value = ((A * t + B) * t + C) * t + D;
    return value;
}

static bool between(SkScalar a, SkScalar b, SkScalar c) {
    SkASSERT(((a <= b && b <= c) || (a >= b && b >= c)) == ((a - b) * (c - b) <= 0)
            || (SkScalarNearlyZero(a) && SkScalarNearlyZero(b) && SkScalarNearlyZero(c)));
    const bool inside = (a - b) * (c - b) <= 0;
    return inside;
}

static SkScalar eval_cubic_pts(SkScalar c0, SkScalar c1, SkScalar c2, SkScalar c3,
                               SkScalar t) {
    const SkScalar A = c3 + 3*(c1 - c2) - c0;
    const SkScalar B = 3*(c2 - c1 - c1 + c0);
    const SkScalar C = 3*(c1 - c0);
    const SkScalar D = c0;
    const SkScalar value = poly_eval(A, B, C, D, t);
    return value;
}

template <size_t N> static void find_minmax(const SkPoint pts[],
                                            SkScalar* minPtr, SkScalar* maxPtr) {
    SkScalar min = pts[0].fX;
    SkScalar max = pts[0].fX;
    for (size_t i = 1; i < N; ++i) {
        min = std::min(min, pts[i].fX);
        max = std::max(max, pts[i].fX);
    }
    *minPtr = min;
    *maxPtr = max;
}

static bool checkOnCurve(SkScalar x, SkScalar y, const SkPoint& start, const SkPoint& end) {
    if (start.fY == end.fY) {
        const bool onHorizontal = between(start.fX, x, end.fX) && x != end.fX;
        return onHorizontal;
    }
    const bool atStart = x == start.fX && y == start.fY;
    return atStart;
}

static int winding_mono_cubic(const SkPoint pts[], SkScalar x, SkScalar y, int* onCurveCount) {
    SkScalar y0 = pts[0].fY;
    SkScalar y3 = pts[3].fY;

    int dir = 1;
    if (y0 > y3) {
        using std::swap;
        swap(y0, y3);
        dir = -1;
    }
    if (y < y0 || y > y3) {
        return 0;
    }
    if (checkOnCurve(x, y, pts[0], pts[3])) {
        *onCurveCount += 1;
        return 0;
    }
    if (y == y3) {
        return 0;
    }

    // quickreject or quickaccept
    SkScalar min = 0;
    SkScalar max = 0;
    find_minmax<4>(pts, &min, &max);
    if (x < min) {
        return 0;
    }
    if (x > max) {
        return dir;
    }

    // compute the actual x(t) value
    SkScalar t = 0;
    if (!SkCubicClipper::ChopMonoAtY(pts, y, &t)) {
        return 0;
    }
    const SkScalar xt = eval_cubic_pts(pts[0].fX, pts[1].fX, pts[2].fX, pts[3].fX, t);
    if (SkScalarNearlyEqual(xt, x)) {
        if (x != pts[3].fX || y != pts[3].fY) {  // don't test end points; they're start points
            *onCurveCount += 1;
            return 0;
        }
    }
    const int winding = xt < x ? dir : 0;
    return winding;
}

static int winding_cubic(SkSpan<const SkPoint> pts, SkScalar x, SkScalar y, int* onCurveCount) {
    SkPoint dst[10] = {};
    const int n = SkChopCubicAtYExtrema(pts.data(), dst);
    int w = 0;
    for (int i = 0; i <= n; ++i) {
        w += winding_mono_cubic(&dst[i * 3], x, y, onCurveCount);
    }
    return w;
}

static double conic_eval_numerator(const SkScalar src[], SkScalar w, SkScalar t) {
    SkASSERT(src);
    SkASSERT(t >= 0 && t <= 1);
    // src addresses three points as six floats: x0 y0 x1 y1 x2 y2; stride 2 selects one axis.
    const SkScalar src2w = src[2] * w;
    const SkScalar C = src[0];
    const SkScalar A = src[4] - 2 * src2w + C;
    const SkScalar B = 2 * (src2w - C);
    const double numerator = poly_eval(A, B, C, t);
    return numerator;
}


static double conic_eval_denominator(SkScalar w, SkScalar t) {
    const SkScalar B = 2 * (w - 1);
    const SkScalar C = 1;
    const SkScalar A = -B;
    const double denominator = poly_eval(A, B, C, t);
    return denominator;
}

static int winding_mono_conic(const SkConic& conic, SkScalar x, SkScalar y, int* onCurveCount) {
    const SkPoint* pts = conic.fPts;
    SkScalar y0 = pts[0].fY;
    SkScalar y2 = pts[2].fY;

    int dir = 1;
    if (y0 > y2) {
        using std::swap;
        swap(y0, y2);
        dir = -1;
    }
    if (y < y0 || y > y2) {
        return 0;
    }
    if (checkOnCurve(x, y, pts[0], pts[2])) {
        *onCurveCount += 1;
        return 0;
    }
    if (y == y2) {
        return 0;
    }

    SkScalar roots[2] = {0, 0};
    SkScalar A = pts[2].fY;
    SkScalar B = pts[1].fY * conic.fW - y * conic.fW + y;
    SkScalar C = pts[0].fY;
    A += C - 2 * B;  // A = a + c - 2*(b*w - yCept*w + yCept)
    B -= C;  // B = b*w - w * yCept + yCept - a
    C -= y;
    const int n = SkFindUnitQuadRoots(A, 2 * B, C, roots);
    SkASSERT(n <= 1);
    SkScalar xt = 0;
    if (0 == n) {
        // zero roots are returned only when y0 == y
        // Need [0] if dir == 1
        // and  [2] if dir == -1
        xt = pts[1 - dir].fX;
    } else {
        const SkScalar t = roots[0];
        xt = conic_eval_numerator(&pts[0].fX, conic.fW, t) / conic_eval_denominator(conic.fW, t);
    }
    if (SkScalarNearlyEqual(xt, x)) {
        if (x != pts[2].fX || y != pts[2].fY) {  // don't test end points; they're start points
            *onCurveCount += 1;
            return 0;
        }
    }
    const int winding = xt < x ? dir : 0;
    return winding;
}

static bool is_mono_quad(SkScalar y0, SkScalar y1, SkScalar y2) {
    //    return SkScalarSignAsInt(y0 - y1) + SkScalarSignAsInt(y1 - y2) != 0;
    if (y0 == y1) {
        return true;
    }
    if (y0 < y1) {
        const bool rising = y1 <= y2;
        return rising;
    }
    const bool falling = y1 >= y2;
    return falling;
}

static int winding_conic(SkSpan<const SkPoint> pts, SkScalar x, SkScalar y, SkScalar weight,
                         int* onCurveCount) {
    const SkConic conic(pts.data(), weight);
    SkConic chopped[2];
    // If the data points are very large, the conic may not be monotonic but may also
    // fail to chop. Then, the chopper does not split the original conic in two.
    const bool isMono = is_mono_quad(pts[0].fY, pts[1].fY, pts[2].fY) ||
                        !conic.chopAtYExtrema(chopped);
    int w = winding_mono_conic(isMono ? conic : chopped[0], x, y, onCurveCount);
    if (!isMono) {
        w += winding_mono_conic(chopped[1], x, y, onCurveCount);
    }
    return w;
}

static int winding_mono_quad(SkSpan<const SkPoint> pts, SkScalar x, SkScalar y, int* onCurveCount) {
    SkScalar y0 = pts[0].fY;
    SkScalar y2 = pts[2].fY;

    int dir = 1;
    if (y0 > y2) {
        using std::swap;
        swap(y0, y2);
        dir = -1;
    }
    if (y < y0 || y > y2) {
        return 0;
    }
    if (checkOnCurve(x, y, pts[0], pts[2])) {
        *onCurveCount += 1;
        return 0;
    }
    if (y == y2) {
        return 0;
    }
    SkScalar roots[2] = {0, 0};
    const int n = SkFindUnitQuadRoots(pts[0].fY - 2 * pts[1].fY + pts[2].fY,
                                      2 * (pts[1].fY - pts[0].fY),
                                      pts[0].fY - y,
                                      roots);
    SkASSERT(n <= 1);
    SkScalar xt = 0;
    if (0 == n) {
        // zero roots are returned only when y0 == y
        // Need [0] if dir == 1
        // and  [2] if dir == -1
        xt = pts[1 - dir].fX;
    } else {
        const SkScalar t = roots[0];
        const SkScalar C = pts[0].fX;
        const SkScalar A = pts[2].fX - 2 * pts[1].fX + C;
        const SkScalar B = 2 * (pts[1].fX - C);
        xt = poly_eval(A, B, C, t);
    }
    if (SkScalarNearlyEqual(xt, x)) {
        if (x != pts[2].fX || y != pts[2].fY) {  // don't test end points; they're start points
            *onCurveCount += 1;
            return 0;
        }
    }
    const int winding = xt < x ? dir : 0;
    return winding;
}

static int winding_quad(SkSpan<const SkPoint> pts, SkScalar x, SkScalar y, int* onCurveCount) {
    SkPoint               spanStorage[5] = {};
    SkSpan<const SkPoint> span = pts;
    int                   n = 0;

    if (!is_mono_quad(pts[0].fY, pts[1].fY, pts[2].fY)) {
        n = SkChopQuadAtYExtrema(pts.data(), spanStorage);
        span = spanStorage;
    }
    int w = winding_mono_quad(span, x, y, onCurveCount);
    if (n > 0) {
        w += winding_mono_quad(span.subspan(2), x, y, onCurveCount);
    }
    return w;
}

static int winding_line(SkSpan<const SkPoint> pts, SkScalar x, SkScalar y, int* onCurveCount) {
    const SkScalar x0 = pts[0].fX;
    SkScalar y0 = pts[0].fY;
    const SkScalar x1 = pts[1].fX;
    SkScalar y1 = pts[1].fY;

    const SkScalar dy = y1 - y0;

    int dir = 1;
    if (y0 > y1) {
        using std::swap;
        swap(y0, y1);
        dir = -1;
    }
    if (y < y0 || y > y1) {
        return 0;
    }
    if (checkOnCurve(x, y, pts[0], pts[1])) {
        *onCurveCount += 1;
        return 0;
    }
    if (y == y1) {
        return 0;
    }
    const SkScalar cross = (x1 - x0) * (y - pts[0].fY) - dy * (x - x0);

    if (!cross) {
        // zero cross means the point is on the line, and since the case where
        // y of the query point is at the end point is handled above, we can be
        // sure that we're on the line (excluding the end point) here
        if (x != x1 || y != pts[1].fY) {
            *onCurveCount += 1;
        }
        dir = 0;
    } else if (SkScalarSignAsInt(cross) == dir) {
        dir = 0;
    }
    return dir;
}

static void tangent_cubic(SkSpan<const SkPoint> pts, SkScalar x, SkScalar y,
                          SkTDArray<SkVector>* tangents) {
    if (!between(pts[0].fY, y, pts[1].fY) && !between(pts[1].fY, y, pts[2].fY)
             && !between(pts[2].fY, y, pts[3].fY)) {
        return;
    }
    if (!between(pts[0].fX, x, pts[1].fX) && !between(pts[1].fX, x, pts[2].fX)
             && !between(pts[2].fX, x, pts[3].fX)) {
        return;
    }
    SkPoint dst[10] = {};
    const int n = SkChopCubicAtYExtrema(pts.data(), dst);
    for (int i = 0; i <= n; ++i) {
        const SkPoint* c = &dst[i * 3];
        SkScalar t = 0;
        if (!SkCubicClipper::ChopMonoAtY(c, y, &t)) {
            continue;
        }
        const SkScalar xt = eval_cubic_pts(c[0].fX, c[1].fX, c[2].fX, c[3].fX, t);
        if (!SkScalarNearlyEqual(x, xt)) {
            continue;
        }
        SkVector tangent{0, 0};
        SkEvalCubicAt(c, t, nullptr, &tangent, nullptr);
        (*tangents).push_back(tangent);
    }
}

static void tangent_conic(SkSpan<const SkPoint> pts, SkScalar x, SkScalar y, SkScalar w,
                          SkTDArray<SkVector>* tangents) {
    if (!between(pts[0].fY, y, pts[1].fY) && !between(pts[1].fY, y, pts[2].fY)) {
        return;
    }
    if (!between(pts[0].fX, x, pts[1].fX) && !between(pts[1].fX, x, pts[2].fX)) {
        return;
    }
    SkScalar roots[2] = {0, 0};
    SkScalar A = pts[2].fY;
    SkScalar B = pts[1].fY * w - y * w + y;
    SkScalar C = pts[0].fY;
    A += C - 2 * B;  // A = a + c - 2*(b*w - yCept*w + yCept)
    B -= C;  // B = b*w - w * yCept + yCept - a
    C -= y;
    const int n = SkFindUnitQuadRoots(A, 2 * B, C, roots);
    for (int index = 0; index < n; ++index) {
        const SkScalar t = roots[index];
        const SkScalar xt = conic_eval_numerator(&pts[0].fX, w, t) / conic_eval_denominator(w, t);
        if (!SkScalarNearlyEqual(x, xt)) {
            continue;
        }
        const SkConic conic(pts.data(), w);
        (*tangents).push_back(conic.evalTangentAt(t));
    }
}

static void tangent_quad(SkSpan<const SkPoint> pts, SkScalar x, SkScalar y,
                         SkTDArray<SkVector>* tangents) {
    if (!between(pts[0].fY, y, pts[1].fY) && !between(pts[1].fY, y, pts[2].fY)) {
        return;
    }
    if (!between(pts[0].fX, x, pts[1].fX) && !between(pts[1].fX, x, pts[2].fX)) {
        return;
    }
    SkScalar roots[2] = {0, 0};
    const int n = SkFindUnitQuadRoots(pts[0].fY - 2 * pts[1].fY + pts[2].fY,
                                      2 * (pts[1].fY - pts[0].fY),
                                      pts[0].fY - y,
                                      roots);
    for (int index = 0; index < n; ++index) {
        const SkScalar t = roots[index];
        const SkScalar C = pts[0].fX;
        const SkScalar A = pts[2].fX - 2 * pts[1].fX + C;
        const SkScalar B = 2 * (pts[1].fX - C);
        const SkScalar xt = poly_eval(A, B, C, t);
        if (!SkScalarNearlyEqual(x, xt)) {
            continue;
        }
        (*tangents).push_back(SkEvalQuadTangentAt(pts.data(), t));
    }
}

static void tangent_line(SkSpan<const SkPoint> pts, SkScalar x, SkScalar y,
                         SkTDArray<SkVector>* tangents) {
    const SkScalar y0 = pts[0].fY;
    const SkScalar y1 = pts[1].fY;
    if (!between(y0, y, y1)) {
        return;
    }
    const SkScalar x0 = pts[0].fX;
    const SkScalar x1 = pts[1].fX;
    if (!between(x0, x, x1)) {
        return;
    }
    const SkScalar dx = x1 - x0;
    const SkScalar dy = y1 - y0;
    if (!SkScalarNearlyEqual((x - x0) * dy, dx * (y - y0))) {
        return;
    }
    const SkVector v{dx, dy};
    (*tangents).push_back(v);
}

static bool contains_inclusive(const SkRect& r, SkPoint p) {
    const bool inside = r.fLeft <= p.fX && p.fX <= r.fRight && r.fTop <= p.fY && p.fY <= r.fBottom;
    return inside;
}

bool SkPathPriv::Contains(const SkPathRaw& raw, SkPoint p) {
    const SkPathFillType ft = raw.fillType();
    const bool isInverse = SkPathFillType_IsInverse(ft);
    if (raw.empty()) {
        return isInverse;
    }

    if (!contains_inclusive(raw.bounds(), p)) {
        return isInverse;
    }

    int w = 0;
    int onCurveCount = 0;

    SkPathEdgeIter windingIter(raw);
    for (SkPathEdgeIter::Result rec = windingIter.next(); rec; rec = windingIter.next()) {
        switch (rec.fEdge) {
            case SkPathEdgeIter::Edge::kLine:
                w += winding_line(SkSpan<const SkPoint>(rec.fPts, 2), p.fX, p.fY, &onCurveCount);
                break;
            case SkPathEdgeIter::Edge::kQuad:
                w += winding_quad(SkSpan<const SkPoint>(rec.fPts, 3), p.fX, p.fY, &onCurveCount);
                break;
            case SkPathEdgeIter::Edge::kConic:
                w += winding_conic(SkSpan<const SkPoint>(rec.fPts, 3), p.fX, p.fY,
                                   windingIter.conicWeight(), &onCurveCount);
                break;
            case SkPathEdgeIter::Edge::kCubic:
                w += winding_cubic(SkSpan<const SkPoint>(rec.fPts, 4), p.fX, p.fY, &onCurveCount);
                break;
       }
    }
    const bool evenOddFill = SkPathFillType::kEvenOdd        == ft
                          || SkPathFillType::kInverseEvenOdd == ft;
    if (evenOddFill) {
        w &= 1;
    }
    if (w) {
        return !isInverse;
    }
    if (onCurveCount <= 1) {
        const bool onCurve = SkToBool(onCurveCount) ^ isInverse;
        return onCurve;
    }
    if ((onCurveCount & 1) || evenOddFill) {
        const bool oddOnCurve = SkToBool(onCurveCount & 1) ^ isInverse;
        return oddOnCurve;
    }
    // If the point touches an even number of curves, and the fill is winding, check for
    // coincidence. Count coincidence as places where the on curve points have identical tangents.
    SkTDArray<SkVector> tangents{};
    SkPathEdgeIter tangentIter(raw);
    for (SkPathEdgeIter::Result rec = tangentIter.next(); rec; rec = tangentIter.next()) {
        const int oldCount = tangents.size();
        switch (rec.fEdge) {
            case SkPathEdgeIter::Edge::kLine:
                tangent_line(SkSpan<const SkPoint>(rec.fPts, 2), p.fX, p.fY, &tangents);
                break;
            case SkPathEdgeIter::Edge::kQuad:
                tangent_quad(SkSpan<const SkPoint>(rec.fPts, 3), p.fX, p.fY, &tangents);
                break;
            case SkPathEdgeIter::Edge::kConic:
                tangent_conic(SkSpan<const SkPoint>(rec.fPts, 3), p.fX, p.fY,
                              tangentIter.conicWeight(), &tangents);
                break;
            case SkPathEdgeIter::Edge::kCubic:
                tangent_cubic(SkSpan<const SkPoint>(rec.fPts, 4), p.fX, p.fY, &tangents);
                break;
       }
       if (tangents.size() > oldCount) {
            const int last = tangents.size() - 1;
            const SkVector& tangent = tangents[last];
            if (SkScalarNearlyZero(SkPointPriv::LengthSqd(tangent))) {
                tangents.remove(last);
            } else {
                for (int index = 0; index < last; ++index) {
                    const SkVector& test = tangents[index];
                    if (SkScalarNearlyZero(test.cross(tangent))
                            && SkScalarSignAsInt(tangent.fX * test.fX) <= 0
                            && SkScalarSignAsInt(tangent.fY * test.fY) <= 0) {
                        tangents.remove(last);
                        tangents.removeShuffle(index);
                        break;
                    }
                }
            }
        }
    }
    const bool inside = SkToBool(tangents.size()) ^ isInverse;
    return inside;
}

////////////////////////////////////////////////////////////////////////////////////////

bool SkPathPriv::IsAxisAligned(SkSpan<const SkPoint> pts) {
    // Conservative (quick) test to see if all segments are axis-aligned.
    // Multiple contours might give a false-negative, but for speed, we ignore that
    // and just look at the raw points.

    for (size_t i = 1; i < pts.size(); ++i) {
        if (pts[i-1].fX != pts[i].fX && pts[i-1].fY != pts[i].fY) {
            return false;
        }
    }
    return true;
}

SkPathConvexity SkPathPriv::TransformConvexity(const SkMatrix& matrix, SkSpan<const SkPoint> pts,
                                               SkPathConvexity convexity) {
    if (matrix.isIdentity() || pts.empty()) {
        return convexity;
    }

    // Due to finite/fragile float numerics, we can't assume that a convex path remains
    // convex after a transformation, so mark it as unknown here.
    // However, some transformations are thought to be safe:
    //    axis-aligned values under scale/translate.
    //
    if (SkPathConvexity_IsConvex(convexity)) {
        if (!matrix.isScaleTranslate() || !SkPathPriv::IsAxisAligned(pts)) {
            // Not safe to still assume we're convex...
            convexity = SkPathConvexity::kUnknown;
        } else {
            const SkScalar det2x2 =
                matrix.get(SkMatrix::kMScaleX) * matrix.get(SkMatrix::kMScaleY) -
                matrix.get(SkMatrix::kMSkewX)  * matrix.get(SkMatrix::kMSkewY);
            if (det2x2 < 0) {
                convexity = SkPathConvexity_OppositeConvexDirection(convexity);
            } else if (det2x2 > 0) {
                // we keep our direction
            } else /* det2x == 0 */ {
                convexity = SkPathConvexity::kConvex_Degenerate;
            }
        }
    }
    return convexity;
}

//////////////////////////////////////////////////////////////////////////////

static int compute_quad_extremas(const SkPoint src[3], SkPoint extremas[3]) {
    SkScalar ts[2] = {0, 0};
    int n  = SkFindQuadExtrema(src[0].fX, src[1].fX, src[2].fX, ts);
        n += SkFindQuadExtrema(src[0].fY, src[1].fY, src[2].fY, &ts[n]);
    // Each axis contributes at most one extremum.
    if (n < 0 || n > 2) {
        n = 0;
    }
    for (int i = 0; i < n; ++i) {
        extremas[i] = SkEvalQuadAt(src, ts[i]);
    }
    extremas[n] = src[2];
    return n + 1;
}

static int compute_conic_extremas(const SkPoint src[3], SkScalar w, SkPoint extremas[3]) {
    const SkConic conic(src[0], src[1], src[2], w);
    SkScalar ts[2] = {0, 0};
    int n  = conic.findXExtrema(ts);
        n += conic.findYExtrema(&ts[n]);
    // Each axis contributes at most one extremum.
    if (n < 0 || n > 2) {
        n = 0;
    }
    for (int i = 0; i < n; ++i) {
        extremas[i] = conic.evalAt(ts[i]);
    }
    extremas[n] = src[2];
    return n + 1;
}

static int compute_cubic_extremas(const SkPoint src[4], SkPoint extremas[5]) {
    SkScalar ts[4] = {0, 0, 0, 0};
    int n  = SkFindCubicExtrema(src[0].fX, src[1].fX, src[2].fX, src[3].fX, ts);
        n += SkFindCubicExtrema(src[0].fY, src[1].fY, src[2].fY, src[3].fY, &ts[n]);
    // Each axis contributes at most two extrema.
    if (n < 0 || n > 4) {
        n = 0;
    }
    for (int i = 0; i < n; ++i) {
        SkEvalCubicAt(src, ts[i], &extremas[i], nullptr, nullptr);
    }
    extremas[n] = src[3];
    return n + 1;
}

SkRect SkPathPriv::ComputeTightBounds(SkSpan<const SkPoint> points,
                                      SkSpan<const SkPathVerb> verbs,
                                      SkSpan<const float> conicWeights) {
    if (verbs.empty()) {
        return SkRect::MakeEmpty();
    }

    // initial with the first MoveTo, so we don't have to check inside the switch
    float L = points[0].fX;
    float T = points[0].fY;
    float R = points[0].fX;
    float B = points[0].fY;

    for (const SkPathPriv::VerbRecord record :
            SkPathPriv::Iterate(verbs, points.data(), conicWeights.data())) {
        const SkPoint* pts = record.fPoints;
        SkPoint extremas[5] = {}; // big enough to hold worst-case curve type (cubic) extremas + 1
        int count = 0;
        switch (record.fVerb) {
            case SkPathVerb::kMove:
                extremas[0] = pts[0];
                count = 1;
                break;
            case SkPathVerb::kLine:
                extremas[0] = pts[1];
                count = 1;
                break;
            case SkPathVerb::kQuad:
                count = compute_quad_extremas(pts, extremas);
                break;
            case SkPathVerb::kConic:
                count = compute_conic_extremas(pts, *record.fWeights, extremas);
                break;
            case SkPathVerb::kCubic:
                count = compute_cubic_extremas(pts, extremas);
                break;
            case SkPathVerb::kClose:
                break;
        }
        for (int i = 0; i < count; ++i) {
            const SkPoint p = extremas[i];
            L = std::fminf(p.fX, L);
            T = std::fminf(p.fY, T);
            R = std::fmaxf(p.fX, R);
            B = std::fmaxf(p.fY, B);
        }
    }
    const SkRect bounds{L, T, R, B};
    return bounds;
}

int SkPathPriv::FindLastMoveToIndex(SkSpan<const SkPathVerb> verbs, const size_t ptCount) {
    if (verbs.empty()) {
        SkASSERT(ptCount == 0);
        return -1;
    }
    SkASSERT(verbs[0] == SkPathVerb::kMove);
    SkASSERT(ptCount > 0);

    int ptIndex = SkToInt(ptCount) - 1;
    for (size_t i = verbs.size(); i > 0; --i) {
        const SkPathVerb verb = verbs[i - 1];
        if (verb == SkPathVerb::kMove) {
            break;
        }
        ptIndex -= PtsInVerb(verb);
    }
    SkASSERT(ptIndex >= 0);
    return ptIndex;
}

SkPathPriv::DirAndStart
SkPathPriv::TransformDirAndStart(const SkMatrix& matrix, bool isRRect, SkPathDirection dir,
                                 unsigned start) {
    unsigned inStart = start;
    bool isCCW = (dir == SkPathDirection::kCCW);

    unsigned rm = 0;
    if (isRRect) {
        // Degenerate rrect indices to oval indices and remember the remainder.
        // Ovals have one index per side whereas rrects have two.
        rm = inStart & 0b1;
        inStart /= 2;
    }
    // Is the antidiagonal non-zero (otherwise the diagonal is zero)
    unsigned antiDiag = 0;
    // Is the non-zero value in the top row (either kMScaleX or kMSkewX) negative
    unsigned topNeg = 0;
    // Are the two non-zero diagonal or antidiagonal values the same sign.
    unsigned sameSign = 0;
    if (matrix.get(SkMatrix::kMScaleX) != 0) {
        antiDiag = 0b00;
        if (matrix.get(SkMatrix::kMScaleX) > 0) {
            topNeg = 0b00;
            sameSign = matrix.get(SkMatrix::kMScaleY) > 0 ? 0b01 : 0b00;
        } else {
            topNeg = 0b10;
            sameSign = matrix.get(SkMatrix::kMScaleY) > 0 ? 0b00 : 0b01;
        }
    } else {
        antiDiag = 0b01;
        if (matrix.get(SkMatrix::kMSkewX) > 0) {
            topNeg = 0b00;
            sameSign = matrix.get(SkMatrix::kMSkewY) > 0 ? 0b01 : 0b00;
        } else {
            topNeg = 0b10;
            sameSign = matrix.get(SkMatrix::kMSkewY) > 0 ? 0b00 : 0b01;
        }
    }
    if (sameSign != antiDiag) {
        // This is a rotation (and maybe scale). The direction is unchanged.
        // Trust me on the start computation (or draw yourself some pictures)
        start = (inStart + 4 - (topNeg | antiDiag)) % 4;
        SkASSERT(start < 4);
        if (isRRect) {
            start = 2 * start + rm;
        }
    } else {
        // This is a mirror (and maybe scale). The direction is reversed.
        isCCW = !isCCW;
        // Trust me on the start computation (or draw yourself some pictures)
        start = (6 + (topNeg | antiDiag) - inStart) % 4;
        SkASSERT(start < 4);
        if (isRRect) {
            start = 2 * start + (rm ? 0 : 1);
        }
    }

    const DirAndStart mapped{
        isCCW ? SkPathDirection::kCCW : SkPathDirection::kCW,
        start
    };
    return mapped;
}

SkRRect SkPathPriv::DeduceRRectFromContour(const SkRect& bounds, SkSpan<const SkPoint> pts,
                                           SkSpan<const SkPathVerb> vbs) {
    SkASSERT(!vbs.empty());
    SkASSERT(vbs.front() == SkPathVerb::kMove);

    SkVector radii[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};

    size_t ptIndex = 0;
    for (const SkPathVerb verb : vbs) {
        switch (verb) {
            case SkPathVerb::kMove:
                SkASSERT(ptIndex == 0); // we only expect 1 move
                ptIndex += 1;
                break;
            case SkPathVerb::kLine: {
                // we only expect horizontal or vertical lines
                SkDEBUGCODE(const SkVector delta = pts[ptIndex] - pts[ptIndex-1];)
                SkASSERT(delta.fX == 0 || delta.fY == 0);
                ptIndex += 1;
            } break;
            case SkPathVerb::kQuad:  SkASSERT(false); break;
            case SkPathVerb::kCubic: SkASSERT(false); break;
            case SkPathVerb::kConic: {
                const SkVector v1_0 = pts[ptIndex] - pts[ptIndex - 1];
                const SkVector v2_1 = pts[ptIndex + 1] - pts[ptIndex];
                SkVector dxdy{0, 0};
                if (v1_0.fX) {
                    SkASSERT(!v2_1.fX && !v1_0.fY);
                    dxdy.set(SkScalarAbs(v1_0.fX), SkScalarAbs(v2_1.fY));
                } else if (!v1_0.fY) {
                    SkASSERT(!v2_1.fX || !v2_1.fY);
                    dxdy.set(SkScalarAbs(v2_1.fX), SkScalarAbs(v2_1.fY));
                } else {
                    SkASSERT(!v2_1.fY);
                    dxdy.set(SkScalarAbs(v2_1.fX), SkScalarAbs(v1_0.fY));
                }
                const SkRRect::Corner corner =
                    pts[ptIndex].fX == bounds.fLeft ?
                        pts[ptIndex].fY == bounds.fTop ?
                            SkRRect::kUpperLeft_Corner : SkRRect::kLowerLeft_Corner :
                        pts[ptIndex].fY == bounds.fTop ?
                            SkRRect::kUpperRight_Corner : SkRRect::kLowerRight_Corner;
                SkASSERT(!radii[corner].fX && !radii[corner].fY);
                radii[corner] = dxdy;
                ptIndex += 2;
            } break;
            case SkPathVerb::kClose:
                break;
        }
    }
    SkRRect rrect{};
    rrect.setRectRadii(bounds, radii);
    return rrect;
}

