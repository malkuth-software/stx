// Differential corpus for the non-anti-aliased scan converters: path, rectangle and triangle
// fills and hairlines (butt, square and round caps, plain and anti-aliased line procedures),
// through rectangular, complex and anti-aliased clips. A recording blitter captures every
// span, rectangle, run and mask in order; a direct-blit blitter captures hairline pixels.

#include "differential_corpus.h"

#include "include/core/SkImageInfo.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRegion.h"
#include "include/core/SkSpan.h"
#include "src/core/SkBlitter.h"
#include "src/core/SkMask.h"
#include "src/core/SkPathEnums.h"
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

// Offers direct pixel writes into a 128x128 buffer of the given pixel size; any other call is
// recorded through the recording blitter.
class DirectBlitter : public RecordingBlitter {
public:
    DirectBlitter(Output* output, SkColorType colorType)
            : RecordingBlitter(output)
            , fInfo(SkImageInfo::Make(128, 128, colorType, kPremul_SkAlphaType))
            , fStorage(static_cast<size_t>(fInfo.computeMinByteSize()), 0) {}

    std::optional<DirectBlit> canDirectBlit() override {
        DirectBlit blit{};
        blit.pm = SkPixmap(fInfo, fStorage.data(), fInfo.minRowBytes());
        blit.value = 0xA1B2C3D4E5F60718ull;
        const std::optional<DirectBlit> direct(blit);
        return direct;
    }

    const std::vector<uint8_t>& storage() const { return fStorage; }

private:
    SkImageInfo fInfo;
    std::vector<uint8_t> fStorage;
};

float pick_coordinate(Sequence& sequence, bool allowLarge) {
    const uint32_t choice = sequence.next() % 30;
    if (allowLarge && choice == 0) {
        const float large = (sequence.next() % 2 == 0) ? 40000.0f : -1e6f;
        return large;
    }
    const int32_t whole = static_cast<int32_t>(sequence.next() % 200) - 50;
    float fraction = static_cast<float>(sequence.next() % 16) * 0.0625f;
    if (choice == 1) {
        fraction = 0.5f;
    }
    const float value = static_cast<float>(whole) + fraction;
    return value;
}

SkPoint pick_point(Sequence& sequence, const SkPoint& previous, bool allowLarge) {
    const uint32_t choice = sequence.next() % 10;
    if (choice == 0) {
        const SkPoint vertical{previous.fX, pick_coordinate(sequence, allowLarge)};
        return vertical;
    }
    if (choice == 1) {
        const SkPoint horizontal{pick_coordinate(sequence, allowLarge), previous.fY};
        return horizontal;
    }
    if (choice == 2) {
        return previous;
    }
    const SkPoint point{pick_coordinate(sequence, allowLarge),
                        pick_coordinate(sequence, allowLarge)};
    return point;
}

SkPath random_path(Sequence& sequence, bool allowLarge) {
    SkPathBuilder builder{};
    const int contours = 1 + static_cast<int>(sequence.next() % 3);
    SkPoint last{0, 0};
    for (int contour = 0; contour < contours; ++contour) {
        last = SkPoint{pick_coordinate(sequence, allowLarge),
                       pick_coordinate(sequence, allowLarge)};
        builder.moveTo(last);
        const int verbs = static_cast<int>(sequence.next() % 6);
        for (int verb = 0; verb < verbs; ++verb) {
            const SkPoint p1 = pick_point(sequence, last, allowLarge);
            const SkPoint p2 = pick_point(sequence, p1, allowLarge);
            const SkPoint p3 = pick_point(sequence, p2, allowLarge);
            const uint32_t kind = sequence.next() % 5;
            if (kind <= 1) {
                builder.lineTo(p1);
                last = p1;
            } else if (kind == 2) {
                builder.quadTo(p1, p2);
                last = p2;
            } else if (kind == 3) {
                builder.conicTo(p1, p2, (sequence.next() % 2 == 0) ? 0.70710678f : 3.0f);
                last = p2;
            } else {
                builder.cubicTo(p1, p2, p3);
                last = p3;
            }
        }
        if (sequence.next() % 2 == 0) {
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
    clips.emplace_back(SkIRect::MakeLTRB(-20, 10, 60, 45));
    clips.emplace_back(SkIRect::MakeLTRB(5, 5, 6, 90));
    const SkPath circle = SkPath::Circle(50, 50, 40);
    clips.emplace_back(circle, SkIRect::MakeLTRB(0, 0, 100, 100), false);
    clips.emplace_back(circle, SkIRect::MakeLTRB(0, 0, 100, 100), true);
    SkPathBuilder holes{};
    holes.addRect(SkRect::MakeLTRB(0, 0, 100, 100));
    holes.addRect(SkRect::MakeLTRB(30, 30, 70, 70), SkPathDirection::kCCW);
    const SkPath ring = holes.detach();
    clips.emplace_back(ring, SkIRect::MakeLTRB(0, 0, 100, 100), false);
    clips.emplace_back(SkIRect::MakeLTRB(0, 0, 40000, 40000));
    return clips;
}

void fill_corpus(Output& output, Sequence& sequence, const std::vector<SkRasterClip>& clips) {
    for (int i = 0; i < 2500; ++i) {
        const SkPath path = random_path(sequence, i % 8 == 0);
        const SkRasterClip& clip = clips[sequence.pick(clips.size())];
        for (int resolve = 0; resolve < 2; ++resolve) {
            const std::optional<SkPathRaw> raw = SkPathPriv::Raw(
                    path, resolve == 0 ? SkResolveConvexity::kNo : SkResolveConvexity::kYes);
            append_bool(output, raw.has_value());
            if (!raw.has_value()) {
                continue;
            }
            RecordingBlitter blitter(&output);
            SkScan::FillPath(raw.value(), clip, &blitter);
            append(output, blitter.calls());
            if (clip.isBW()) {
                RecordingBlitter regionBlitter(&output);
                SkScan::FillPath(raw.value(), clip.bwRgn(), &regionBlitter);
                append(output, regionBlitter.calls());
            }
        }
        ++output.cases;
    }
}

void hairline_corpus(Output& output, Sequence& sequence, const std::vector<SkRasterClip>& clips) {
    using HairProc = void (*)(const SkPathRaw&, const SkRasterClip&, SkBlitter*);
    const std::array<HairProc, 6> procs{
        SkScan::HairPath, SkScan::HairSquarePath, SkScan::HairRoundPath,
        SkScan::AntiHairPath, SkScan::AntiHairSquarePath, SkScan::AntiHairRoundPath,
    };
    for (int i = 0; i < 2500; ++i) {
        const SkPath path = random_path(sequence, i % 8 == 0);
        const std::optional<SkPathRaw> raw = SkPathPriv::Raw(path, SkResolveConvexity::kNo);
        append_bool(output, raw.has_value());
        if (!raw.has_value()) {
            ++output.cases;
            continue;
        }
        const SkRasterClip& clip = clips[sequence.pick(clips.size())];
        const size_t proc = sequence.pick(procs.size());
        RecordingBlitter blitter(&output);
        procs[proc](raw.value(), clip, &blitter);
        append(output, blitter.calls());
        ++output.cases;
    }
}

void line_corpus(Output& output, Sequence& sequence, const std::vector<SkRasterClip>& clips) {
    const std::array<SkColorType, 4> directTypes{
        kAlpha_8_SkColorType, kRGB_565_SkColorType, kRGBA_8888_SkColorType,
        kRGBA_F16_SkColorType,
    };
    for (int i = 0; i < 2000; ++i) {
        std::array<SkPoint, 4> pts{};
        pts[0] = SkPoint{pick_coordinate(sequence, i % 10 == 0),
                         pick_coordinate(sequence, i % 10 == 0)};
        for (size_t k = 1; k < pts.size(); ++k) {
            pts[k] = pick_point(sequence, pts[k - 1], i % 10 == 0);
        }
        const size_t count = 1 + sequence.pick(pts.size());
        const SkSpan<const SkPoint> span(pts.data(), count);
        const SkRasterClip& clip = clips[sequence.pick(clips.size())];

        RecordingBlitter blitter(&output);
        SkScan::HairLine(span, clip, &blitter);
        RecordingBlitter antiBlitter(&output);
        SkScan::AntiHairLine(span, clip, &antiBlitter);
        append(output, blitter.calls());
        append(output, antiBlitter.calls());

        // Direct writes into pixel storage of every offered size, through a rectangle clip.
        const SkRasterClip directClip(SkIRect::MakeLTRB(0, 0, 128, 128));
        DirectBlitter direct(&output, directTypes[sequence.pick(directTypes.size())]);
        SkScan::HairLine(span, directClip, &direct);
        append(output, corpus::fnv1a(direct.storage()));
        append(output, direct.calls());

        const SkRect rect = SkRect::MakeLTRB(pts[0].fX, pts[0].fY, pts[1].fX, pts[1].fY);
        RecordingBlitter rectBlitter(&output);
        SkScan::HairRect(rect, clip, &rectBlitter);
        SkScan::FillRect(rect, clip, &rectBlitter);
        SkScan::FillIRect(rect.roundOut(), clip, &rectBlitter);
        const SkPoint stroke{static_cast<float>(sequence.next() % 5),
                             static_cast<float>(sequence.next() % 5)};
        SkScan::FrameRect(rect, stroke, clip, &rectBlitter);
        SkScan::FillTriangle(pts.data(), clip, &rectBlitter);
        append(output, rectBlitter.calls());
        append_bool(output, SkScan::PathRequiresTiling(rect.roundOut()));
        ++output.cases;
    }
}

void scan_corpus(Output& output) {
    Sequence sequence{};
    const std::vector<SkRasterClip> clips = make_clips();
    fill_corpus(output, sequence, clips);
    hairline_corpus(output, sequence, clips);
    line_corpus(output, sequence, clips);
}

#ifndef STX_SKIA_REFERENCE
bool corrected_behavior() {
    // A capped hairline over storage ending in repeated moves: the iterator trims one trailing
    // move, and the original then dereferenced an empty record for the last remaining one.
    const std::array<SkPoint, 3> points{SkPoint{1, 1}, SkPoint{5, 5}, SkPoint{9, 9}};
    const std::array<SkPathVerb, 3> verbs{SkPathVerb::kMove, SkPathVerb::kMove,
                                          SkPathVerb::kMove};
    const SkPathRaw raw{
        SkSpan<const SkPoint>(points.data(), points.size()),
        SkSpan<const SkPathVerb>(verbs.data(), verbs.size()),
        SkSpan<const float>(),
        SkRect::MakeLTRB(1, 1, 9, 9),
        SkPathFillType::kWinding,
        SkPathConvexity::kUnknown,
        0,
    };
    Output scratch{};
    RecordingBlitter blitter(&scratch);
    const SkRasterClip clip(SkIRect::MakeLTRB(0, 0, 20, 20));
    SkScan::HairSquarePath(raw, clip, &blitter);
    SkScan::HairRoundPath(raw, clip, &blitter);
    return blitter.calls() == 0;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
#ifndef STX_SKIA_REFERENCE
    if (!corrected_behavior()) {
        std::printf("scan converters: corrected behavior FAIL\n");
        return 1;
    }
#endif
    Output output{};
    scan_corpus(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("scan converters: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
