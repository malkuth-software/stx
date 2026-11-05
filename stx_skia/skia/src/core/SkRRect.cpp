/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkRRect.h"

#include "include/core/SkMatrix.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkScalar.h"
#include "include/core/SkString.h"
#include "include/core/SkTypes.h"
#include "include/private/SkDebug.h"
#include "include/private/SkFloatingPoint.h"
#include "src/core/SkBuffer.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPathRawShapes.h"
#include "src/core/SkRRectPriv.h"
#include "src/core/SkRectPriv.h"
#include "src/core/SkScaleToSides.h"
#include "src/core/SkStringUtils.h"
#include "src/utils/SkFloatUtils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>

///////////////////////////////////////////////////////////////////////////////

void SkRRect::setOval(const SkRect& oval) {
    if (!initializeRect(oval)) {
        return;
    }

    const SkScalar xRad = SkRectPriv::HalfWidth(fRect);
    const SkScalar yRad = SkRectPriv::HalfHeight(fRect);

    if (xRad == 0.0f || yRad == 0.0f) {
        // All the corners will be square
        clearRadii();
        fType = kRect_Type;
    } else {
        for (int i = 0; i < 4; ++i) {
            fRadii[i].set(xRad, yRad);
        }
        fType = kOval_Type;
    }

    SkASSERT(isValid());
}

void SkRRect::setRectXY(const SkRect& rect, SkScalar xRad, SkScalar yRad) {
    if (!initializeRect(rect)) {
        return;
    }

    if (!SkIsFinite(xRad, yRad)) {
        // devolve into a simple rect
        xRad = 0;
        yRad = 0;
    }

    if (fRect.width() < xRad+xRad || fRect.height() < yRad+yRad) {
        // At most one of these two divides will be by zero, and neither numerator is zero.
        const float scale = std::min(sk_ieee_float_divide(fRect.width(), xRad + xRad),
                                     sk_ieee_float_divide(fRect.height(), yRad + yRad));
        SkASSERT(scale < SK_Scalar1);
        xRad *= scale;
        yRad *= scale;
    }

    if (xRad <= 0 || yRad <= 0) {
        // all corners are square in this case
        setRect(rect);
        return;
    }

    for (int i = 0; i < 4; ++i) {
        fRadii[i].set(xRad, yRad);
    }
    fType = kSimple_Type;
    if (xRad >= (fRect.width() / 2.f) && yRad >= (fRect.height() / 2.f)) {
        fType = kOval_Type;
        xRad = SkRectPriv::HalfWidth(fRect);
        yRad = SkRectPriv::HalfHeight(fRect);
        for (int i = 0; i < 4; ++i) {
            fRadii[i].set(xRad, yRad);
        }
    }

    SkASSERT(isValid());
}

// Clamps each corner with a non-positive radius to (0, 0). Returns true if every
// corner is then square.
static bool clamp_to_zero(SkVector radii[4]) {
    bool allCornersSquare = true;

    // Clamp negative radii to zero
    for (int i = 0; i < 4; ++i) {
        if (radii[i].fX <= 0 || radii[i].fY <= 0) {
            // In this case we are being a little fast & loose. Since one of
            // the radii is 0 the corner is square. However, the other radii
            // could still be non-zero and play in the global scale factor
            // computation.
            radii[i].fX = 0;
            radii[i].fY = 0;
        } else {
            allCornersSquare = false;
        }
    }

    return allCornersSquare;
}

static bool radii_are_nine_patch(const SkVector radii[4]) {
    const bool ninePatch =
            radii[SkRRect::kUpperLeft_Corner].fX == radii[SkRRect::kLowerLeft_Corner].fX &&
            radii[SkRRect::kUpperLeft_Corner].fY == radii[SkRRect::kUpperRight_Corner].fY &&
            radii[SkRRect::kUpperRight_Corner].fX == radii[SkRRect::kLowerRight_Corner].fX &&
            radii[SkRRect::kLowerLeft_Corner].fY == radii[SkRRect::kLowerRight_Corner].fY;
    return ninePatch;
}

void SkRRect::setNinePatch(const SkRect& rect, SkScalar leftRad, SkScalar topRad,
                           SkScalar rightRad, SkScalar bottomRad) {
    if (!initializeRect(rect)) {
        return;
    }

    if (!SkIsFinite(leftRad, topRad, rightRad, bottomRad)) {
        setRect(rect);    // devolve into a simple rect
        return;
    }

    leftRad = std::max(leftRad, 0.0f);
    topRad = std::max(topRad, 0.0f);
    rightRad = std::max(rightRad, 0.0f);
    bottomRad = std::max(bottomRad, 0.0f);

    float scale = 1.0f;
    if (leftRad + rightRad > fRect.width()) {
        scale = fRect.width() / (leftRad + rightRad);
    }
    if (topRad + bottomRad > fRect.height()) {
        scale = std::min(scale, fRect.height() / (topRad + bottomRad));
    }

    if (scale < 1.0f) {
        leftRad *= scale;
        topRad *= scale;
        rightRad *= scale;
        bottomRad *= scale;
    }

    if (leftRad == rightRad && topRad == bottomRad) {
        if (leftRad >= (fRect.width() / 2.f) && topRad >= (fRect.height() / 2.f)) {
            fType = kOval_Type;
            leftRad = SkRectPriv::HalfWidth(fRect);
            rightRad = leftRad;
            topRad = SkRectPriv::HalfHeight(fRect);
            bottomRad = topRad;
        } else if (0 == leftRad || 0 == topRad) {
            // If the left and (by equality check above) right radii are zero then it is a rect.
            // Same goes for top/bottom.
            fType = kRect_Type;
            leftRad = 0;
            topRad = 0;
            rightRad = 0;
            bottomRad = 0;
        } else {
            fType = kSimple_Type;
        }
    } else {
        fType = kNinePatch_Type;
    }

    fRadii[kUpperLeft_Corner].set(leftRad, topRad);
    fRadii[kUpperRight_Corner].set(rightRad, topRad);
    fRadii[kLowerRight_Corner].set(rightRad, bottomRad);
    fRadii[kLowerLeft_Corner].set(leftRad, bottomRad);
    if (clamp_to_zero(fRadii)) {
        setRect(rect);    // devolve into a simple rect
        return;
    }
    if (fType == kNinePatch_Type && !radii_are_nine_patch(fRadii)) {
        fType = kComplex_Type;
    }

    SkASSERT(isValid());
}

// These parameters intentionally double. Apropos crbug.com/463920, if one of the
// radii is huge while the other is small, single precision math can completely
// miss the fact that a scale is required.
static double compute_min_scale(double rad1, double rad2, double limit, double curMin) {
    if ((rad1 + rad2) > limit) {
        const double scale = std::min(curMin, limit / (rad1 + rad2));
        return scale;
    }
    return curMin;
}

void SkRRect::setRectRadii(const SkRect& rect, const SkVector radii[4]) {
    if (!initializeRect(rect)) {
        return;
    }

    for (int i = 0; i < 4; ++i) {
        if (!radii[i].isFinite()) {
            setRect(rect);    // devolve into a simple rect
            return;
        }
    }

    // radii may address fRadii; copying an element onto itself is harmless.
    for (int i = 0; i < 4; ++i) {
        fRadii[i] = radii[i];
    }

    if (clamp_to_zero(fRadii)) {
        setRect(rect);
        return;
    }

    scaleRadii();

    if (!isValid()) {
        setRect(rect);
        return;
    }
}

bool SkRRect::initializeRect(const SkRect& rect) {
    // Check this before sorting because sorting can hide nans.
    if (!rect.isFinite()) {
        *this = SkRRect();
        return false;
    }
    fRect = rect.makeSorted();
    if (fRect.isEmpty()) {
        clearRadii();
        fType = kEmpty_Type;
        return false;
    }
    return true;
}

// If we can't distinguish one of the radii relative to the other, force it to zero so it
// doesn't confuse us later. See crbug.com/850350
//
static void flush_to_zero(SkScalar& a, SkScalar& b) {
    SkASSERT(a >= 0);
    SkASSERT(b >= 0);
    if (a + b == a) {
        b = 0;
    } else if (a + b == b) {
        a = 0;
    }
}

bool SkRRect::scaleRadii() {
    // Proportionally scale down all radii to fit. Find the minimum ratio
    // of a side and the radii on that side (for all four sides) and use
    // that to scale down _all_ the radii. This algorithm is from the
    // W3 spec (http://www.w3.org/TR/css3-background/) section 5.5 - Overlapping
    // Curves:
    // "Let f = min(Li/Si), where i is one of { top, right, bottom, left },
    //   Si is the sum of the two corresponding radii of the corners on side i,
    //   and Ltop = Lbottom = the width of the box,
    //   and Lleft = Lright = the height of the box.
    // If f < 1, then all corner radii are reduced by multiplying them by f."
    double scale = 1.0;

    // The sides of the rectangle may be larger than a float.
    const double width = static_cast<double>(fRect.fRight) - static_cast<double>(fRect.fLeft);
    const double height = static_cast<double>(fRect.fBottom) - static_cast<double>(fRect.fTop);
    scale = compute_min_scale(fRadii[0].fX, fRadii[1].fX, width,  scale);
    scale = compute_min_scale(fRadii[1].fY, fRadii[2].fY, height, scale);
    scale = compute_min_scale(fRadii[2].fX, fRadii[3].fX, width,  scale);
    scale = compute_min_scale(fRadii[3].fY, fRadii[0].fY, height, scale);

    flush_to_zero(fRadii[0].fX, fRadii[1].fX);
    flush_to_zero(fRadii[1].fY, fRadii[2].fY);
    flush_to_zero(fRadii[2].fX, fRadii[3].fX);
    flush_to_zero(fRadii[3].fY, fRadii[0].fY);

    if (scale < 1.0) {
        SkScaleToSides::AdjustRadii(width,  scale, &fRadii[0].fX, &fRadii[1].fX);
        SkScaleToSides::AdjustRadii(height, scale, &fRadii[1].fY, &fRadii[2].fY);
        SkScaleToSides::AdjustRadii(width,  scale, &fRadii[2].fX, &fRadii[3].fX);
        SkScaleToSides::AdjustRadii(height, scale, &fRadii[3].fY, &fRadii[0].fY);
    }

    // adjust radii may set x or y to zero; set companion to zero as well
    clamp_to_zero(fRadii);

    // May be simple, oval, or complex, or become a rect/empty if the radii adjustment made them 0
    computeType();

    const bool scaled = scale < 1.0;
    return scaled;
}

// This method determines if a point known to be inside the RRect's bounds is
// inside all the corners.
bool SkRRect::checkCornerContainment(SkScalar x, SkScalar y) const {
    SkPoint canonicalPt{0, 0}; // (x,y) translated to one of the quadrants
    int index = kUpperLeft_Corner;

    if (kOval_Type == type()) {
        canonicalPt.set(x - fRect.centerX(), y - fRect.centerY());
        index = kUpperLeft_Corner;  // any corner will do in this case
    } else {
        if (x < fRect.fLeft + fRadii[kUpperLeft_Corner].fX &&
            y < fRect.fTop + fRadii[kUpperLeft_Corner].fY) {
            // UL corner
            index = kUpperLeft_Corner;
            canonicalPt.set(x - (fRect.fLeft + fRadii[kUpperLeft_Corner].fX),
                            y - (fRect.fTop + fRadii[kUpperLeft_Corner].fY));
            SkASSERT(canonicalPt.fX < 0 && canonicalPt.fY < 0);
        } else if (x < fRect.fLeft + fRadii[kLowerLeft_Corner].fX &&
                   y > fRect.fBottom - fRadii[kLowerLeft_Corner].fY) {
            // LL corner
            index = kLowerLeft_Corner;
            canonicalPt.set(x - (fRect.fLeft + fRadii[kLowerLeft_Corner].fX),
                            y - (fRect.fBottom - fRadii[kLowerLeft_Corner].fY));
            SkASSERT(canonicalPt.fX < 0 && canonicalPt.fY > 0);
        } else if (x > fRect.fRight - fRadii[kUpperRight_Corner].fX &&
                   y < fRect.fTop + fRadii[kUpperRight_Corner].fY) {
            // UR corner
            index = kUpperRight_Corner;
            canonicalPt.set(x - (fRect.fRight - fRadii[kUpperRight_Corner].fX),
                            y - (fRect.fTop + fRadii[kUpperRight_Corner].fY));
            SkASSERT(canonicalPt.fX > 0 && canonicalPt.fY < 0);
        } else if (x > fRect.fRight - fRadii[kLowerRight_Corner].fX &&
                   y > fRect.fBottom - fRadii[kLowerRight_Corner].fY) {
            // LR corner
            index = kLowerRight_Corner;
            canonicalPt.set(x - (fRect.fRight - fRadii[kLowerRight_Corner].fX),
                            y - (fRect.fBottom - fRadii[kLowerRight_Corner].fY));
            SkASSERT(canonicalPt.fX > 0 && canonicalPt.fY > 0);
        } else {
            // not in any of the corners
            return true;
        }
    }

    // A point is in an ellipse (in standard position) if:
    //      x^2     y^2
    //     ----- + ----- <= 1
    //      a^2     b^2
    // or :
    //     b^2*x^2 + a^2*y^2 <= (ab)^2
    const float dist =  SkScalarSquare(canonicalPt.fX) * SkScalarSquare(fRadii[index].fY) +
                        SkScalarSquare(canonicalPt.fY) * SkScalarSquare(fRadii[index].fX);
    const bool inside = dist <= SkScalarSquare(fRadii[index].fX * fRadii[index].fY);
    return inside;
}

bool SkRRect::contains(const SkPoint& point) const {
    if (!fRect.contains(point.fX, point.fY)) {
        // If 'point' isn't contained by the RR's bounds then the RR definitely
        // doesn't contain it.
        return false;
    }

    if (isRect()) {
        // The prior test was sufficient.
        return true;
    }

    // At this point we know `point` is inside the bounds of this RR. Check to
    // see it is inside all the curves.
    const bool inside = checkCornerContainment(point.fX, point.fY);
    return inside;
}

bool SkRRect::contains(const SkRect& rect) const {
    if (!fRect.contains(rect)) {
        // If 'rect' isn't contained by the RR's bounds then the
        // RR definitely doesn't contain it
        return false;
    }

    if (isRect()) {
        // the prior test was sufficient
        return true;
    }

    // At this point we know all four corners of 'rect' are inside the
    // bounds of of this RR. Check to make sure all the corners are inside
    // all the curves
    const bool inside = checkCornerContainment(rect.fLeft, rect.fTop) &&
                        checkCornerContainment(rect.fRight, rect.fTop) &&
                        checkCornerContainment(rect.fRight, rect.fBottom) &&
                        checkCornerContainment(rect.fLeft, rect.fBottom);
    return inside;
}

// There is a simplified version of this method in setRectXY
void SkRRect::computeType() {
    if (fRect.isEmpty()) {
        SkASSERT(fRect.isSorted());
        for (size_t i = 0; i < std::size(fRadii); ++i) {
            SkASSERT((fRadii[i] == SkVector{0, 0}));
        }
        fType = kEmpty_Type;
        SkASSERT(isValid());
        return;
    }

    bool allRadiiEqual = true; // are all x radii equal and all y radii?
    bool allCornersSquare = 0 == fRadii[0].fX || 0 == fRadii[0].fY;

    for (int i = 1; i < 4; ++i) {
        if (0 != fRadii[i].fX && 0 != fRadii[i].fY) {
            // if either radius is zero the corner is square so both have to
            // be non-zero to have a rounded corner
            allCornersSquare = false;
        }
        if (fRadii[i].fX != fRadii[i-1].fX || fRadii[i].fY != fRadii[i-1].fY) {
            allRadiiEqual = false;
        }
    }

    if (allCornersSquare) {
        fType = kRect_Type;
        SkASSERT(isValid());
        return;
    }

    if (allRadiiEqual) {
        if (fRadii[0].fX >= (fRect.width() / 2.f) && fRadii[0].fY >= (fRect.height() / 2.f)) {
            fType = kOval_Type;
            for (int i = 0; i < 4; ++i) {
                fRadii[i].set(SkRectPriv::HalfWidth(fRect), SkRectPriv::HalfHeight(fRect));
            }
        } else {
            fType = kSimple_Type;
        }
        SkASSERT(isValid());
        return;
    }

    if (radii_are_nine_patch(fRadii)) {
        fType = kNinePatch_Type;
    } else {
        fType = kComplex_Type;
    }

    if (!isValid()) {
        const SkRect bounds = fRect;
        setRect(bounds);
        SkASSERT(isValid());
    }
}

std::optional<SkRRect> SkRRect::transform(const SkMatrix& matrix) const {
    if (matrix.isIdentity()) {
        return *this;
    }

    if (!matrix.preservesAxisAlignment()) {
        return {};
    }

    const SkRect newRect = matrix.mapRect(fRect);
    if (!newRect.isFinite()) {
        return {};
    }

    switch (getType()) {
        case kEmpty_Type: {
            const SkRRect empty = MakeEmpty();
            return empty;
        }
        case kRect_Type: {
            const SkRRect mappedRect = MakeRect(newRect);
            return mappedRect;
        }
        case kOval_Type: {
            const SkRRect mappedOval = MakeOval(newRect);
            return mappedOval;
        }
        case kSimple_Type:
        case kNinePatch_Type:
        case kComplex_Type:
            break;
    }

    // Map the rounded rectangle's contour and recover its corners.
    SkPathRawShapes::RRect raw(*this);
    matrix.mapPoints(raw.fStorage);
    const std::optional<SkRRect> mapped =
            SkPathPriv::DeduceRRectFromContour(newRect, raw.fPoints, raw.fVerbs);
    return mapped;
}

bool SkRRect::transform(const SkMatrix& matrix, SkRRect* dst) const {
    const std::optional<SkRRect> mapped = transform(matrix);
    if (!mapped.has_value()) {
        return false;
    }
    if (dst) {
        *dst = mapped.value();
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////

void SkRRect::inset(SkScalar dx, SkScalar dy, SkRRect* dst) const {
    // dst may be this: the bounds and radii are read before dst is written.
    SkRect r = fRect.makeInset(dx, dy);
    bool degenerate = false;
    if (r.fRight <= r.fLeft) {
        degenerate = true;
        const float middle = sk_float_midpoint(r.fLeft, r.fRight);
        r.fLeft = middle;
        r.fRight = middle;
    }
    if (r.fBottom <= r.fTop) {
        degenerate = true;
        const float middle = sk_float_midpoint(r.fTop, r.fBottom);
        r.fTop = middle;
        r.fBottom = middle;
    }
    if (degenerate) {
        (*dst).fRect = r;
        (*dst).clearRadii();
        (*dst).fType = kEmpty_Type;
        return;
    }
    if (!r.isFinite()) {
        *dst = SkRRect();
        return;
    }

    SkVector radii[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    for (int i = 0; i < 4; ++i) {
        radii[i] = fRadii[i];
        if (radii[i].fX) {
            radii[i].fX -= dx;
        }
        if (radii[i].fY) {
            radii[i].fY -= dy;
        }
    }
    (*dst).setRectRadii(r, radii);
}

///////////////////////////////////////////////////////////////////////////////

// The serialized form is fRect followed by fRadii; the derived type tag is not stored.
static_assert(sizeof(SkRect) + 4 * sizeof(SkVector) == SkRRect::kSizeInMemory);

size_t SkRRect::writeToMemory(void* buffer) const {
    uint8_t* bytes = static_cast<uint8_t*>(buffer);
    std::memcpy(bytes, &fRect, sizeof(fRect));
    std::memcpy(bytes + sizeof(fRect), fRadii, sizeof(fRadii));
    return kSizeInMemory;
}

void SkRRectPriv::WriteToBuffer(const SkRRect& rr, SkWBuffer* buffer) {
    uint8_t bytes[SkRRect::kSizeInMemory] = {};
    const size_t written = rr.writeToMemory(bytes);
    (*buffer).write(bytes, written);
}

size_t SkRRect::readFromMemory(const void* buffer, size_t length) {
    if (length < kSizeInMemory) {
        return 0;
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(buffer);
    SkRect rect{};
    SkVector radii[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    std::memcpy(&rect, bytes, sizeof(rect));
    std::memcpy(radii, bytes + sizeof(rect), sizeof(radii));
    setRectRadii(rect, radii);
    return kSizeInMemory;
}

bool SkRRectPriv::ReadFromBuffer(SkRBuffer* buffer, SkRRect* rr) {
    if ((*buffer).available() < SkRRect::kSizeInMemory) {
        return false;
    }
    uint8_t bytes[SkRRect::kSizeInMemory] = {};
    if (!(*buffer).read(bytes, SkRRect::kSizeInMemory)) {
        return false;
    }
    const size_t consumed = (*rr).readFromMemory(bytes, SkRRect::kSizeInMemory);
    const bool complete = consumed == SkRRect::kSizeInMemory;
    return complete;
}

SkString SkRRect::dumpToString(bool asHex) const {
    const SkScalarAsStringType asType =
            asHex ? kHex_SkScalarAsStringType : kDec_SkScalarAsStringType;

    SkString line = fRect.dumpToString(asHex);
    line.appendf("\nconst SkPoint corners[] = {\n");
    for (int i = 0; i < 4; ++i) {
        SkString strX{};
        SkString strY{};
        SkAppendScalar(&strX, fRadii[i].x(), asType);
        SkAppendScalar(&strY, fRadii[i].y(), asType);
        line.appendf("    { %s, %s },", strX.c_str(), strY.c_str());
        if (asHex) {
            line.appendf(" /* %f %f */", fRadii[i].x(), fRadii[i].y());
        }
        line.append("\n");
    }
    line.append("};");
    return line;
}

void SkRRect::dump(bool asHex) const {
    const SkString text = dumpToString(asHex);
    SkDebugf("%s\n", text.c_str());
}

///////////////////////////////////////////////////////////////////////////////

/**
 *  We need all combinations of predicates to be true to have a "safe" radius value.
 */
static bool are_radius_check_predicates_valid(SkScalar rad, SkScalar min, SkScalar max) {
    const bool valid = (min <= max) && (rad <= max - min) && (min + rad <= max) &&
                       (max - rad >= min) && rad >= 0;
    return valid;
}

bool SkRRect::isValid() const {
    if (!AreRectAndRadiiValid(fRect, fRadii)) {
        return false;
    }

    bool allRadiiZero = (0 == fRadii[0].fX && 0 == fRadii[0].fY);
    bool allCornersSquare = (0 == fRadii[0].fX || 0 == fRadii[0].fY);
    bool allRadiiSame = true;

    for (int i = 1; i < 4; ++i) {
        if (0 != fRadii[i].fX || 0 != fRadii[i].fY) {
            allRadiiZero = false;
        }

        if (fRadii[i].fX != fRadii[i-1].fX || fRadii[i].fY != fRadii[i-1].fY) {
            allRadiiSame = false;
        }

        if (0 != fRadii[i].fX && 0 != fRadii[i].fY) {
            allCornersSquare = false;
        }
    }
    const bool patchesOfNine = radii_are_nine_patch(fRadii);

    switch (fType) {
        case kEmpty_Type:
            if (!fRect.isEmpty() || !allRadiiZero || !allRadiiSame || !allCornersSquare) {
                return false;
            }
            return true;
        case kRect_Type:
            if (fRect.isEmpty() || !allRadiiZero || !allRadiiSame || !allCornersSquare) {
                return false;
            }
            return true;
        case kOval_Type: {
            if (fRect.isEmpty() || allRadiiZero || !allRadiiSame || allCornersSquare) {
                return false;
            }

            const float desiredWidthVal = SkRectPriv::HalfWidth(fRect);
            const float desiredHeightVal = SkRectPriv::HalfHeight(fRect);
            const SkFloatingPoint<float, 4> desiredWidth(desiredWidthVal);
            const SkFloatingPoint<float, 4> desiredHeight(desiredHeightVal);

            for (int i = 0; i < 4; ++i) {
                const SkFloatingPoint<float, 4> x(fRadii[i].fX);
                const SkFloatingPoint<float, 4> y(fRadii[i].fY);
                const bool xMatches = SkScalarNearlyEqual(fRadii[i].fX, desiredWidthVal) ||
                                      x.AlmostEquals(desiredWidth);
                const bool yMatches = SkScalarNearlyEqual(fRadii[i].fY, desiredHeightVal) ||
                                      y.AlmostEquals(desiredHeight);
                if (!xMatches || !yMatches) {
                    return false;
                }
            }
            return true;
        }
        case kSimple_Type:
            if (fRect.isEmpty() || allRadiiZero || !allRadiiSame || allCornersSquare) {
                return false;
            }
            return true;
        case kNinePatch_Type:
            if (fRect.isEmpty() || allRadiiZero || allRadiiSame || allCornersSquare ||
                !patchesOfNine) {
                return false;
            }
            return true;
        case kComplex_Type:
            if (fRect.isEmpty() || allRadiiZero || allRadiiSame || allCornersSquare ||
                patchesOfNine) {
                return false;
            }
            return true;
        default:
            // A type tag outside the enumeration is never valid.
            return false;
    }
}

bool SkRRect::AreRectAndRadiiValid(const SkRect& rect, const SkVector radii[4]) {
    if (!rect.isFinite() || !rect.isSorted()) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (!are_radius_check_predicates_valid(radii[i].fX, rect.fLeft, rect.fRight) ||
            !are_radius_check_predicates_valid(radii[i].fY, rect.fTop, rect.fBottom)) {
            return false;
        }
    }
    return true;
}
