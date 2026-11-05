/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkStroke_DEFINED
#define SkStroke_DEFINED

#include "include/core/SkPaint.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkScalar.h"
#include "include/private/SkAssert.h"
#include "include/private/SkDebug.h"
#include "include/private/SkTo.h"

#include <cmath>
#include <cstdint>

class SkPath;
class SkPathBuilder;
struct SkRect;

/** \class SkStroke
    SkStroke is the utility class that constructs paths by stroking
    geometries (lines, rects, ovals, roundrects, paths). This is
    invoked when a geometry or text is drawn in a canvas with the
    kStroke_Mask bit set in the paint.
*/
class SkStroke {
public:
    SkStroke();
    explicit SkStroke(const SkPaint&);
    SkStroke(const SkPaint&, SkScalar width);   // width overrides paint.getStrokeWidth()

    SkPaint::Cap getCap() const {
        const SkPaint::Cap cap = static_cast<SkPaint::Cap>(fCap);
        return cap;
    }
    void         setCap(SkPaint::Cap);

    SkPaint::Join getJoin() const {
        const SkPaint::Join join = static_cast<SkPaint::Join>(fJoin);
        return join;
    }
    void          setJoin(SkPaint::Join);

    void    setMiterLimit(SkScalar);
    void    setWidth(SkScalar);

    bool    getDoFill() const { return fDoFill; }
    void    setDoFill(bool doFill) { fDoFill = doFill; }

    /**
     *  ResScale is the "intended" resolution for the output.
     *      Default is 1.0.
     *      Larger values (res > 1) indicate that the result should be more precise, since it will
     *          be zoomed up, and small errors will be magnified.
     *      Smaller values (0 < res < 1) indicate that the result can be less precise, since it will
     *          be zoomed down, and small errors may be invisible.
     */
    SkScalar getResScale() const { return fResScale; }
    void setResScale(SkScalar rs) {
        SkASSERT(rs > 0 && std::isfinite(rs));
        fResScale = rs;
    }

    /**
     *  Stroke the specified rect, winding it in the specified direction..
     */
    void    strokeRect(const SkRect& rect, SkPathBuilder* result,
                       SkPathDirection = SkPathDirection::kCW) const;
    void    strokePath(const SkPath& path, SkPathBuilder*) const;

    ////////////////////////////////////////////////////////////////

private:
    SkScalar    fWidth = 0;
    SkScalar    fMiterLimit = 0;
    SkScalar    fResScale = 1;
    uint8_t     fCap = 0;
    uint8_t     fJoin = 0;
    bool        fDoFill = false;

    friend class SkPaint;
};

#endif
