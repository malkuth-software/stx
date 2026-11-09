/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkSpan.h"
#include "include/core/SkStream.h"
#include "src/core/SkFloatBits.h"
#include "src/core/SkStringUtils.h"


static const char* fill_type_name(SkPathFillType fillType) {
    switch (fillType) {
        case SkPathFillType::kWinding:        return "Winding";
        case SkPathFillType::kEvenOdd:        return "EvenOdd";
        case SkPathFillType::kInverseWinding: return "InverseWinding";
        case SkPathFillType::kInverseEvenOdd: return "InverseEvenOdd";
    }
    // Fill types outside the enumeration are unsupported; name them rather than index past
    // the table.
    return "Unknown";
}

constexpr float kSentinelConicWeight = -12345;  // not a valid weight

static void append_coordinate_list(SkString& str, SkSpan<const SkPoint> pts,
                                   SkScalarAsStringType strType, bool decimal) {
    for (size_t i = 0; i < pts.size(); ++i) {
        const SkScalar coordinates[2] = {pts[i].fX, pts[i].fY};
        for (int axis = 0; axis < 2; ++axis) {
            if (decimal) {
                SkAppendScalarDec(&str, coordinates[axis]);
            } else {
                SkAppendScalar(&str, coordinates[axis], strType);
            }
            const bool last = i + 1 == pts.size() && axis == 1;
            if (!last) {
                str.append(", ");
            }
        }
    }
}

static void append_params(SkString* str, const char label[], SkSpan<const SkPoint> pts,
                          SkScalarAsStringType strType, bool useSemicolon,
                          SkScalar conicWeight = kSentinelConicWeight) {
    SkString& text = *str;
    text.append(label);
    text.append("(");

    append_coordinate_list(text, pts, strType, false);
    if (conicWeight != kSentinelConicWeight) {
        text.append(", ");
        SkAppendScalar(&text, conicWeight, strType);
    }
    text.append(useSemicolon ? ");" : ")");
    if (kHex_SkScalarAsStringType == strType) {
        text.append("  // ");
        append_coordinate_list(text, pts, strType, true);
        if (conicWeight >= 0) {
            text.append(", ");
            SkAppendScalarDec(&text, conicWeight);
        }
    }
    text.append("\n");
}

// Appends one builder command per verb. When flushToDebug is true, each command is written to
// SkDebugf and the text is cleared afterward.
static void dump_iter(SkPathIter iter, SkString* builder, const char cmdPrefix[],
                      SkScalarAsStringType strType, bool useSemicolon, bool flushToDebug) {
    for (std::optional<SkPathIter::Rec> rec = iter.next(); rec.has_value(); rec = iter.next()) {
        SkString cmd(cmdPrefix);
        SkSpan<const SkPoint> pts{};
        float cw = kSentinelConicWeight;
        const SkPathIter::Rec& record = rec.value();

        switch (record.fVerb) {
            case SkPathVerb::kMove:
                cmd.append(".moveTo");
                pts = record.fPoints.subspan(0, 1);
                break;
            case SkPathVerb::kLine:
                cmd.append(".lineTo");
                pts = record.fPoints.subspan(1, 1);
                break;
            case SkPathVerb::kQuad:
                cmd.append(".quadTo");
                pts = record.fPoints.subspan(1, 2);
                break;
            case SkPathVerb::kConic:
                cmd.append(".conicTo");
                pts = record.fPoints.subspan(1, 2);
                cw = record.conicWeight();
                break;
            case SkPathVerb::kCubic:
                cmd.append(".cubicTo");
                pts = record.fPoints.subspan(1, 3);
                break;
            case SkPathVerb::kClose:
                cmd.append(".close()");
                if (useSemicolon) {
                    cmd.append(";");
                }
                cmd.append("\n");
                (*builder).append(cmd.c_str());
                break;
        }
        // we don't do this for kClose
        if (pts.size()) {
            append_params(builder, cmd.c_str(), pts, strType, useSemicolon, cw);
        }
        if (flushToDebug && (*builder).size()) {
            SkDebugf("%s", (*builder).c_str());
            (*builder).reset();
        }
    }
}

void SkPath::dump(SkWStream* wStream, bool dumpAsHex) const {
    const SkScalarAsStringType asType = dumpAsHex ? kHex_SkScalarAsStringType
                                                  : kDec_SkScalarAsStringType;

    SkString builder{};
    builder.printf("path.setFillType(SkPathFillType::k%s);\n", fill_type_name(getFillType()));

    // Without a stream, each command goes to SkDebugf as it is produced.
    const bool flushToDebug = wStream == nullptr;
    dump_iter(iter(), &builder, "path", asType, true, flushToDebug);
    if (wStream) {
        (*wStream).writeText(builder.c_str());
    }
}

SkString SkPathBuilder::dumpToString(DumpFormat format) const {
    const SkScalarAsStringType asType = format == DumpFormat::kHex ? kHex_SkScalarAsStringType
                                                                   : kDec_SkScalarAsStringType;

    SkString builder{};
    builder.printf("SkPathBuilder(SkPathFillType::k%s)\n", fill_type_name(fillType()));

    dump_iter(iter(), &builder, "", asType, false, false);

    return builder;
}

void SkPathBuilder::dump(DumpFormat format) const {
    const SkString text = dumpToString(format);
    SkDebugf("%s", text.c_str());
}
