/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkPathData.h"

#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSpan.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkTo.h"
#include "src/core/SkPathEnums.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPathRawShapes.h"
#include "src/core/SkPointPriv.h"
#include "src/core/SkSafeMath.h"
#include "src/core/SkSpanPriv.h"

#include <new>
#include <optional>
#include <type_traits>

// The shared empty path data; intentionally never released.
SkPathData* SkPathData::PeekEmptySingleton() {
    static SkPathData* gEmpty = SkPathData::MakeNoCheck({}, {}, {}, {}, {}).release();
    return gEmpty;
}

// Returns a nonzero identity whose top two bits are clear, leaving room for a fill type.
static uint32_t next_pathdata_unique_id() {
    constexpr int kHighBitsToMakeRoomForFillType = 2;

    static std::atomic<int32_t> nextID{1};

    uint32_t id = 0;
    do {
        id = static_cast<uint32_t>(nextID.fetch_add(1, std::memory_order_relaxed));
        // clear the high bits to make room for filltype
        id <<= kHighBitsToMakeRoomForFillType;
        id >>= kHighBitsToMakeRoomForFillType;
    } while (id == 0);
    return id;
}

// Size of one allocation holding SkPathData followed by its points, conic weights and verbs.
// Returns nullopt when the size is not representable.
static std::optional<size_t> path_data_allocation_size(size_t npts, size_t nvbs, size_t ncns) {
    SkSafeMath safe{};
    size_t total = sizeof(SkPathData);
    total = safe.add(total, safe.mul(npts, sizeof(SkPoint)));
    total = safe.add(total, safe.mul(ncns, sizeof(float)));
    total = safe.add(total, safe.mul(nvbs, sizeof(SkPathVerb)));
    if (!safe.ok()) {
        return {};
    }
    return total;
}

// Points following each verb, indexed by the verb's value.
static size_t points_per_verb(SkPathVerb verb) {
    switch (verb) {
        case SkPathVerb::kMove:  return 1;
        case SkPathVerb::kLine:  return 1;
        case SkPathVerb::kQuad:  return 2;
        case SkPathVerb::kConic: return 2;
        case SkPathVerb::kCubic: return 3;
        case SkPathVerb::kClose: return 0;
    }
    return 0;
}

static inline bool valid_conic_weight(float w) {
    const bool valid = w >= 0 && SkIsFinite(w);
    return valid;
}

// Returns true when the verbs form a valid sequence and the point and weight counts match it.
static bool valid_path_data(SkSpan<const SkPoint> pts,
                            SkSpan<const SkPathVerb> vbs,
                            SkSpan<const float> conics) {
    if (vbs.empty()) {
        const bool empty = pts.empty() && conics.empty();
        return empty;
    }

    // We must begin with a Move (unless we're empty)
    if (vbs.front() != SkPathVerb::kMove) {
        return false;
    }
    SkPathVerb prev = SkPathVerb::kMove;
    size_t point_count = 1;
    size_t conic_count = 0;

    // Check that we have a valid sequence.
    for (size_t i = 1; i < vbs.size(); ++i) {
        const SkPathVerb curr = vbs[i];

        if (static_cast<unsigned>(curr) > static_cast<unsigned>(SkPathVerb::kLast_Verb)) {
            return false;
        }

        // Previous verb             Valid next verb
        // -----------------------------------------
        // Move                  --> not Move
        // Line/Quad/Conic/Cubic --> *
        // Close                 --> Move
        //
        if (prev == SkPathVerb::kMove && curr == SkPathVerb::kMove) {
            return false;
        }
        if (prev == SkPathVerb::kClose && curr != SkPathVerb::kMove) {
            return false;
        }

        point_count += points_per_verb(curr);
        conic_count += (curr == SkPathVerb::kConic);

        prev = curr;
    }

    if (pts.size() != point_count || conics.size() != conic_count){
        return false;
    }

    for (const float w : conics) {
        if (!valid_conic_weight(w)) {
            return false;
        }
    }

    return true;
}

// This just sets-up the spans to point inside our allocation: the storage that Alloc reserved
// directly after this object holds npts points, then ncns weights, then nvbs verbs.
//
SkPathData::SkPathData(size_t npts, size_t nvbs, size_t ncns)
    : fUniqueID(next_pathdata_unique_id())
    , fConvexity(static_cast<uint8_t>(SkPathConvexity::kUnknown))
    , fType(SkPathIsAType::kGeneral)
{
    SkASSERT((npts == 0 && nvbs == 0 && ncns == 0) ||
             (npts != 0 && nvbs != 0));
    SkASSERT(path_data_allocation_size(npts, nvbs, ncns).has_value());

    if (nvbs == 0) {
        SkASSERT(fPoints.empty());
        SkASSERT(fVerbs.empty());
        SkASSERT(fConics.empty());
    } else {
        std::byte* data = reinterpret_cast<std::byte*>(this + 1);

        fPoints = SkSpan<SkPoint>(reinterpret_cast<SkPoint*>(data), npts);
        data += fPoints.size_bytes();

        fConics = SkSpan<float>(reinterpret_cast<float*>(data), ncns);
        data += fConics.size_bytes();

        fVerbs = SkSpan<SkPathVerb>(reinterpret_cast<SkPathVerb*>(data), nvbs);
    }

    // fBounds is initialized in finishInit()
}

SkPathData::~SkPathData() {
    // We will implicitly call our IDChangeList here, notifying them that we are
    // being dstroyed.
    SkDEBUGCODE(fUniqueID = 0xEEEEEEEE;)
}

// Releases the single allocation made by Alloc.
void SkPathData::operator delete(void* p) {
    ::operator delete(p);
}

void SkPathData::addGenIDChangeListener(sk_sp<SkIDChangeListener> listener) const {
    // our empty singleton is never deleted, so we don't want to add any listeners to it.
    if (this != SkPathData::PeekEmptySingleton()) {
        // this method on the list is thread-safe
        fGenIDChangeListeners.add(std::move(listener));
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////

// NOTE: This only allocates and initializes the span pointers (points, verbs),
//       it does NOT set the other fields. Returns null when the size is unrepresentable.
sk_sp<SkPathData> SkPathData::Alloc(size_t npts, size_t nvbs, size_t ncns) {
    const std::optional<size_t> size = path_data_allocation_size(npts, nvbs, ncns);
    if (!size.has_value()) {
        return nullptr;
    }
    // This trick allows us to just make one allocation, for us and our buffer
    // rather than allocating us and also allocating the buffer (via malloc or new[])
    // We have the corresponding operator delete() specified as well.
    void* storage = ::operator new (size.value());
    sk_sp<SkPathData> path(new (storage) SkPathData(npts, nvbs, ncns));
    return path;
}

bool SkPathData::finishInit(std::optional<SkRect> bounds, std::optional<uint8_t> segmentMask) {
    SkASSERT(valid_path_data(fPoints, fVerbs, fConics));

    if (fPoints.empty()) {
        fBounds = SkRect::MakeEmpty();
        fSegmentMask = 0;
        return true;
    }

    if (segmentMask.has_value()) {
        fSegmentMask = segmentMask.value();
    } else {
        fSegmentMask = SkPathPriv::ComputeSegmentMask(fVerbs);
    }

    if (bounds.has_value()) {
        fBounds = bounds.value().makeSorted();
        SkASSERT(SkPointPriv::AreFinite(fPoints.data(), SkToInt(fPoints.size())));
    } else {
        const std::optional<SkRect> r = SkPathPriv::TrimmedBounds(fPoints, fVerbs);
        if (!r.has_value()) {
            // non-finite bounds
            return false;
        }
        fBounds = r.value();
    }

    SkASSERT(fBounds.isSorted());
    return true;
}

sk_sp<SkPathData> SkPathData::MakeTransform(const SkPathRaw& src, const SkMatrix& mx) {
    if (src.empty()) {
        sk_sp<SkPathData> empty = SkPathData::Empty();
        return empty;
    }

    if (mx.hasPerspective()) {
        SkPathBuilder builder{};
        builder.addRaw(src, SkPathBuilder::Reserve::kExact);
        builder.transform(mx);
        sk_sp<SkPathData> projected = builder.detachData();
        return projected;
    }

    // Allocate our result, so we can map the new points directly into it
    sk_sp<SkPathData> result = Alloc(src.points().size(), src.verbs().size(),
                                     src.conics().size());
    if (!result) {
        return nullptr;
    }
    SkPathData& data = *result;
    mx.mapPoints(data.fPoints, src.points());
    SkSpanPriv::Copy(data.fConics, src.conics());
    SkSpanPriv::Copy(data.fVerbs,  src.verbs());

    std::optional<SkRect> transformedBounds{};
    if (mx.rectStaysRect()) {
        // safe us from having to compute our transformed bounds in finishInit()
        transformedBounds = mx.mapRect(src.bounds());
        if (!transformedBounds.value().isFinite()) {
            // transform created non-finite bounds
            return nullptr;
        }
    }

    if (!data.finishInit(transformedBounds, src.fSegmentMask)) {
        return nullptr;
    }

    data.setConvexity(SkPathPriv::TransformConvexity(mx, src.fPoints, src.fConvexity));

    return result;
}

sk_sp<SkPathData> SkPathData::makeTransform(const SkMatrix& mx) const {
    if (mx.isIdentity()) {
        sk_sp<SkPathData> same = sk_ref_sp(this);
        return same;
    }

    // not important for transform, just need a value
    const SkPathFillType ft = SkPathFillType::kDefault;

    sk_sp<SkPathData> result = MakeTransform(raw(ft, SkResolveConvexity::kNo), mx);
    if (!result) {
        return nullptr;
    }
    // See if we can maintian our IsA status ...
    if ((fType == SkPathIsAType::kOval || fType == SkPathIsAType::kRRect) &&
        mx.rectStaysRect() && SkPathPriv::IsAxisAligned(fPoints))
    {
        const SkPathPriv::DirAndStart mapped =
                SkPathPriv::TransformDirAndStart(mx, fType == SkPathIsAType::kRRect,
                                                 fIsA.fDirection, fIsA.fStartIndex);
        (*result).setupIsA(fType, mapped.fDirection, mapped.fStartIndex);
    }
    return result;
}

sk_sp<SkPathData> SkPathData::makeOffset(SkVector v) const {
    sk_sp<SkPathData> moved = makeTransform(SkMatrix::Translate(v));
    return moved;
}

bool operator==(const SkPathData& a, const SkPathData& b) {
    if (&a == &b) {
        return true;
    }

    const bool equal = SkSpanPriv::EQ(a.fPoints, b.fPoints) &&
                       SkSpanPriv::EQ(a.fConics, b.fConics) &&
                       SkSpanPriv::EQ(a.fVerbs,  b.fVerbs);
    return equal;
}

/////////////////////////////////////

// Returns null when the storage cannot be allocated or the points are not finite.
sk_sp<SkPathData> SkPathData::MakeNoCheck(SkSpan<const SkPoint> pts,
                                          SkSpan<const SkPathVerb> vbs,
                                          SkSpan<const float> conics,
                                          std::optional<SkRect> bounds,
                                          std::optional<unsigned> segmentMask) {
    SkASSERT(valid_path_data(pts, vbs, conics));

    sk_sp<SkPathData> path = Alloc(pts.size(), vbs.size(), conics.size());
    if (!path) {
        return nullptr;
    }
    SkPathData& data = *path;

    SkSpanPriv::Copy(data.fPoints, pts);
    SkSpanPriv::Copy(data.fConics, conics);
    SkSpanPriv::Copy(data.fVerbs,  vbs);

    std::optional<uint8_t> mask{};
    if (segmentMask.has_value()) {
        mask = SkTo<uint8_t>(segmentMask.value());
    }
    if (!data.finishInit(bounds, mask)) {
        return nullptr;
    }
    return path;
}

sk_sp<SkPathData> SkPathData::MakeNoCheck(const SkPathRaw& raw) {
    sk_sp<SkPathData> path = MakeNoCheck(raw.points(), raw.verbs(), raw.conics(), raw.fBounds,
                                         raw.fSegmentMask);
    return path;
}

sk_sp<SkPathData> SkPathData::Empty() {
    sk_sp<SkPathData> empty = sk_ref_sp(PeekEmptySingleton());
    return empty;
}

void SkPathData::setupIsA(SkPathIsAType type, SkPathDirection dir, unsigned index) {
    setConvexity(SkPathDirection_ToConvexity(dir));

    SkASSERT(type == SkPathIsAType::kOval || type == SkPathIsAType::kRRect);
    fType = type;

    SkASSERT((type == SkPathIsAType::kOval && index < 4) ||
             (type == SkPathIsAType::kRRect && index < 8));

    fIsA.fDirection  = dir;
    fIsA.fStartIndex = SkTo<uint8_t>(index);
}

sk_sp<SkPathData> SkPathData::Rect(const SkRect& r, SkPathDirection dir, unsigned index) {
    if (!r.isFinite()) {
        return nullptr;
    }
    const SkPathRawShapes::Rect raw(r, dir, index);
    sk_sp<SkPathData> path = MakeNoCheck(raw.points(), raw.verbs(), raw.conics(), raw.fBounds,
                                         raw.fSegmentMask);
    return path;
}

sk_sp<SkPathData> SkPathData::Oval(const SkRect& r, SkPathDirection dir, unsigned index) {
    if (!r.isFinite()) {
        return nullptr;
    }
    const SkPathRawShapes::Oval raw(r, dir, index);
    sk_sp<SkPathData> path = MakeNoCheck(raw.points(), raw.verbs(), raw.conics(), raw.fBounds,
                                         raw.fSegmentMask);
    if (!path) {
        return nullptr;
    }
    (*path).setupIsA(SkPathIsAType::kOval, dir, index);
    return path;
}

sk_sp<SkPathData> SkPathData::RRect(const SkRRect& r, SkPathDirection dir, unsigned index) {
    if (!r.isValid()) {
        return nullptr;
    }
    const SkPathRawShapes::RRect raw(r, dir, index);
    // we use Make, not MakeNoCheck, to confirm all points an conics are finite
    sk_sp<SkPathData> path = Make(raw.points(), raw.verbs(), raw.conics());
    if (!path) {
        return nullptr;
    }
    (*path).setupIsA(SkPathIsAType::kRRect, dir, index);
    return path;
}

sk_sp<SkPathData> SkPathData::Polygon(SkSpan<const SkPoint> pts, bool isClosed) {
    if (pts.size() == 0 || (pts.size() == 1 && !isClosed)) {
        sk_sp<SkPathData> empty = Empty();
        return empty;
    }

    const size_t nverbs = pts.size() + isClosed;    // +1 for the kClose verb
    const size_t nconics = 0;
    sk_sp<SkPathData> path = Alloc(pts.size(), nverbs, nconics);
    if (!path) {
        return nullptr;
    }
    SkPathData& data = *path;

    SkSpanPriv::Copy(data.fPoints, pts);

    data.fVerbs[0] = SkPathVerb::kMove;
    for (size_t i = 1; i < pts.size(); ++i) {
        data.fVerbs[i] = SkPathVerb::kLine;
    }
    if (isClosed) {
        data.fVerbs.back() = SkPathVerb::kClose;
    }

    if (!data.finishInit({}, kLine_SkPathSegmentMask)) {
        return nullptr;
    }
    return path;
}

/////////////////////////////////////

SkPathConvexity SkPathData::getConvexityOrUnknown() const {
    const SkPathConvexity convexity =
            static_cast<SkPathConvexity>(fConvexity.load(std::memory_order_relaxed));
    return convexity;
}

// Computes and caches convexity on first use. Concurrent first uses compute the same value;
// the cache is a relaxed atomic.
SkPathConvexity SkPathData::getResolvedConvexity() const {
    SkPathConvexity convexity = getConvexityOrUnknown();
    if (convexity == SkPathConvexity::kUnknown) {
        convexity = SkPathPriv::ComputeConvexity(fPoints, fVerbs, fConics);
        setConvexity(convexity);
    }
    return convexity;
}

void SkPathData::setConvexity(SkPathConvexity convexity) const {
    fConvexity.store(static_cast<uint8_t>(convexity), std::memory_order_relaxed);
}

bool SkPathData::isConvex() const {
    const bool convex = SkPathConvexity_IsConvex(getResolvedConvexity());
    return convex;
}

SkRect SkPathData::computeTightBounds() const {
    const SkRect bounds = SkPathPriv::ComputeTightBounds(points(), verbs(), conics());
    return bounds;
}

SkPathRaw SkPathData::raw(SkPathFillType ft, SkResolveConvexity rc) const {
    const SkPathConvexity convexity = rc == SkResolveConvexity::kYes ? getResolvedConvexity()
                                                                     : getConvexityOrUnknown();
    const SkPathRaw view{
        fPoints,
        fVerbs,
        fConics,
        fBounds,
        ft,
        convexity,
        fSegmentMask,
    };
    return view;
}

std::optional<std::array<SkPoint, 2>> SkPathData::asLine() const {
    if (fPoints.size() == 2 && fVerbs.size() == 2 && fConics.size() == 0 &&
        fVerbs[1] == SkPathVerb::kLine)
    {
        SkASSERT(fVerbs[0] == SkPathVerb::kMove);
        const std::array<SkPoint, 2> line{fPoints[0], fPoints[1]};
        return line;
    }
    return {};
}

std::optional<SkPathRectInfo> SkPathData::asRect() const {
    const std::optional<SkPathPriv::RectContour> rc =
            SkPathPriv::IsRectContour(fPoints, fVerbs, fSegmentMask, false);
    if (!rc.has_value()) {
        return {};
    }
    SkASSERT(rc.value().fRect == fBounds);
    const SkPathRectInfo info{
        fBounds,
        rc.value().fDirection,
        0, // start index???
    };
    return info;
}

std::optional<SkPathOvalInfo> SkPathData::asOval() const {
    if (fType != SkPathIsAType::kOval) {
        return {};
    }
    const SkPathOvalInfo info{
        fBounds,
        fIsA.fDirection,
        fIsA.fStartIndex,
    };
    return info;
}

std::optional<SkPathRRectInfo> SkPathData::asRRect() const {
    if (fType != SkPathIsAType::kRRect) {
        return {};
    }
    const SkPathRRectInfo info{
        SkPathPriv::DeduceRRectFromContour(fBounds, fPoints, fVerbs),
        fIsA.fDirection,
        fIsA.fStartIndex,
    };
    return info;
}

bool SkPathData::contains(SkPoint p, SkPathFillType ft) const {
    const bool inside = SkPathPriv::Contains(raw(ft, SkResolveConvexity::kNo), p);
    return inside;
}

/////////////////

sk_sp<SkPathData> SkPathData::Make(SkSpan<const SkPoint> pts,
                                   SkSpan<const SkPathVerb> vbs,
                                   SkSpan<const float> conics) {
    if (!valid_path_data(pts, vbs, conics)) {
        // invalid path data
        return nullptr;
    }

    // MakeNoCheck *does* compute/check bounds if we don't pass them in
    sk_sp<SkPathData> path = MakeNoCheck(pts, vbs, conics, {}, {});
    return path;
}
