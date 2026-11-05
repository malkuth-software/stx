// Differential corpus for stroking: random and degenerate paths stroked with every cap,
// join, a range of widths, miter limits and resolutions, with and without fill; rectangle
// strokes in both directions; and the stroke-record queries and conversions.

#include "differential_corpus.h"

#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSpan.h"
#include "include/core/SkStrokeRec.h"
#include "src/core/SkStroke.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

const std::array<float, 8> kSpecials{
    0.0f, -0.0f, 1e-7f, 1e-30f, 1e20f, -1e20f, corpus::kDenormal, 1e-4f,
};

const std::array<float, 8> kWeights{
    0.5f, 0.70710678f, 1.0f, 2.0f, 1e-6f, 1e6f, 0.0f, 10.0f,
};

const std::array<float, 9> kWidths{
    0.0f, 0.5f, 1.0f, 3.0f, 10.0f, 1e-3f, 40.0f, 1e5f, -1.0f,
};

const std::array<float, 6> kMiterLimits{
    0.0f, 1.0f, 1.41421354f, 1.5f, 4.0f, 10.0f,
};

const std::array<float, 5> kResScales{
    1.0f, 0.25f, 4.0f, 100.0f, 1e-3f,
};

float pick_coordinate(Sequence& sequence, bool allowSpecial) {
    if (allowSpecial && sequence.next() % 20 == 0) {
        const float special = kSpecials[sequence.pick(kSpecials.size())];
        return special;
    }
    const int32_t whole = static_cast<int32_t>(sequence.next() % 129) - 64;
    const float fraction = static_cast<float>(sequence.next() % 8) * 0.125f;
    const float value = static_cast<float>(whole) + fraction;
    return value;
}

SkPoint pick_point(Sequence& sequence, bool allowSpecial, const SkPoint& previous) {
    const uint32_t choice = sequence.next() % 12;
    if (choice == 0) {
        return previous;
    }
    if (choice == 1) {
        // Nearly coincident with the previous point.
        const SkPoint near{previous.fX + 1e-6f, previous.fY};
        return near;
    }
    const SkPoint point{pick_coordinate(sequence, allowSpecial),
                        pick_coordinate(sequence, allowSpecial)};
    return point;
}

SkPath random_path(Sequence& sequence, bool allowSpecial) {
    SkPathBuilder builder{};
    const int contours = 1 + static_cast<int>(sequence.next() % 3);
    SkPoint last{0, 0};
    for (int contour = 0; contour < contours; ++contour) {
        last = pick_point(sequence, allowSpecial, last);
        builder.moveTo(last);
        const int verbs = static_cast<int>(sequence.next() % 6);
        for (int verb = 0; verb < verbs; ++verb) {
            const uint32_t kind = sequence.next() % 5;
            const SkPoint p1 = pick_point(sequence, allowSpecial, last);
            const SkPoint p2 = pick_point(sequence, allowSpecial, p1);
            const SkPoint p3 = pick_point(sequence, allowSpecial, p2);
            if (kind == 0 || kind == 4) {
                builder.lineTo(p1);
                last = p1;
            } else if (kind == 1) {
                builder.quadTo(p1, p2);
                last = p2;
            } else if (kind == 2) {
                builder.conicTo(p1, p2, kWeights[sequence.pick(kWeights.size())]);
                last = p2;
            } else {
                builder.cubicTo(p1, p2, p3);
                last = p3;
            }
        }
        if (sequence.next() % 3 == 0) {
            builder.close();
        }
    }
    if (sequence.next() % 10 == 0) {
        builder.setFillType(SkPathFillType::kInverseWinding);
    }
    const SkPath path = builder.detach();
    return path;
}

// Hand-built shapes that reach the stroker's degenerate and special paths.
std::vector<SkPath> special_paths() {
    std::vector<SkPath> paths{};
    SkPathBuilder builder{};

    builder.moveTo(10, 10);
    builder.lineTo(10, 10);
    paths.push_back(builder.detach());

    builder.moveTo(10, 10);
    builder.close();
    paths.push_back(builder.detach());

    builder.moveTo(10, 10);
    builder.lineTo(10, 10);
    builder.lineTo(10, 10);
    builder.close();
    paths.push_back(builder.detach());

    builder.moveTo(0, 0);
    builder.quadTo(0, 0, 0, 0);
    paths.push_back(builder.detach());

    builder.moveTo(0, 0);
    builder.cubicTo(0, 0, 0, 0, 0, 0);
    paths.push_back(builder.detach());

    // Control points on the chord, inside and outside the ends.
    builder.moveTo(0, 0);
    builder.quadTo(30, 0, 10, 0);
    paths.push_back(builder.detach());
    builder.moveTo(0, 0);
    builder.cubicTo(20, 0, -10, 0, 10, 0);
    paths.push_back(builder.detach());
    builder.moveTo(0, 0);
    builder.cubicTo(30, 0, -20, 0, 40, 0);
    paths.push_back(builder.detach());
    builder.moveTo(0, 0);
    builder.conicTo(30, 0, 10, 0, 2);
    paths.push_back(builder.detach());

    // A cusp, a loop and a tight curve.
    builder.moveTo(0, 0);
    builder.cubicTo(40, 40, 0, 40, 40, 0);
    paths.push_back(builder.detach());
    builder.moveTo(0, 0);
    builder.cubicTo(60, 60, -20, 60, 40, 0);
    paths.push_back(builder.detach());
    builder.moveTo(0, 0);
    builder.quadTo(1, 6, 0, 3);
    paths.push_back(builder.detach());

    // Sharp and reversing joins.
    builder.moveTo(0, 0);
    builder.lineTo(50, 1);
    builder.lineTo(0, 2);
    builder.lineTo(50, 2);
    builder.lineTo(0, 2);
    paths.push_back(builder.detach());

    // A closed convex line-only polygon (center can be ignored when filled) and a rectangle.
    builder.moveTo(0, 0);
    builder.lineTo(30, 0);
    builder.lineTo(40, 20);
    builder.lineTo(10, 30);
    builder.close();
    paths.push_back(builder.detach());
    paths.push_back(SkPath::Rect(SkRect::MakeLTRB(5, 5, 25, 15)));
    paths.push_back(SkPath::Rect(SkRect::MakeLTRB(5, 5, 25, 15), SkPathDirection::kCCW, 2));
    paths.push_back(SkPath::Oval(SkRect::MakeLTRB(0, 0, 30, 20)));

    // A clockwise and a counter-clockwise curved contour for stroke-and-fill direction.
    builder.moveTo(0, 0);
    builder.quadTo(20, -10, 40, 0);
    builder.lineTo(20, 30);
    builder.close();
    paths.push_back(builder.detach());
    builder.moveTo(0, 0);
    builder.lineTo(20, 30);
    builder.quadTo(40, 10, 40, 0);
    builder.close();
    paths.push_back(builder.detach());
    return paths;
}

void append_point(Output& output, const SkPoint& point) {
    append(output, point.fX);
    append(output, point.fY);
}

void append_path(Output& output, const SkPath& path) {
    const SkSpan<const SkPathVerb> verbs = path.verbs();
    const SkSpan<const SkPoint> points = path.points();
    const SkSpan<const float> weights = path.conicWeights();
    append(output, static_cast<uint8_t>(path.getFillType()));
    append(output, verbs.size());
    for (const SkPathVerb verb : verbs) {
        append(output, static_cast<uint8_t>(verb));
    }
    append(output, points.size());
    for (const SkPoint& point : points) {
        append_point(output, point);
    }
    append(output, weights.size());
    for (const float weight : weights) {
        append(output, weight);
    }
}

struct StrokeParams {
    float fWidth = 1;
    float fMiterLimit = 4;
    float fResScale = 1;
    SkPaint::Cap fCap = SkPaint::kButt_Cap;
    SkPaint::Join fJoin = SkPaint::kMiter_Join;
    bool fDoFill = false;
};

StrokeParams pick_params(Sequence& sequence) {
    StrokeParams params{};
    params.fWidth = kWidths[sequence.pick(kWidths.size())];
    if (params.fWidth < 0) {
        params.fWidth = 2.0f;
    }
    params.fMiterLimit = kMiterLimits[sequence.pick(kMiterLimits.size())];
    params.fResScale = kResScales[sequence.pick(kResScales.size())];
    params.fCap = static_cast<SkPaint::Cap>(sequence.next() % SkPaint::kCapCount);
    params.fJoin = static_cast<SkPaint::Join>(sequence.next() % SkPaint::kJoinCount);
    params.fDoFill = sequence.next() % 4 == 0;
    return params;
}

void append_stroke(Output& output, const SkPath& path, const StrokeParams& params) {
    SkStroke stroke{};
    stroke.setWidth(params.fWidth);
    stroke.setMiterLimit(params.fMiterLimit);
    stroke.setResScale(params.fResScale);
    stroke.setCap(params.fCap);
    stroke.setJoin(params.fJoin);
    stroke.setDoFill(params.fDoFill);
    append(output, static_cast<uint8_t>(stroke.getCap()));
    append(output, static_cast<uint8_t>(stroke.getJoin()));
    append_bool(output, stroke.getDoFill());
    append(output, stroke.getResScale());

    SkPathBuilder builder{};
    builder.moveTo(-1, -1);  // strokePath appends; strokeRect resets.
    stroke.strokePath(path, &builder);
    const SkPath stroked = builder.detach();
    append_path(output, stroked);
}

void stroke_path_corpus(Output& output, Sequence& sequence) {
    const std::vector<SkPath> specials = special_paths();
    for (const SkPath& path : specials) {
        for (int cap = 0; cap < SkPaint::kCapCount; ++cap) {
            for (int join = 0; join < SkPaint::kJoinCount; ++join) {
                for (const float width : {1.0f, 6.0f, 25.0f}) {
                    for (int fill = 0; fill < 2; ++fill) {
                        StrokeParams params{};
                        params.fWidth = width;
                        params.fCap = static_cast<SkPaint::Cap>(cap);
                        params.fJoin = static_cast<SkPaint::Join>(join);
                        params.fDoFill = fill == 1;
                        params.fMiterLimit = join == 0 ? 10.0f : 4.0f;
                        append_stroke(output, path, params);
                        ++output.cases;
                    }
                }
            }
        }
    }
    for (int i = 0; i < 2500; ++i) {
        const SkPath path = random_path(sequence, i % 5 == 0);
        for (int variant = 0; variant < 3; ++variant) {
            const StrokeParams params = pick_params(sequence);
            append_stroke(output, path, params);
        }
        ++output.cases;
    }
}

void stroke_rect_corpus(Output& output, Sequence& sequence) {
    const std::array<SkRect, 6> rects{
        SkRect::MakeLTRB(0, 0, 20, 10), SkRect::MakeLTRB(20, 10, 0, 0),
        SkRect::MakeLTRB(20, 0, 0, 10), SkRect::MakeLTRB(0, 0, 0, 10),
        SkRect::MakeLTRB(5, 5, 5, 5), SkRect::MakeLTRB(-3, -2, 1e5f, 0.5f),
    };
    for (const SkRect& rect : rects) {
        for (int join = 0; join < SkPaint::kJoinCount; ++join) {
            for (const float miter : {1.0f, 1.41421354f, 1.5f}) {
                for (const float width : {0.0f, 1.0f, 4.0f, 30.0f}) {
                    for (int fill = 0; fill < 2; ++fill) {
                        for (int dir = 0; dir < 2; ++dir) {
                            SkStroke stroke{};
                            stroke.setWidth(width);
                            stroke.setMiterLimit(miter);
                            stroke.setJoin(static_cast<SkPaint::Join>(join));
                            stroke.setDoFill(fill == 1);
                            SkPathBuilder builder{};
                            builder.moveTo(-1, -1);
                            stroke.strokeRect(rect, &builder,
                                              dir == 0 ? SkPathDirection::kCW
                                                       : SkPathDirection::kCCW);
                            const SkPath stroked = builder.detach();
                            append_path(output, stroked);
                            ++output.cases;
                        }
                    }
                }
            }
        }
    }
    static_cast<void>(sequence);
}

void append_rec(Output& output, const SkStrokeRec& rec) {
    append(output, static_cast<uint8_t>(rec.getStyle()));
    append(output, rec.getWidth());
    append(output, rec.getMiter());
    append(output, static_cast<uint8_t>(rec.getCap()));
    append(output, static_cast<uint8_t>(rec.getJoin()));
    append_bool(output, rec.isHairlineStyle());
    append_bool(output, rec.isFillStyle());
    append_bool(output, rec.needToApply());
    append(output, rec.getResScale());
    append(output, rec.getInflationRadius());
}

void stroke_rec_corpus(Output& output, Sequence& sequence) {
    const SkStrokeRec hairline(SkStrokeRec::kHairline_InitStyle);
    const SkStrokeRec fill(SkStrokeRec::kFill_InitStyle);
    append_rec(output, hairline);
    append_rec(output, fill);
    std::vector<SkStrokeRec> recs{};
    for (int i = 0; i < 600; ++i) {
        SkPaint paint{};
        paint.setStyle(static_cast<SkPaint::Style>(sequence.next() % 3));
        paint.setStrokeWidth(kWidths[sequence.pick(kWidths.size() - 1)]);
        paint.setStrokeMiter(kMiterLimits[sequence.pick(kMiterLimits.size())]);
        paint.setStrokeCap(static_cast<SkPaint::Cap>(sequence.next() % SkPaint::kCapCount));
        paint.setStrokeJoin(static_cast<SkPaint::Join>(sequence.next() % SkPaint::kJoinCount));
        const SkPaint::Style override = static_cast<SkPaint::Style>(sequence.next() % 3);
        const SkStrokeRec fromPaint(paint, kResScales[sequence.pick(kResScales.size())]);
        const SkStrokeRec overridden(paint, override);
        append_rec(output, fromPaint);
        append_rec(output, overridden);
        append(output, SkStrokeRec::GetInflationRadius(paint, override));
        append(output, SkStrokeRec::GetInflationRadius(paint.getStrokeJoin(),
                                                       paint.getStrokeMiter(),
                                                       paint.getStrokeCap(),
                                                       paint.getStrokeWidth()));

        SkStrokeRec edited = fromPaint;
        const uint32_t edit = sequence.next() % 4;
        if (edit == 0) {
            edited.setFillStyle();
        } else if (edit == 1) {
            edited.setHairlineStyle();
        } else {
            edited.setStrokeStyle(kWidths[sequence.pick(kWidths.size() - 1)], edit == 3);
        }
        edited.setStrokeParams(static_cast<SkPaint::Cap>(sequence.next() % SkPaint::kCapCount),
                               static_cast<SkPaint::Join>(sequence.next() % SkPaint::kJoinCount),
                               kMiterLimits[sequence.pick(kMiterLimits.size())]);
        edited.setResScale(kResScales[sequence.pick(kResScales.size())]);
        append_rec(output, edited);

        SkPaint applied{};
        edited.applyToPaint(&applied);
        append(output, static_cast<uint8_t>(applied.getStyle()));
        append(output, applied.getStrokeWidth());
        append(output, applied.getStrokeMiter());
        append(output, static_cast<uint8_t>(applied.getStrokeCap()));
        append(output, static_cast<uint8_t>(applied.getStrokeJoin()));

        const SkPath path = random_path(sequence, false);
        SkPathBuilder builder{};
        builder.moveTo(-1, -1);
        append_bool(output, edited.applyToPath(&builder, path));
        const SkPath stroked = builder.detach();
        append_path(output, stroked);

        recs.push_back(edited);
        recs.push_back(fromPaint);
        ++output.cases;
    }
    for (size_t a = 0; a < 60; ++a) {
        for (size_t b = 0; b < 60; ++b) {
            append_bool(output, recs[a].hasEqualEffect(recs[b]));
        }
    }
    ++output.cases;
}

void stroke_corpus(Output& output) {
    Sequence sequence{};
    stroke_path_corpus(output, sequence);
    stroke_rect_corpus(output, sequence);
    stroke_rec_corpus(output, sequence);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
    Output output{};
    stroke_corpus(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("stroke geometry: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
