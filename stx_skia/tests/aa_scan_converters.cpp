// Differential corpus for the anti-aliased scan converters: analytic path fills (mask,
// run-based and clamped run-based accumulation, convex and general walks, inverse fills,
// fat rectangles), anti-aliased clips built from paths, and anti-aliased hairlines, rectangle
// fills and rectangle frames. A recording blitter captures every call in order, including
// every coverage byte of each blitted mask.

#include "differential_corpus.h"

#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSpan.h"
#include "src/core/SkBlitter.h"
#include "src/core/SkMask.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkPathRaw.h"
#include "src/core/SkRasterClip.h"
#include "src/core/SkScan.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

// Records every blitter call, in order, into a corpus.
class RecordingBlitter : public SkBlitter {
public:
    explicit RecordingBlitter(Output* output) : fOutput(output) {}

    void blitH(int x, int y, int width) override {
        record(1, x, y, width, 0);
    }
    void blitAntiH(int x, int y, const SkAlpha antialias[], const int16_t runs[]) override {
        record(2, x, y, 0, 0);
        int index = 0;
        while (runs[index] > 0) {
            append(*fOutput, antialias[index]);
            append(*fOutput, runs[index]);
            index += runs[index];
        }
        append(*fOutput, static_cast<int16_t>(0));
    }
    void blitV(int x, int y, int height, SkAlpha alpha) override {
        record(3, x, y, height, alpha);
    }
    void blitRect(int x, int y, int width, int height) override {
        record(4, x, y, width, height);
    }
    void blitAntiRect(int x, int y, int width, int height, SkAlpha left, SkAlpha right) override {
        record(5, x, y, width, height);
        append(*fOutput, left);
        append(*fOutput, right);
    }
    void blitMask(const SkMask& mask, const SkIRect& clip) override {
        record(6, mask.fBounds.fLeft, mask.fBounds.fTop, mask.fBounds.width(),
               mask.fBounds.height());
        append(*fOutput, static_cast<uint8_t>(mask.fFormat));
        append(*fOutput, clip.fLeft);
        append(*fOutput, clip.fTop);
        append(*fOutput, clip.fRight);
        append(*fOutput, clip.fBottom);
        SkIRect visible = mask.fBounds;
        if (mask.fFormat == SkMask::kA8_Format && visible.intersect(clip)) {
            for (int y = visible.fTop; y < visible.fBottom; ++y) {
                for (int x = visible.fLeft; x < visible.fRight; ++x) {
                    append(*fOutput, *mask.getAddr8(x, y));
                }
            }
        }
    }

    int calls() const { return fCalls; }

private:
    void record(uint8_t kind, int a, int b, int c, int d) {
        append(*fOutput, kind);
        append(*fOutput, a);
        append(*fOutput, b);
        append(*fOutput, c);
        append(*fOutput, d);
        ++fCalls;
    }

    Output* fOutput;
    int fCalls = 0;
};

// Coordinates in [origin, origin + span) at sixteenth steps.
float pick_coordinate(Sequence& sequence, int origin, int span) {
    const uint32_t offset = sequence.next() % static_cast<uint32_t>(span);
    const int32_t whole = origin + static_cast<int32_t>(offset);
    const float fraction = static_cast<float>(sequence.next() % 16) * 0.0625f;
    const float value = static_cast<float>(whole) + fraction;
    return value;
}

SkPath random_path(Sequence& sequence) {
    // Small paths use the mask accumulator; larger ones the run-based accumulators.
    const uint32_t size = sequence.next() % 4;
    const int span = size == 0 ? 12 : size == 1 ? 30 : size == 2 ? 90 : 400;
    const int origin = static_cast<int>(sequence.next() % 40) - 20;
    SkPathBuilder builder{};
    const uint32_t shape = sequence.next() % 6;
    if (shape == 0) {
        const float l = pick_coordinate(sequence, origin, span);
        const float t = pick_coordinate(sequence, origin, span);
        builder.addRect(SkRect::MakeLTRB(l, t, l + pick_coordinate(sequence, 0, span),
                                         t + pick_coordinate(sequence, 0, span)));
    } else if (shape == 1) {
        const float cx = pick_coordinate(sequence, origin, span);
        const float cy = pick_coordinate(sequence, origin, span);
        builder.addOval(SkRect::MakeLTRB(cx, cy, cx + pick_coordinate(sequence, 1, span),
                                         cy + pick_coordinate(sequence, 1, span)));
    } else {
        const int contours = 1 + static_cast<int>(sequence.next() % 2);
        for (int contour = 0; contour < contours; ++contour) {
            SkPoint last{pick_coordinate(sequence, origin, span),
                         pick_coordinate(sequence, origin, span)};
            builder.moveTo(last);
            const int verbs = 2 + static_cast<int>(sequence.next() % 5);
            for (int verb = 0; verb < verbs; ++verb) {
                const SkPoint p1{pick_coordinate(sequence, origin, span),
                                 pick_coordinate(sequence, origin, span)};
                const SkPoint p2{pick_coordinate(sequence, origin, span),
                                 pick_coordinate(sequence, origin, span)};
                const SkPoint p3{pick_coordinate(sequence, origin, span),
                                 pick_coordinate(sequence, origin, span)};
                const uint32_t kind = sequence.next() % 4;
                if (kind == 0) {
                    builder.lineTo(p1);
                    last = p1;
                } else if (kind == 1) {
                    builder.quadTo(p1, p2);
                    last = p2;
                } else if (kind == 2) {
                    builder.conicTo(p1, p2, 0.70710678f);
                    last = p2;
                } else {
                    builder.cubicTo(p1, p2, p3);
                    last = p3;
                }
            }
            builder.close();
        }
    }
    const std::array<SkPathFillType, 4> fills{
        SkPathFillType::kWinding, SkPathFillType::kEvenOdd, SkPathFillType::kInverseWinding,
        SkPathFillType::kInverseEvenOdd,
    };
    builder.setFillType(fills[sequence.pick(fills.size())]);
    const SkPath path = builder.detach();
    return path;
}

std::vector<SkRasterClip> make_clips() {
    std::vector<SkRasterClip> clips{};
    clips.reserve(8);
    clips.emplace_back(SkIRect::MakeLTRB(0, 0, 100, 100));
    clips.emplace_back(SkIRect::MakeLTRB(-30, -30, 400, 400));
    clips.emplace_back(SkIRect::MakeLTRB(5, 7, 20, 60));
    const SkPath circle = SkPath::Circle(50, 50, 40);
    clips.emplace_back(circle, SkIRect::MakeLTRB(0, 0, 100, 100), false);
    clips.emplace_back(circle, SkIRect::MakeLTRB(0, 0, 100, 100), true);
    SkPathBuilder holes{};
    holes.addRect(SkRect::MakeLTRB(0, 0, 100, 100));
    holes.addRect(SkRect::MakeLTRB(30, 30, 70, 70), SkPathDirection::kCCW);
    const SkPath ring = holes.detach();
    clips.emplace_back(ring, SkIRect::MakeLTRB(0, 0, 100, 100), true);
    clips.emplace_back(SkIRect::MakeLTRB(0, 0, 40000, 40000));
    return clips;
}

void fill_corpus(Output& output, Sequence& sequence, const std::vector<SkRasterClip>& clips) {
    for (int i = 0; i < 3000; ++i) {
        const SkPath path = random_path(sequence);
        const SkRasterClip& clip = clips[sequence.pick(clips.size())];
        for (int resolve = 0; resolve < 2; ++resolve) {
            const std::optional<SkPathRaw> raw = SkPathPriv::Raw(
                    path, resolve == 0 ? SkResolveConvexity::kNo : SkResolveConvexity::kYes);
            append_bool(output, raw.has_value());
            if (!raw.has_value()) {
                continue;
            }
            RecordingBlitter blitter(&output);
            SkScan::AntiFillPath(raw.value(), clip, &blitter);
            append(output, blitter.calls());
        }
        ++output.cases;
    }
}

void aa_clip_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 600; ++i) {
        const SkPath path = random_path(sequence);
        const SkIRect bounds = SkIRect::MakeLTRB(-10, -10, 120 + static_cast<int>(i % 300),
                                                 110);
        // Building an anti-aliased clip fills the path through the run-based accumulator.
        const SkRasterClip clip(path, bounds, true);
        append_bool(output, clip.isEmpty());
        append_bool(output, clip.isBW());
        append_bool(output, clip.isRect());
        const SkIRect& clipBounds = clip.getBounds();
        append(output, clipBounds.fLeft);
        append(output, clipBounds.fTop);
        append(output, clipBounds.fRight);
        append(output, clipBounds.fBottom);
        RecordingBlitter blitter(&output);
        SkScan::FillRect(SkRect::MakeLTRB(-20, -20, 500, 200), clip, &blitter);
        append(output, blitter.calls());
        ++output.cases;
    }
}

void rect_and_hairline_corpus(Output& output, Sequence& sequence,
                              const std::vector<SkRasterClip>& clips) {
    for (int i = 0; i < 2500; ++i) {
        const SkRasterClip& clip = clips[sequence.pick(clips.size())];
        const SkRect rect = SkRect::MakeLTRB(pick_coordinate(sequence, -20, 140),
                                             pick_coordinate(sequence, -20, 140),
                                             pick_coordinate(sequence, -20, 140),
                                             pick_coordinate(sequence, -20, 140));
        RecordingBlitter blitter(&output);
        SkScan::AntiFillRect(rect, clip, &blitter);
        SkScan::AntiHairRect(rect, clip, &blitter);
        const std::array<float, 6> strokes{0.0f, 0.25f, 0.5f, 1.0f, 2.5f, 7.0f};
        const SkPoint stroke{strokes[sequence.pick(strokes.size())],
                             strokes[sequence.pick(strokes.size())]};
        SkScan::AntiFrameRect(rect, stroke, clip, &blitter);
        SkXRect xrect = SkIRect::MakeEmpty();
        XRect_set(&xrect, rect.makeSorted());
        SkScan::AntiFillXRect(xrect, clip, &blitter);

        std::array<SkPoint, 5> pts{};
        for (SkPoint& point : pts) {
            point = SkPoint{pick_coordinate(sequence, -40, 180),
                            pick_coordinate(sequence, -40, 180)};
        }
        if (i % 7 == 0) {
            pts[1] = SkPoint{pts[0].fX, pts[1].fY};  // vertical
        } else if (i % 7 == 1) {
            pts[1] = SkPoint{pts[1].fX, pts[0].fY};  // horizontal
        } else if (i % 7 == 2) {
            pts[1] = SkPoint{pts[0].fX + 2000, pts[0].fY + 3};  // long, subdivided
        }
        const size_t count = 2 + sequence.pick(pts.size() - 1);
        SkScan::AntiHairLine(SkSpan<const SkPoint>(pts.data(), count), clip, &blitter);
        append(output, blitter.calls());
        ++output.cases;
    }
}

void aa_scan_corpus(Output& output) {
    Sequence sequence{};
    const std::vector<SkRasterClip> clips = make_clips();
    fill_corpus(output, sequence, clips);
    aa_clip_corpus(output, sequence);
    rect_and_hairline_corpus(output, sequence, clips);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
    Output output{};
    aa_scan_corpus(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("aa scan converters: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
