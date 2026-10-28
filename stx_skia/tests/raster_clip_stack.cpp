// Contract checks for the raster clip stack, and a differential corpus (raster-clip-stack.raw)
// over random sequences of saves, restores and every clip operation on stacks of several sizes,
// including one wide enough to disable antialiased clipping. Both builds must produce identical
// bytes. Sequences that also replace the clip go to raster-clip-stack-replaced.raw, which is
// meant to differ. Arguments beginning with "--" select a Release-check stop instead.

#include "differential_corpus.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkClipOp.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkRegion.h"
#include "include/core/SkShader.h"
#include "include/core/SkSurface.h"
#include "src/core/SkCanvasPriv.h"
#include "src/core/SkRasterClipStack.h"

#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
bool fractional_clip_is_aa(SkRasterClipStack& stack) {
    const SkMatrix transform = SkMatrix::I();
    const SkRect rectangle = SkRect::MakeLTRB(0.25f, 0.25f, 7.75f, 7.75f);
    stack.clipRect(transform, rectangle, SkClipOp::kIntersect, true);
    return stack.rc().isAA();
}

bool ordinary_clip_case() {
    SkRasterClipStack small(16, 16);
    if (!fractional_clip_is_aa(small)) return false;
    SkRasterClipStack large(20000, 16);
    if (fractional_clip_is_aa(large)) return false;
    return true;
}


using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

void record_clip(Output& output, const SkRasterClip& clip) {
    append_bool(output, clip.isEmpty());
    append_bool(output, clip.isRect());
    append_bool(output, clip.isAA());
    append_bool(output, clip.isComplex());
    // An empty clip draws nothing whatever its shader; the candidate drops the shader of an
    // emptied clip and the reference keeps it, so only a non-empty clip's shader is recorded.
    append_bool(output, !clip.isEmpty() && clip.clipShader() != nullptr);
    const SkIRect& bounds = clip.getBounds();
    append(output, bounds.fLeft);
    append(output, bounds.fTop);
    append(output, bounds.fRight);
    append(output, bounds.fBottom);
    if (clip.isBW()) {
        int rects = 0;
        for (SkRegion::Iterator iter(clip.bwRgn()); !iter.done(); iter.next()) ++rects;
        append(output, rects);
        for (SkRegion::Iterator iter(clip.bwRgn()); !iter.done(); iter.next()) {
            const SkIRect& r = iter.rect();
            append(output, r.fLeft);
            append(output, r.fTop);
            append(output, r.fRight);
            append(output, r.fBottom);
        }
    } else {
        const SkIRect& aaBounds = clip.aaRgn().getBounds();
        append(output, aaBounds.fLeft);
        append(output, aaBounds.fTop);
        append(output, aaBounds.fRight);
        append(output, aaBounds.fBottom);
        append_bool(output, clip.aaRgn().isRect());
    }
}

float coordinate(Sequence& sequence, int span) {
    const uint32_t choices = static_cast<uint32_t>(span) * 4 + 9;
    const int quarters = static_cast<int>(sequence.next() % choices) - 8;
    return static_cast<float>(quarters) / 4.0f;
}

// Draws are taken in named order: function arguments are evaluated in unspecified order.
SkRect random_rect(Sequence& sequence, int width, int height) {
    const float left = coordinate(sequence, width);
    const float top = coordinate(sequence, height);
    const float right = coordinate(sequence, width);
    const float bottom = coordinate(sequence, height);
    const SkRect rect = SkRect::MakeLTRB(left, top, right, bottom);
    return rect;
}

SkIRect random_irect(Sequence& sequence, int width, int height) {
    const SkRect rect = random_rect(sequence, width, height);
    const SkIRect irect = SkIRect::MakeLTRB(static_cast<int>(rect.fLeft), static_cast<int>(rect.fTop),
                                            static_cast<int>(rect.fRight),
                                            static_cast<int>(rect.fBottom));
    return irect;
}

SkMatrix random_transform(Sequence& sequence) {
    switch (sequence.pick(5)) {
        case 0:
            return SkMatrix::I();
        case 1: {
            const float dx = coordinate(sequence, 8);
            const float dy = coordinate(sequence, 8);
            return SkMatrix::Translate(dx, dy);
        }
        case 2: {
            const float sx = 0.5f + static_cast<float>(sequence.pick(8)) / 4.0f;
            const float sy = 0.75f + static_cast<float>(sequence.pick(4)) / 4.0f;
            return SkMatrix::Scale(sx, sy);
        }
        case 3:
            return SkMatrix::RotateDeg(static_cast<float>(sequence.pick(12)) * 15.0f,
                                       SkPoint{16, 12});
        default: {
            SkMatrix skew = SkMatrix::I();
            skew.setSkew(0.25f, -0.125f);
            return skew;
        }
    }
}

// Applies one random operation. Restores never outnumber saves, which the canvas guarantees.
// Replacement is drawn only when allowed: the candidate drops a shader clip on replacement
// (2026-10-26) and the reference keeps it.
void apply_operation(Sequence& sequence, SkRasterClipStack& stack, int& depth, int width,
                     int height, bool allowReplace) {
    const SkClipOp op = sequence.pick(4) == 0 ? SkClipOp::kDifference : SkClipOp::kIntersect;
    const bool aa = sequence.pick(2) == 0;
    size_t choice = sequence.pick(allowReplace ? 9 : 8);
    if (!allowReplace && choice >= 7) choice += 1;
    switch (choice) {
        case 0:
        case 1:
            stack.save();
            ++depth;
            break;
        case 2:
            if (depth > 0) {
                stack.restore();
                --depth;
            }
            break;
        case 3: {
            const SkMatrix transform = random_transform(sequence);
            const SkRect rect = random_rect(sequence, width, height);
            stack.clipRect(transform, rect, op, aa);
            break;
        }
        case 4: {
            const SkRect bounds = random_rect(sequence, width, height).makeSorted();
            const float rx = coordinate(sequence, 4);
            const float ry = coordinate(sequence, 4);
            const SkRRect rrect = SkRRect::MakeRectXY(bounds, rx, ry);
            const SkMatrix transform = random_transform(sequence);
            stack.clipRRect(transform, rrect, op, aa);
            break;
        }
        case 5: {
            std::array<SkPoint, 3> triangle{};
            for (SkPoint& point : triangle) {
                point = SkPoint{coordinate(sequence, width), coordinate(sequence, height)};
            }
            const SkRect oval = random_rect(sequence, width, height).makeSorted();
            const SkSpan<const SkPoint> points(triangle.data(), triangle.size());
            const SkPath path = sequence.pick(2) == 0 ? SkPath::Oval(oval)
                                                      : SkPath::Polygon(points, true);
            const SkMatrix transform = random_transform(sequence);
            stack.clipPath(transform, path, op, aa);
            break;
        }
        case 6: {
            SkRegion region(random_irect(sequence, width, height));
            const int x = static_cast<int>(sequence.pick(8));
            const int y = static_cast<int>(sequence.pick(8));
            region.op(SkIRect::MakeXYWH(x, y, 5, 3), SkRegion::kUnion_Op);
            stack.clipRegion(region, op);
            break;
        }
        case 7:
            stack.replaceClip(random_irect(sequence, width, height).makeOutset(0, 4));
            break;
        default:
            stack.clipShader(SkShaders::Color(SkColorSetARGB(
                    static_cast<uint8_t>(64 + sequence.pick(128)), 0, 0, 0)));
            break;
    }
}

void sequence_corpus(Output& output, bool allowReplace) {
    struct Size {
        int width = 0;
        int height = 0;
    };
    const std::array<Size, 5> sizes{Size{64, 48}, Size{1, 1}, Size{0, 0}, Size{0, 9},
                                    Size{20000, 8}};
    Sequence sequence{};
    for (const Size size : sizes) {
        for (int run = 0; run < 8; ++run) {
            SkRasterClipStack stack(size.width, size.height);
            int depth = 0;
            for (int step = 0; step < 160; ++step) {
                apply_operation(sequence, stack, depth, size.width, size.height, allowReplace);
                append(output, depth);
                record_clip(output, stack.rc());
                ++output.cases;
            }
            // Unwind every save; the root clip must be the untouched device bounds.
            while (depth > 0) {
                stack.restore();
                --depth;
                record_clip(output, stack.rc());
            }
            ++output.cases;
        }
    }
}

#ifndef STX_SKIA_REFERENCE
bool resized_clip_case() {
    SkRasterClipStack grown(16, 16);
    grown.setNewSize(20000, 16);
    if (fractional_clip_is_aa(grown)) return false;

    SkRasterClipStack shrunk(20000, 16);
    shrunk.setNewSize(16, 16);
    if (!fractional_clip_is_aa(shrunk)) return false;
    return true;
}

bool replacement_drops_shader_case() {
    SkRasterClipStack stack(8, 8);
    stack.clipShader(SkShaders::Color(SkColorSetARGB(128, 0, 0, 0)));
    stack.replaceClip(SkIRect::MakeWH(8, 8));
    if (stack.rc().clipShader()) return false;

    stack.clipShader(SkShaders::Color(SkColorSetARGB(128, 0, 0, 0)));
    stack.setNewSize(16, 8);
    if (stack.rc().clipShader()) return false;

    const SkImageInfo info = SkImageInfo::MakeN32Premul(8, 8);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.clear(SK_ColorTRANSPARENT);
    canvas.clipShader(SkShaders::Color(SkColorSetARGB(128, 0, 0, 0)));
    SkCanvasPriv::ResetClip(&canvas);
    SkPaint paint;
    paint.setColor(SK_ColorWHITE);
    canvas.drawRect(SkRect::MakeWH(8, 8), paint);
    SkPixmap pixels;
    if (!(*surface).peekPixels(&pixels)) return false;
    return SkColorGetA(pixels.getColor(1, 1)) == 255;
}

bool aa_translation_preserves_bounds_case() {
    const SkRect fractional = SkRect::MakeLTRB(0.25f, 0.25f, 4.75f, 4.75f);
    const SkPath path = SkPath::Rect(fractional);
    const SkIRect limit = SkIRect::MakeWH(8, 8);
    const SkRasterClip source(path, limit, true);
    if (!source.isAA() || source.isEmpty()) return false;
    const int originalWidth = source.getBounds().width();
    const int originalHeight = source.getBounds().height();

    SkAAClip direct;
    source.aaRgn().translate(INT_MAX, INT_MAX, &direct);
    if (direct.isEmpty() || direct.getBounds().width() != originalWidth ||
        direct.getBounds().height() != originalHeight) {
        std::fputs("direct AA translation changed bounds\n", stderr);
        return false;
    }

    SkRasterClip raster;
    source.translate(INT_MAX, INT_MAX, &raster);
    if (raster.isEmpty() || raster.getBounds().width() != originalWidth ||
        raster.getBounds().height() != originalHeight) {
        std::fputs("raster AA translation changed bounds\n", stderr);
        return false;
    }

    SkAAClip inPlace = source.aaRgn();
    inPlace.translate(INT_MAX, INT_MAX, &inPlace);
    if (inPlace.isEmpty() || inPlace.getBounds().width() != originalWidth ||
        inPlace.getBounds().height() != originalHeight) {
        std::fputs("in-place AA translation changed bounds\n", stderr);
        return false;
    }

    const SkRect negativeFractional = SkRect::MakeLTRB(-4.75f, -4.75f, -0.25f, -0.25f);
    const SkPath negativePath = SkPath::Rect(negativeFractional);
    const SkIRect negativeLimit = SkIRect::MakeLTRB(-8, -8, 0, 0);
    const SkRasterClip negativeSource(negativePath, negativeLimit, true);
    if (!negativeSource.isAA() || negativeSource.isEmpty()) {
        std::fputs("negative AA source was empty\n", stderr);
        return false;
    }
    SkRasterClip negativeRaster;
    negativeSource.translate(INT_MIN, INT_MIN, &negativeRaster);
    if (negativeRaster.isEmpty() ||
        negativeRaster.getBounds().width() != negativeSource.getBounds().width() ||
        negativeRaster.getBounds().height() != negativeSource.getBounds().height()) {
        std::fputs("negative AA translation changed bounds\n", stderr);
        return false;
    }
    return true;
}

// A null shader clip restricts nothing, so it leaves an earlier shader clip in place.
bool null_shader_keeps_clip_case() {
    SkRasterClipStack stack(8, 8);
    const sk_sp<SkShader> shader = SkShaders::Color(SkColorSetARGB(128, 0, 0, 0));
    stack.clipShader(shader);
    stack.clipShader(nullptr);
    if (stack.rc().clipShader() != shader) return false;
    // Without an earlier shader clip it still adds none.
    SkRasterClipStack plain(8, 8);
    plain.clipShader(nullptr);
    if (plain.rc().clipShader() != nullptr || plain.rc().isEmpty()) return false;
    // Deferred saves and their restores leave the clip and its shader as they were.
    stack.save();
    stack.save();
    stack.restore();
    stack.restore();
    return stack.rc().clipShader() == shader;
}

bool bw_translation_preserves_bounds_case() {
    const SkIRect original = SkIRect::MakeLTRB(-5, -5, 0, 0);
    const SkRasterClip source(original);
    SkRasterClip translated;
    source.translate(INT_MIN, INT_MIN, &translated);
    if (translated.isEmpty()) return false;
    if (translated.getBounds().width() != original.width() ||
        translated.getBounds().height() != original.height()) return false;
    return true;
}
#endif
}

int main(int argc, char** argv) {
#ifndef STX_SKIA_REFERENCE
    if (argc == 2 && std::strcmp(argv[1], "--resize-saved") == 0) {
        SkRasterClipStack invalid(16, 16);
        invalid.save();
        invalid.setNewSize(32, 32);
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--negative-size") == 0) {
        SkRasterClipStack invalid(16, 16);
        invalid.setNewSize(-1, 16);
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--negative-construction") == 0) {
        SkRasterClipStack invalid(16, -1);
        return invalid.rc().isEmpty() ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--unmatched-restore") == 0) {
        // A save that changed the clip, its restore, and one restore too many.
        SkRasterClipStack invalid(16, 16);
        invalid.save();
        invalid.clipRect(SkMatrix::I(), SkRect::MakeWH(4, 4), SkClipOp::kIntersect, false);
        invalid.restore();
        invalid.restore();
        return invalid.rc().isEmpty() ? 0 : 1;
    }
#endif
    bool valid = ordinary_clip_case();
#ifndef STX_SKIA_REFERENCE
    valid = resized_clip_case() && valid;
    valid = replacement_drops_shader_case() && valid;
    valid = aa_translation_preserves_bounds_case() && valid;
    valid = bw_translation_preserves_bounds_case() && valid;
    valid = null_shader_keeps_clip_case() && valid;
#endif
    if (!valid) {
        std::fputs("raster clip stack mismatch\n", stderr);
        return 1;
    }
    Output output{};
    Output replaced{};
    sequence_corpus(output, false);
    sequence_corpus(replaced, true);
    if (argc >= 2 && !corpus::write_file(argv[1], output)) return 2;
    if (argc == 3 && !corpus::write_file(argv[2], replaced)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    const unsigned long long replacedHash = corpus::fnv1a(replaced.bytes);
    std::printf("raster clip stack: %zu cases, %zu bytes, hash=%llu; replaced %zu cases, "
                "hash=%llu PASS\n",
                output.cases, output.bytes.size(), hash, replaced.cases, replacedHash);
    return 0;
}
