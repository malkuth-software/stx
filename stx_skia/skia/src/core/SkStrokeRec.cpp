/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkStrokeRec.h"

#include "src/core/SkPaintDefaults.h"
#include "src/core/SkStroke.h"

#include <algorithm>

// must be < 0, since ==0 means hairline, and >0 means normal stroke
static constexpr SkScalar kStrokeRec_FillStyleWidth = -SK_Scalar1;

SkStrokeRec::SkStrokeRec(InitStyle s)
    : fResScale(1)
    , fWidth((kFill_InitStyle == s) ? kStrokeRec_FillStyleWidth : 0)
    , fMiterLimit(SkPaintDefaults_MiterLimit)
    , fCap(static_cast<uint32_t>(SkPaint::kDefault_Cap))
    , fJoin(static_cast<uint32_t>(SkPaint::kDefault_Join))
    , fStrokeAndFill(0) {}

SkStrokeRec::SkStrokeRec(const SkPaint& paint, SkScalar resScale) {
    init(paint, paint.getStyle(), resScale);
}

SkStrokeRec::SkStrokeRec(const SkPaint& paint, SkPaint::Style styleOverride, SkScalar resScale) {
    init(paint, styleOverride, resScale);
}

void SkStrokeRec::init(const SkPaint& paint, SkPaint::Style style, SkScalar resScale) {
    fResScale = resScale;

    switch (style) {
        case SkPaint::kFill_Style:
            fWidth = kStrokeRec_FillStyleWidth;
            fStrokeAndFill = 0;
            break;
        case SkPaint::kStroke_Style:
            fWidth = paint.getStrokeWidth();
            fStrokeAndFill = 0;
            break;
        case SkPaint::kStrokeAndFill_Style:
            if (0 == paint.getStrokeWidth()) {
                // hairline+fill == fill
                fWidth = kStrokeRec_FillStyleWidth;
                fStrokeAndFill = 0;
            } else {
                fWidth = paint.getStrokeWidth();
                fStrokeAndFill = 1;
            }
            break;
        default:
            SkDEBUGFAIL("unknown paint style");
            // fall back on just fill
            fWidth = kStrokeRec_FillStyleWidth;
            fStrokeAndFill = 0;
            break;
    }

    // copy these from the paint, regardless of our "style"
    fMiterLimit = paint.getStrokeMiter();
    fCap        = static_cast<uint32_t>(paint.getStrokeCap());
    fJoin       = static_cast<uint32_t>(paint.getStrokeJoin());
}

SkStrokeRec::Style SkStrokeRec::getStyle() const {
    Style style = kFill_Style;
    if (fWidth < 0) {
        style = kFill_Style;
    } else if (0 == fWidth) {
        style = kHairline_Style;
    } else {
        style = fStrokeAndFill ? kStrokeAndFill_Style : kStroke_Style;
    }
    return style;
}

void SkStrokeRec::setFillStyle() {
    fWidth = kStrokeRec_FillStyleWidth;
    fStrokeAndFill = 0;
}

void SkStrokeRec::setHairlineStyle() {
    fWidth = 0;
    fStrokeAndFill = 0;
}

void SkStrokeRec::setStrokeStyle(SkScalar width, bool strokeAndFill) {
    if (strokeAndFill && (0 == width)) {
        // hairline+fill == fill
        setFillStyle();
    } else {
        fWidth = width;
        fStrokeAndFill = strokeAndFill ? 1 : 0;
    }
}

bool SkStrokeRec::applyToPath(SkPathBuilder* dst, const SkPath& src) const {
    if (fWidth <= 0) {  // hairline or fill
        return false;
    }

    SkStroke stroker;
    stroker.setCap(getCap());
    stroker.setJoin(getJoin());
    stroker.setMiterLimit(fMiterLimit);
    stroker.setWidth(fWidth);
    stroker.setDoFill(fStrokeAndFill != 0);
    stroker.setResScale(fResScale);
    stroker.strokePath(src, dst);
    return true;
}

void SkStrokeRec::applyToPaint(SkPaint* paint) const {
    if (fWidth < 0) {  // fill
        (*paint).setStyle(SkPaint::kFill_Style);
        return;
    }

    (*paint).setStyle(fStrokeAndFill ? SkPaint::kStrokeAndFill_Style : SkPaint::kStroke_Style);
    (*paint).setStrokeWidth(fWidth);
    (*paint).setStrokeMiter(fMiterLimit);
    (*paint).setStrokeCap(getCap());
    (*paint).setStrokeJoin(getJoin());
}

SkScalar SkStrokeRec::getInflationRadius() const {
    const SkScalar radius = GetInflationRadius(getJoin(), fMiterLimit, getCap(), fWidth);
    return radius;
}

SkScalar SkStrokeRec::GetInflationRadius(const SkPaint& paint, SkPaint::Style style) {
    const SkScalar width = SkPaint::kFill_Style == style ? -SK_Scalar1 : paint.getStrokeWidth();
    const SkScalar radius = GetInflationRadius(paint.getStrokeJoin(), paint.getStrokeMiter(),
                                               paint.getStrokeCap(), width);
    return radius;
}

SkScalar SkStrokeRec::GetInflationRadius(SkPaint::Join join, SkScalar miterLimit, SkPaint::Cap cap,
                                         SkScalar strokeWidth) {
    if (strokeWidth < 0) {  // fill
        return 0;
    } else if (0 == strokeWidth) {
        // FIXME: We need a "matrixScale" parameter here in order to properly handle hairlines.
        // Their with is determined in device space, unlike other strokes.
        // skbug.com/40039419
        return SK_Scalar1;
    }

    // since we're stroked, outset the rect by the radius (and join type, caps)
    SkScalar multiplier = SK_Scalar1;
    if (SkPaint::kMiter_Join == join) {
        multiplier = std::max(multiplier, miterLimit);
    }
    if (SkPaint::kSquare_Cap == cap) {
        multiplier = std::max(multiplier, SK_ScalarSqrt2);
    }
    const SkScalar radius = strokeWidth/2 * multiplier;
    return radius;
}
