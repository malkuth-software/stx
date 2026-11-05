// Differential corpus for regions and raster clips: region construction, set operations,
// queries, iteration, translation, serialization and boundary paths; regions from paths
// (including tiled clips); antialiased clips built from paths, rectangles and regions, their
// operations, masks and clipping blitter; raster clips switching between their integer and
// antialiased forms; and the raster clip stack. Every recorded value comes from public or
// test-visible state, so both builds must produce identical bytes.

#include "differential_corpus.h"

#include "include/core/SkClipOp.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkRegion.h"
#include "include/core/SkSpan.h"
#include "src/core/SkAAClip.h"
#include "src/core/SkBlitter.h"
#include "src/core/SkMask.h"
#include "src/core/SkRasterClip.h"
#include "src/core/SkRasterClipStack.h"
#include "src/core/SkRegionPriv.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

// Records every blitter call, in order, into a corpus, including the coverage of A8 and LCD16
// masks inside the call's clip.
class RecordingBlitter : public SkBlitter {
public:
    explicit RecordingBlitter(Output* output) : fOutput(output) {}

    void blitH(int x, int y, int width) override {
        (*this).record(1, x, y, width, 0);
    }
    void blitAntiH(int x, int y, const SkAlpha antialias[], const int16_t runs[]) override {
        (*this).record(2, x, y, 0, 0);
        int index = 0;
        while (runs[index] > 0) {
            append(*fOutput, antialias[index]);
            append(*fOutput, runs[index]);
            index += runs[index];
        }
        append(*fOutput, static_cast<int16_t>(0));
    }
    void blitV(int x, int y, int height, SkAlpha alpha) override {
        (*this).record(3, x, y, height, alpha);
    }
    void blitRect(int x, int y, int width, int height) override {
        (*this).record(4, x, y, width, height);
    }
    void blitAntiRect(int x, int y, int width, int height, SkAlpha left, SkAlpha right) override {
        (*this).record(5, x, y, width, height);
        append(*fOutput, left);
        append(*fOutput, right);
    }
    void blitMask(const SkMask& mask, const SkIRect& clip) override {
        (*this).record(6, mask.fBounds.fLeft, mask.fBounds.fTop, mask.fBounds.width(),
                       mask.fBounds.height());
        append(*fOutput, static_cast<uint8_t>(mask.fFormat));
        append(*fOutput, clip.fLeft);
        append(*fOutput, clip.fTop);
        append(*fOutput, clip.fRight);
        append(*fOutput, clip.fBottom);
        size_t bytesPerPixel = 0;
        if (mask.fFormat == SkMask::kA8_Format) {
            bytesPerPixel = 1;
        } else if (mask.fFormat == SkMask::kLCD16_Format) {
            bytesPerPixel = 2;
        }
        SkIRect visible = mask.fBounds;
        if (bytesPerPixel != 0 && visible.intersect(clip)) {
            for (int y = visible.fTop; y < visible.fBottom; ++y) {
                const size_t row = static_cast<size_t>(y - mask.fBounds.fTop) * mask.fRowBytes;
                for (int x = visible.fLeft; x < visible.fRight; ++x) {
                    const size_t column =
                            static_cast<size_t>(x - mask.fBounds.fLeft) * bytesPerPixel;
                    for (size_t byte = 0; byte < bytesPerPixel; ++byte) {
                        append(*fOutput, mask.fImage[row + column + byte]);
                    }
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

int pick_int(Sequence& sequence, int origin, int span) {
    const uint32_t offset = sequence.next() % static_cast<uint32_t>(span);
    const int value = origin + static_cast<int>(offset);
    return value;
}

// Coordinates in [origin, origin + span) at quarter steps.
float pick_coordinate(Sequence& sequence, int origin, int span) {
    const int whole = pick_int(sequence, origin, span);
    const float fraction = static_cast<float>(sequence.next() % 4) * 0.25f;
    const float value = static_cast<float>(whole) + fraction;
    return value;
}

SkIRect random_irect(Sequence& sequence, int origin, int span) {
    const int l = pick_int(sequence, origin, span);
    const int t = pick_int(sequence, origin, span);
    const int w = pick_int(sequence, 0, span / 2 + 1);
    const int h = pick_int(sequence, 0, span / 2 + 1);
    const SkIRect rect = SkIRect::MakeLTRB(l, t, l + w, t + h);
    return rect;
}

SkRect random_rect(Sequence& sequence, int origin, int span) {
    const float l = pick_coordinate(sequence, origin, span);
    const float t = pick_coordinate(sequence, origin, span);
    const float w = pick_coordinate(sequence, 0, span / 2 + 1);
    const float h = pick_coordinate(sequence, 0, span / 2 + 1);
    const SkRect rect = SkRect::MakeLTRB(l, t, l + w, t + h);
    return rect;
}

SkPath random_path(Sequence& sequence, int origin, int span) {
    SkPathBuilder builder{};
    const uint32_t shape = sequence.next() % 5;
    if (shape == 0) {
        builder.addRect(random_rect(sequence, origin, span));
    } else if (shape == 1) {
        builder.addOval(random_rect(sequence, origin, span));
    } else {
        const int contours = 1 + static_cast<int>(sequence.next() % 2);
        for (int contour = 0; contour < contours; ++contour) {
            const SkPoint start{pick_coordinate(sequence, origin, span),
                                pick_coordinate(sequence, origin, span)};
            builder.moveTo(start);
            const int verbs = 2 + static_cast<int>(sequence.next() % 4);
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
                } else if (kind == 1) {
                    builder.quadTo(p1, p2);
                } else if (kind == 2) {
                    builder.conicTo(p1, p2, 0.70710678f);
                } else {
                    builder.cubicTo(p1, p2, p3);
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

// Empty, rectangular, or the union of up to a dozen overlapping rectangles, some empty.
SkRegion random_region(Sequence& sequence) {
    SkRegion region;
    const uint32_t kind = sequence.next() % 6;
    if (kind == 0) {
        return region;
    }
    if (kind == 1) {
        region.setRect(random_irect(sequence, -10, 60));
        return region;
    }
    if (kind == 2) {
        const SkRegion clip(SkIRect::MakeLTRB(-16, -16, 80, 80));
        region.setPath(random_path(sequence, -10, 70), clip);
        return region;
    }
    const size_t count = 2 + sequence.pick(11);
    std::vector<SkIRect> rects(count);
    for (SkIRect& rect : rects) {
        rect = random_irect(sequence, -10, 60);
    }
    region.setRects(SkSpan<const SkIRect>(rects.data(), rects.size()));
    return region;
}

void record_rect(Output& output, const SkIRect& rect) {
    append(output, rect.fLeft);
    append(output, rect.fTop);
    append(output, rect.fRight);
    append(output, rect.fBottom);
}

// Records the region's state and its serialized runs.
void record_region(Output& output, const SkRegion& region) {
    append_bool(output, region.isEmpty());
    append_bool(output, region.isRect());
    append_bool(output, region.isComplex());
    record_rect(output, region.getBounds());
    append(output, region.computeRegionComplexity());
    const size_t size = region.writeToMemory(nullptr);
    std::vector<uint8_t> bytes(size);
    const size_t written = region.writeToMemory(bytes.data());
    append(output, static_cast<uint64_t>(size));
    append(output, static_cast<uint64_t>(written));
    output.bytes.insert(output.bytes.end(), bytes.begin(), bytes.end());
}

void record_path(Output& output, const SkPath& path) {
    const int points = path.countPoints();
    const int verbs = path.countVerbs();
    append(output, points);
    append(output, verbs);
    for (int i = 0; i < points; ++i) {
        const SkPoint point = path.getPoint(i);
        append(output, point.fX);
        append(output, point.fY);
    }
    const SkSpan<const SkPathVerb> verbList = path.verbs();
    for (const SkPathVerb verb : verbList) {
        append(output, static_cast<uint8_t>(verb));
    }
}

// Records the clip's state and a hash of its coverage mask.
void record_aaclip(Output& output, const SkAAClip& clip) {
    append_bool(output, clip.isEmpty());
    append_bool(output, clip.isRect());
    record_rect(output, clip.getBounds());
    SkMaskBuilder mask;
    clip.copyToMask(&mask);
    const SkAutoMaskFreeImage freeImage(mask.image());
    record_rect(output, mask.fBounds);
    append(output, mask.fRowBytes);
    std::vector<uint8_t> bytes{};
    if (mask.fImage) {
        const size_t size = mask.computeImageSize();
        bytes.assign(mask.fImage, mask.fImage + size);
    }
    append(output, static_cast<uint64_t>(bytes.size()));
    append(output, corpus::fnv1a(bytes));
}

void record_raster_clip(Output& output, const SkRasterClip& clip) {
    append_bool(output, clip.isEmpty());
    append_bool(output, clip.isRect());
    append_bool(output, clip.isComplex());
    append_bool(output, clip.isBW());
    record_rect(output, clip.getBounds());
    if (clip.isBW()) {
        record_region(output, clip.bwRgn());
    } else {
        record_aaclip(output, clip.aaRgn());
    }
}

// Records each visited span; a named listener for SkRegionPriv::VisitSpans. The reference
// build's visitor parameter is a std::function; the candidate's is SkRegionSpanVisitor.
#ifdef STX_SKIA_REFERENCE
struct SpanRecorder {
    Output* fOutput;

    void operator()(const SkIRect& span) const {
        record_rect(*fOutput, span);
    }
};
#else
struct SpanRecorder final : SkRegionSpanVisitor {
    explicit SpanRecorder(Output* output) : fOutput(output) {}

    void visitSpan(const SkIRect& span) override {
        record_rect(*fOutput, span);
    }

    Output* fOutput;
};
#endif

void region_queries(Output& output, Sequence& sequence, const SkRegion& a, const SkRegion& b) {
    for (int op = 0; op < SkRegion::kOpCnt; ++op) {
        SkRegion result;
        const bool nonEmpty = result.op(a, b, static_cast<SkRegion::Op>(op));
        append_bool(output, nonEmpty);
        record_region(output, result);
    }
    append_bool(output, a.intersects(b));
    append_bool(output, a.contains(b));
    append_bool(output, a == b);
    append_bool(output, a.quickReject(b));

    for (int i = 0; i < 8; ++i) {
        const int x = pick_int(sequence, -12, 80);
        const int y = pick_int(sequence, -12, 80);
        append_bool(output, a.contains(x, y));
    }
    for (int i = 0; i < 6; ++i) {
        const SkIRect rect = random_irect(sequence, -12, 70);
        append_bool(output, a.contains(rect));
        append_bool(output, a.intersects(rect));
        append_bool(output, a.quickContains(rect));
        append_bool(output, a.quickReject(rect));
        const SkRegion::Op op = static_cast<SkRegion::Op>(sequence.pick(SkRegion::kOpCnt));
        SkRegion rectFirst;
        append_bool(output, rectFirst.op(rect, a, op));
        record_region(output, rectFirst);
        SkRegion regionFirst;
        append_bool(output, regionFirst.op(a, rect, op));
        record_region(output, regionFirst);
        SkRegion inPlace(a);
        append_bool(output, inPlace.op(rect, op));
        record_region(output, inPlace);
        // The copy shared a's runs; changing it must leave a unchanged.
        record_region(output, a);
    }
}

void region_traversal(Output& output, Sequence& sequence, const SkRegion& region) {
    SkRegion::Iterator iter(region);
    int rects = 0;
    while (!iter.done()) {
        record_rect(output, iter.rect());
        iter.next();
        rects += 1;
    }
    append(output, rects);
    append_bool(output, iter.rewind());

    const SkIRect clip = random_irect(sequence, -12, 70);
    SkRegion::Cliperator cliperator(region, clip);
    while (!cliperator.done()) {
        record_rect(output, cliperator.rect());
        cliperator.next();
    }

    for (int i = 0; i < 4; ++i) {
        const int y = pick_int(sequence, -12, 80);
        const int left = pick_int(sequence, -12, 50);
        const int right = left + pick_int(sequence, 1, 60);
        SkRegion::Spanerator spanerator(region, y, left, right);
        int spanLeft = 0;
        int spanRight = 0;
        while (spanerator.next(&spanLeft, &spanRight)) {
            append(output, spanLeft);
            append(output, spanRight);
        }
        append(output, static_cast<int32_t>(-1));
    }

#ifdef STX_SKIA_REFERENCE
    const SpanRecorder recorder{&output};
    SkRegionPriv::VisitSpans(region, std::function<void(const SkIRect&)>(recorder));
#else
    SpanRecorder recorder(&output);
    SkRegionPriv::VisitSpans(region, recorder);
#endif

    record_path(output, region.getBoundaryPath());
}

void region_translation_and_storage(Output& output, Sequence& sequence, const SkRegion& region) {
    const int dx = pick_int(sequence, -40, 81);
    const int dy = pick_int(sequence, -40, 81);
    SkRegion moved;
    region.translate(dx, dy, &moved);
    record_region(output, moved);
    SkRegion inPlace(region);
    inPlace.translate(dy, dx);
    record_region(output, inPlace);
    record_region(output, region);

    const size_t size = region.writeToMemory(nullptr);
    std::vector<uint8_t> bytes(size);
    region.writeToMemory(bytes.data());
    SkRegion read;
    const size_t consumed = read.readFromMemory(bytes.data(), bytes.size());
    append(output, static_cast<uint64_t>(consumed));
    append_bool(output, read == region);

    SkRegion assigned;
    assigned = region;
    append_bool(output, assigned == region);
    SkRegion swapped(SkIRect::MakeLTRB(1, 2, 3, 4));
    swapped.swap(assigned);
    record_region(output, swapped);
    record_region(output, assigned);
}

void region_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 2000; ++i) {
        const SkRegion a = random_region(sequence);
        const SkRegion b = random_region(sequence);
        record_region(output, a);
        record_region(output, b);
        region_queries(output, sequence, a, b);
        region_traversal(output, sequence, a);
        region_translation_and_storage(output, sequence, a);
        ++output.cases;
    }
}

void region_path_corpus(Output& output, Sequence& sequence) {
    const std::array<SkIRect, 4> clipRects{
        SkIRect::MakeLTRB(0, 0, 64, 64), SkIRect::MakeLTRB(-20, -20, 300, 300),
        SkIRect::MakeLTRB(5, 7, 30, 50),
        // Beyond the scan converter's limit: the path is filled in tiles.
        SkIRect::MakeLTRB(-40000, -40000, 40000, 40000),
    };
    for (int i = 0; i < 2000; ++i) {
        const int size = static_cast<int>(sequence.next() % 3);
        const int span = size == 0 ? 20 : size == 1 ? 80 : 260;
        const SkPath path = random_path(sequence, -10, span);
        SkRegion clip;
        if (sequence.next() % 4 == 0) {
            clip = random_region(sequence);
        } else {
            clip.setRect(clipRects[sequence.pick(clipRects.size())]);
        }
        SkRegion region;
        append_bool(output, region.setPath(path, clip));
        record_region(output, region);
        ++output.cases;
    }
}

SkAAClip random_aaclip(Sequence& sequence) {
    SkAAClip clip;
    const uint32_t kind = sequence.next() % 5;
    if (kind == 0) {
        clip.setRect(random_irect(sequence, -10, 60));
    } else if (kind == 1) {
        clip.setRegion(random_region(sequence));
    } else {
        const SkIRect bounds = SkIRect::MakeLTRB(-8, -8, 72 + pick_int(sequence, 0, 40), 80);
        clip.setPath(random_path(sequence, -10, 70), bounds, kind != 4);
    }
    return clip;
}

// A mask covering clip with a margin, filled with a deterministic pattern.
struct TestMask {
    SkIRect fBounds = {0, 0, 0, 0};
    uint32_t fRowBytes = 0;
    SkMask::Format fFormat = SkMask::kA8_Format;
    std::vector<uint8_t> fImage{};
};

TestMask make_mask(Sequence& sequence, const SkIRect& clip, SkMask::Format format) {
    TestMask mask;
    mask.fFormat = format;
    mask.fBounds = SkIRect::MakeLTRB(clip.fLeft - pick_int(sequence, 0, 4),
                                     clip.fTop - pick_int(sequence, 0, 4),
                                     clip.fRight + pick_int(sequence, 0, 4),
                                     clip.fBottom + pick_int(sequence, 0, 4));
    const int width = mask.fBounds.width();
    size_t planes = 1;
    if (format == SkMask::kBW_Format) {
        mask.fRowBytes = static_cast<uint32_t>((width + 7) >> 3);
    } else if (format == SkMask::kLCD16_Format) {
        mask.fRowBytes = static_cast<uint32_t>(width * 2);
    } else if (format == SkMask::kARGB32_Format) {
        mask.fRowBytes = static_cast<uint32_t>(width * 4);
    } else {
        mask.fRowBytes = static_cast<uint32_t>(width);
        if (format == SkMask::k3D_Format) {
            planes = 3;
        }
    }
    const size_t size = planes * mask.fRowBytes * static_cast<size_t>(mask.fBounds.height());
    mask.fImage.resize(size);
    for (uint8_t& byte : mask.fImage) {
        byte = static_cast<uint8_t>(sequence.next() >> 24);
    }
    return mask;
}

void blit_through_aaclip(Output& output, Sequence& sequence, const SkAAClip& clip) {
    if (clip.isEmpty()) {
        return;
    }
    const SkIRect bounds = clip.getBounds();
    RecordingBlitter recorder(&output);
    SkAAClipBlitterWrapper wrapper(&clip, &recorder);
    SkBlitter* blitter = wrapper.getBlitter();
    record_rect(output, wrapper.getRgn().getBounds());

    for (int i = 0; i < 6; ++i) {
        const int x = pick_int(sequence, bounds.fLeft, bounds.width());
        const int y = pick_int(sequence, bounds.fTop, bounds.height());
        const int width = pick_int(sequence, 1, bounds.fRight - x);
        (*blitter).blitH(x, y, width);

        // Runs stay inside the clip, as the scan converters guarantee.
        std::vector<int16_t> runs(static_cast<size_t>(width) + 1, 0);
        std::vector<SkAlpha> alpha(static_cast<size_t>(width), 0);
        int filled = 0;
        while (filled < width) {
            const int run = std::min(width - filled, pick_int(sequence, 1, 6));
            runs[static_cast<size_t>(filled)] = static_cast<int16_t>(run);
            alpha[static_cast<size_t>(filled)] = static_cast<SkAlpha>(sequence.next() >> 24);
            filled += run;
        }
        (*blitter).blitAntiH(x, y, alpha.data(), runs.data());

        const int height = pick_int(sequence, 1, bounds.fBottom - y);
        (*blitter).blitV(x, y, height, static_cast<SkAlpha>(sequence.next() >> 24));
        (*blitter).blitRect(x, y, width, height);
    }

    const std::array<SkMask::Format, 4> formats{
        SkMask::kA8_Format, SkMask::kBW_Format, SkMask::kLCD16_Format, SkMask::k3D_Format,
    };
    for (const SkMask::Format format : formats) {
        const int left = pick_int(sequence, bounds.fLeft, bounds.width());
        const int top = pick_int(sequence, bounds.fTop, bounds.height());
        const SkIRect maskClip = SkIRect::MakeLTRB(left, top,
                                                   left + pick_int(sequence, 1,
                                                                   bounds.fRight - left),
                                                   top + pick_int(sequence, 1,
                                                                  bounds.fBottom - top));
        const TestMask mask = make_mask(sequence, maskClip, format);
        const SkMask view(mask.fImage.data(), mask.fBounds, mask.fRowBytes, mask.fFormat);
        (*blitter).blitMask(view, maskClip);
    }
    append(output, recorder.calls());
}

void aaclip_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 1500; ++i) {
        const SkAAClip a = random_aaclip(sequence);
        const SkAAClip b = random_aaclip(sequence);
        record_aaclip(output, a);

        const std::array<SkClipOp, 2> ops{SkClipOp::kIntersect, SkClipOp::kDifference};
        for (const SkClipOp op : ops) {
            SkAAClip combined(a);
            append_bool(output, combined.op(b, op));
            record_aaclip(output, combined);

            SkAAClip withRect(a);
            append_bool(output, withRect.op(random_irect(sequence, -10, 70), op));
            record_aaclip(output, withRect);

            const SkRect rect = random_rect(sequence, -10, 70);
            SkAAClip withAARect(a);
            append_bool(output, withAARect.op(rect, op, true));
            record_aaclip(output, withAARect);
            SkAAClip withBWRect(a);
            append_bool(output, withBWRect.op(rect, op, false));
            record_aaclip(output, withBWRect);
        }
        record_aaclip(output, a);

        SkAAClip moved;
        const int dx = pick_int(sequence, -40, 81);
        const int dy = pick_int(sequence, -40, 81);
        append_bool(output, a.translate(dx, dy, &moved));
        record_aaclip(output, moved);
        append_bool(output, a.translate(dx, dy, nullptr));

        for (int j = 0; j < 4; ++j) {
            append_bool(output, a.quickContains(random_irect(sequence, -10, 70)));
        }

        blit_through_aaclip(output, sequence, a);
        ++output.cases;
    }
}

SkMatrix random_matrix(Sequence& sequence) {
    const uint32_t kind = sequence.next() % 5;
    if (kind == 0) {
        return SkMatrix::I();
    }
    if (kind == 1) {
        return SkMatrix::Scale(1.5f, 0.75f);
    }
    if (kind == 2) {
        return SkMatrix::Translate(3.25f, -2.5f);
    }
    if (kind == 3) {
        return SkMatrix::RotateDeg(17.0f, SkPoint{32.0f, 32.0f});
    }
    return SkMatrix::ScaleTranslate(0.5f, 2.0f, 4.0f, 1.0f);
}

void raster_clip_corpus(Output& output, Sequence& sequence) {
    const std::array<SkClipOp, 2> ops{SkClipOp::kIntersect, SkClipOp::kDifference};
    for (int i = 0; i < 1500; ++i) {
        const std::array<int, 3> sizes{64, 100, 37};
        const int width = sizes[sequence.pick(sizes.size())];
        const int height = sizes[sequence.pick(sizes.size())];
        SkRasterClip clip(SkIRect::MakeWH(width, height));
        record_raster_clip(output, clip);
        for (int step = 0; step < 5; ++step) {
            const SkClipOp op = ops[sequence.pick(ops.size())];
            const bool aa = sequence.next() % 2 == 0;
            const uint32_t kind = sequence.next() % 6;
            bool nonEmpty = false;
            if (kind == 0) {
                nonEmpty = clip.op(random_rect(sequence, -10, 80), random_matrix(sequence), op, aa);
            } else if (kind == 1) {
                const SkRRect rrect = SkRRect::MakeRectXY(random_rect(sequence, -10, 80), 3.5f,
                                                          2.0f);
                nonEmpty = clip.op(rrect, random_matrix(sequence), op, aa);
            } else if (kind == 2) {
                nonEmpty = clip.op(random_path(sequence, -10, 80), random_matrix(sequence), op,
                                   aa);
            } else if (kind == 3) {
                nonEmpty = clip.op(random_region(sequence), op);
            } else if (kind == 4) {
                nonEmpty = clip.op(random_irect(sequence, -10, 80), op);
            } else {
                // Exact integer rectangles take the integer path even when antialiased.
                const SkRect rect = SkRect::Make(random_irect(sequence, -10, 80));
                nonEmpty = clip.op(rect, SkMatrix::I(), op, true);
            }
            append_bool(output, nonEmpty);
            record_raster_clip(output, clip);
            append_bool(output, clip.quickContains(random_irect(sequence, -10, 80)));
            append_bool(output, clip.quickReject(random_irect(sequence, -10, 80)));
        }

        // Translate by a nonzero offset into a clip of the other kind.
        SkRasterClip moved(SkPath::Circle(10, 10, 6), SkIRect::MakeWH(30, 30),
                           !clip.isAA());
        const int dx = pick_int(sequence, 1, 40);
        const int dy = pick_int(sequence, -40, 81);
        clip.translate(dx, dy, &moved);
        record_raster_clip(output, moved);

        SkRasterClip copy(clip);
        record_raster_clip(output, copy);
        // Assign over clips of both kinds.
        SkRasterClip assignedBW(SkIRect::MakeWH(20, 20));
        assignedBW = clip;
        record_raster_clip(output, assignedBW);
        SkRasterClip assignedAA(SkPath::Circle(10, 10, 6), SkIRect::MakeWH(30, 30), true);
        assignedAA = clip;
        record_raster_clip(output, assignedAA);
        assignedAA = SkRasterClip(SkIRect::MakeLTRB(2, 3, 9, 11));
        record_raster_clip(output, assignedAA);

        RecordingBlitter recorder(&output);
        SkAAClipBlitterWrapper wrapper(clip, &recorder);
        record_rect(output, wrapper.getBounds());
        if (!clip.isEmpty()) {
            const SkIRect bounds = clip.getBounds();
            (*wrapper.getBlitter()).blitH(bounds.fLeft, bounds.fTop, bounds.width());
            (*wrapper.getBlitter()).blitRect(bounds.fLeft, bounds.fTop, bounds.width(),
                                             bounds.height());
        }
        append(output, recorder.calls());
        ++output.cases;
    }
}

void raster_clip_stack_corpus(Output& output, Sequence& sequence) {
    const std::array<SkClipOp, 2> ops{SkClipOp::kIntersect, SkClipOp::kDifference};
    for (int i = 0; i < 400; ++i) {
        SkRasterClipStack stack(80, 60);
        int depth = 0;
        for (int step = 0; step < 12; ++step) {
            const uint32_t kind = sequence.next() % 7;
            const SkClipOp op = ops[sequence.pick(ops.size())];
            const bool aa = sequence.next() % 2 == 0;
            if (kind == 0) {
                stack.save();
                depth += 1;
            } else if (kind == 1 && depth > 0) {
                stack.restore();
                depth -= 1;
            } else if (kind == 2) {
                stack.clipRect(random_matrix(sequence), random_rect(sequence, -10, 90), op, aa);
            } else if (kind == 3) {
                const SkRRect rrect = SkRRect::MakeRectXY(random_rect(sequence, -10, 90), 4.0f,
                                                          4.0f);
                stack.clipRRect(random_matrix(sequence), rrect, op, aa);
            } else if (kind == 4) {
                stack.clipPath(random_matrix(sequence), random_path(sequence, -10, 90), op, aa);
            } else if (kind == 5) {
                stack.clipRegion(random_region(sequence), op);
            } else {
                stack.replaceClip(random_irect(sequence, -10, 90));
            }
            append(output, static_cast<uint8_t>(kind));
            record_raster_clip(output, stack.rc());
        }
        while (depth > 0) {
            stack.restore();
            depth -= 1;
            record_raster_clip(output, stack.rc());
        }
        ++output.cases;
    }
}

void region_clip_corpus(Output& output) {
    Sequence sequence{};
    region_corpus(output, sequence);
    region_path_corpus(output, sequence);
    aaclip_corpus(output, sequence);
    raster_clip_corpus(output, sequence);
    raster_clip_stack_corpus(output, sequence);
}

#ifndef STX_SKIA_REFERENCE
// The antialiased clip's blitter has no coverage merge for 32-bit masks. A partly covered
// mask of that format is now ignored; it used to call through a null function pointer.
bool unsupported_mask_format_draws_nothing() {
    SkAAClip clip;
    if (!clip.setPath(SkPath::Circle(16, 16, 12), SkIRect::MakeWH(32, 32), true)) return false;
    Output scratch{};
    RecordingBlitter recorder(&scratch);
    SkAAClipBlitterWrapper wrapper(&clip, &recorder);
    Sequence sequence{};
    const SkIRect maskClip = SkIRect::MakeLTRB(2, 2, 30, 30);
    const TestMask mask = make_mask(sequence, maskClip, SkMask::kARGB32_Format);
    const SkMask view(mask.fImage.data(), mask.fBounds, mask.fRowBytes, mask.fFormat);
    (*wrapper.getBlitter()).blitMask(view, maskClip);
    return recorder.calls() == 0;
}

// Assigning an integer clip over an antialiased one leaves only the integer form.
bool assignment_replaces_representation() {
    SkRasterClip clip(SkPath::Circle(16, 16, 12), SkIRect::MakeWH(32, 32), true);
    if (!clip.isAA()) return false;
    const SkRasterClip rect(SkIRect::MakeLTRB(1, 2, 9, 10));
    clip = rect;
    if (!clip.isBW() || clip.bwRgn() != rect.bwRgn()) return false;
    clip = SkRasterClip(SkPath::Circle(16, 16, 12), SkIRect::MakeWH(32, 32), true);
    return clip.isAA() && !clip.isEmpty();
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
#ifndef STX_SKIA_REFERENCE
    if (!unsupported_mask_format_draws_nothing()) {
        std::fprintf(stderr, "unsupported mask format drew through the clip\n");
        return 1;
    }
    if (!assignment_replaces_representation()) {
        std::fprintf(stderr, "raster clip assignment kept the wrong representation\n");
        return 1;
    }
#endif
    Output output{};
    region_clip_corpus(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("region clips: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
