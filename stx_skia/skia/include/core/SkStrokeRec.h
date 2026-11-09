/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkStrokeRec_DEFINED
#define SkStrokeRec_DEFINED

#include "include/core/SkPaint.h"
#include "include/core/SkScalar.h"
#include "include/core/SkTypes.h"
#include "include/private/SkMacros.h"

#include <cmath>
#include <cstdint>

class SkPath;
class SkPathBuilder;

SK_BEGIN_REQUIRE_DENSE
class SK_API SkStrokeRec {
public:
    enum InitStyle {
        kHairline_InitStyle,
        kFill_InitStyle
    };
    explicit SkStrokeRec(InitStyle style);
    SkStrokeRec(const SkPaint&, SkPaint::Style, SkScalar resScale = 1);
    explicit SkStrokeRec(const SkPaint&, SkScalar resScale = 1);

    enum Style {
        kHairline_Style,
        kFill_Style,
        kStroke_Style,
        kStrokeAndFill_Style
    };

    static constexpr int kStyleCount = kStrokeAndFill_Style + 1;

    Style getStyle() const;
    SkScalar getWidth() const { return fWidth; }
    SkScalar getMiter() const { return fMiterLimit; }
    SkPaint::Cap getCap() const {
        const SkPaint::Cap cap = static_cast<SkPaint::Cap>(fCap);
        return cap;
    }
    SkPaint::Join getJoin() const {
        const SkPaint::Join join = static_cast<SkPaint::Join>(fJoin);
        return join;
    }

    bool isHairlineStyle() const {
        const bool hairline = kHairline_Style == getStyle();
        return hairline;
    }

    bool isFillStyle() const {
        const bool fill = kFill_Style == getStyle();
        return fill;
    }

    void setFillStyle();
    void setHairlineStyle();
    /**
     *  Specify the strokewidth, and optionally if you want stroke + fill.
     *  Note, if width==0, then this request is taken to mean:
     *      strokeAndFill==true -> new style will be Fill
     *      strokeAndFill==false -> new style will be Hairline
     */
    void setStrokeStyle(SkScalar width, bool strokeAndFill = false);

    void setStrokeParams(SkPaint::Cap cap, SkPaint::Join join, SkScalar miterLimit) {
        fCap = static_cast<uint32_t>(cap);
        fJoin = static_cast<uint32_t>(join);
        fMiterLimit = miterLimit;
    }

    SkScalar getResScale() const {
        return fResScale;
    }

    void setResScale(SkScalar rs) {
        SkASSERT(rs > 0 && std::isfinite(rs));
        fResScale = rs;
    }

    /**
     *  Returns true if this specifes any thick stroking, i.e. applyToPath()
     *  will return true.
     */
    bool needToApply() const {
        const Style style = getStyle();
        const bool apply = (kStroke_Style == style) || (kStrokeAndFill_Style == style);
        return apply;
    }

    /**
     *  Apply these stroke parameters to the src path, returning the result
     *  in dst.
     *
     *  If there was no change (i.e. style == hairline or fill) this returns
     *  false and dst is unchanged. Otherwise returns true and the result is
     *  stored in dst.
     *
     *  src and dst may be the same path.
     */
    bool applyToPath(SkPathBuilder* dst, const SkPath& src) const;

    /**
     *  Apply these stroke parameters to a paint.
     */
    void applyToPaint(SkPaint* paint) const;

    /**
     * Gives a conservative value for the outset that should applied to a
     * geometries bounds to account for any inflation due to applying this
     * strokeRec to the geometry.
     */
    SkScalar getInflationRadius() const;

    /**
     * Equivalent to:
     *   SkStrokeRec rec(paint, style);
     *   rec.getInflationRadius();
     * This does not account for other effects on the paint (i.e. path
     * effect).
     */
    static SkScalar GetInflationRadius(const SkPaint&, SkPaint::Style);

    static SkScalar GetInflationRadius(SkPaint::Join, SkScalar miterLimit, SkPaint::Cap,
                                       SkScalar strokeWidth);

    /**
     * Compare if two SkStrokeRecs have an equal effect on a path.
     * Equal SkStrokeRecs produce equal paths. Equality of produced
     * paths does not take the ResScale parameter into account.
     */
    bool hasEqualEffect(const SkStrokeRec& other) const {
        if (!needToApply()) {
            const bool sameStyle = getStyle() == other.getStyle();
            return sameStyle;
        }
        const bool equal = fWidth == other.fWidth &&
                           (fJoin != SkPaint::kMiter_Join || fMiterLimit == other.fMiterLimit) &&
                           fCap == other.fCap &&
                           fJoin == other.fJoin &&
                           fStrokeAndFill == other.fStrokeAndFill;
        return equal;
    }

private:
    void init(const SkPaint&, SkPaint::Style, SkScalar resScale);

    SkScalar        fResScale = 1;
    SkScalar        fWidth = 0;
    SkScalar        fMiterLimit = 0;
    // The following three members are packed together into a single u32.
    // This is to avoid unnecessary padding and ensure binary equality for
    // hashing (because the padded areas might contain garbage values).
    //
    // fCap and fJoin are larger than needed to avoid having to initialize
    // any pad values
    uint32_t        fCap : 16 = 0;             // SkPaint::Cap
    uint32_t        fJoin : 15 = 0;            // SkPaint::Join
    uint32_t        fStrokeAndFill : 1 = 0;    // bool
};
SK_END_REQUIRE_DENSE

#endif
