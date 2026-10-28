// Differential corpus for the canvas clip hooks kept for Android framework use: the device clip
// restriction, clip resets that respect it, androidFramework_isClipAA and
// temporary_internal_getRgnClip. Random sequences of saves, layers, restores, clips, resets,
// restrictions and fills run on raster canvases, one wide enough to disable antialiased
// clipping, and on a no-draw canvas. After every step the corpus records the save count, the
// device clip bounds and kind, whether the clip is antialiased and the clip region; each run
// ends with a hash of the pixels. Both builds must produce identical bytes (android-clip.raw).
// The same runs on one no-draw canvas reset between runs go to android-clip-reset.raw, which is
// meant to differ: the reference keeps a root-level restriction across the reset, and the
// candidate ends it. Arguments beginning with "--" select a Release-check stop instead.

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
#include "include/core/SkRefCnt.h"
#include "include/core/SkRegion.h"
#include "include/core/SkSurface.h"
#include "include/utils/SkNoDrawCanvas.h"
#include "src/core/SkCanvasPriv.h"
#include "src/core/SkDevice.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace skiatest {
struct AndroidClipKey;

// SkCanvas befriends TestCanvas so tests can reach its private Android hooks.
template <>
class TestCanvas<AndroidClipKey> {
public:
    static bool IsClipAA(const SkCanvas& canvas) { return canvas.androidFramework_isClipAA(); }
};
}  // namespace skiatest

// Stencil clipping was a GPU device hook; no CPU device overrode or called it.
template <typename T>
concept HasStencilClip = requires(T& device) { device.android_utils_clipWithStencil(); };
#ifndef STX_SKIA_REFERENCE
static_assert(!HasStencilClip<SkDevice>);
#else
static_assert(HasStencilClip<SkDevice>);
#endif

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;
using ClipAccess = skiatest::TestCanvas<skiatest::AndroidClipKey>;

void append_rect(Output& output, const SkIRect& rect) {
    append(output, rect.fLeft);
    append(output, rect.fTop);
    append(output, rect.fRight);
    append(output, rect.fBottom);
}

void record_region(Output& output, const SkRegion& region) {
    append_rect(output, region.getBounds());
    int rects = 0;
    for (SkRegion::Iterator iter(region); !iter.done(); iter.next()) ++rects;
    append(output, rects);
    for (SkRegion::Iterator iter(region); !iter.done(); iter.next()) {
        append_rect(output, iter.rect());
    }
}

void record_clip(Output& output, SkCanvas& canvas) {
    append(output, canvas.getSaveCount());
    append_rect(output, canvas.getDeviceClipBounds());
    append_bool(output, canvas.isClipEmpty());
    append_bool(output, canvas.isClipRect());
    append_bool(output, ClipAccess::IsClipAA(canvas));
    SkRegion region;
    canvas.temporary_internal_getRgnClip(&region);
    record_region(output, region);
}

float coordinate(Sequence& sequence, int span) {
    const uint32_t choices = static_cast<uint32_t>(span) * 4 + 9;
    const int quarters = static_cast<int>(sequence.next() % choices) - 8;
    return static_cast<float>(quarters) / 4.0f;
}

// Values are drawn in named order: function arguments are evaluated in unspecified order.
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
    const SkIRect irect = SkIRect::MakeLTRB(static_cast<int>(rect.fLeft),
                                            static_cast<int>(rect.fTop),
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

// What the test knows of the canvas: which saves are layers, and the save count at which the
// active restriction, if any, was set.
struct State {
    std::vector<bool> layers{};
    int layerCount = 0;
    int restrictionSaveCount = -1;
};

void restore(SkCanvas& canvas, State& state) {
    if (state.layers.back()) --state.layerCount;
    state.layers.pop_back();
    canvas.restore();
    // The restriction ends once the canvas restores past the save level that set it.
    if (canvas.getSaveCount() < state.restrictionSaveCount) state.restrictionSaveCount = -1;
}

// Applies one random operation. A restriction is set only where its contract allows: on the
// root device and with no restriction active.
void apply_operation(Sequence& sequence, SkCanvas& canvas, State& state, int width, int height) {
    const SkClipOp op = sequence.pick(4) == 0 ? SkClipOp::kDifference : SkClipOp::kIntersect;
    const bool aa = sequence.pick(2) == 0;
    switch (sequence.pick(12)) {
        case 0:
        case 1:
            canvas.save();
            state.layers.push_back(false);
            break;
        case 2: {
            const bool bounded = sequence.pick(2) == 0;
            const SkRect bounds = random_rect(sequence, width, height).makeSorted();
            SkPaint paint;
            paint.setAlphaf(static_cast<float>(1 + sequence.pick(4)) / 4.0f);
            canvas.saveLayer(bounded ? &bounds : nullptr, &paint);
            state.layers.push_back(true);
            ++state.layerCount;
            break;
        }
        case 3:
            if (!state.layers.empty()) restore(canvas, state);
            break;
        case 4: {
            const SkMatrix transform = random_transform(sequence);
            const SkRect rect = random_rect(sequence, width, height);
            canvas.setMatrix(transform);
            canvas.clipRect(rect, op, aa);
            break;
        }
        case 5: {
            const SkRect bounds = random_rect(sequence, width, height).makeSorted();
            const float rx = coordinate(sequence, 4);
            const float ry = coordinate(sequence, 4);
            const SkRRect rrect = SkRRect::MakeRectXY(bounds, rx, ry);
            const SkMatrix transform = random_transform(sequence);
            canvas.setMatrix(transform);
            canvas.clipRRect(rrect, op, aa);
            break;
        }
        case 6: {
            std::array<SkPoint, 3> triangle{};
            for (SkPoint& point : triangle) {
                const float x = coordinate(sequence, width);
                const float y = coordinate(sequence, height);
                point = SkPoint{x, y};
            }
            const SkRect oval = random_rect(sequence, width, height).makeSorted();
            const SkSpan<const SkPoint> points(triangle.data(), triangle.size());
            const SkPath path = sequence.pick(2) == 0 ? SkPath::Oval(oval)
                                                      : SkPath::Polygon(points, true);
            const SkMatrix transform = random_transform(sequence);
            canvas.setMatrix(transform);
            canvas.clipPath(path, op, aa);
            break;
        }
        case 7: {
            SkRegion region(random_irect(sequence, width, height));
            const int x = static_cast<int>(sequence.pick(8));
            const int y = static_cast<int>(sequence.pick(8));
            region.op(SkIRect::MakeXYWH(x, y, 5, 3), SkRegion::kUnion_Op);
            canvas.clipRegion(region, op);
            break;
        }
        case 8:
            SkCanvasPriv::ResetClip(&canvas);
            break;
        case 9:
        case 10: {
            const SkIRect rect = random_irect(sequence, width, height).makeSorted();
            if (state.layerCount == 0 && state.restrictionSaveCount < 0) {
                canvas.androidFramework_setDeviceClipRestriction(rect);
                if (!rect.isEmpty()) state.restrictionSaveCount = canvas.getSaveCount();
            }
            break;
        }
        default: {
            SkPaint paint;
            paint.setColor(static_cast<SkColor>(sequence.next() | 0xFF000000u));
            paint.setAntiAlias(aa);
            const SkRect rect = random_rect(sequence, width, height).makeSorted();
            canvas.drawRect(rect, paint);
            break;
        }
    }
}

void record_pixels(Output& output, SkSurface* surface) {
    SkPixmap pixels;
    if (surface == nullptr || !(*surface).peekPixels(&pixels)) {
        append_bool(output, false);
        return;
    }
    append_bool(output, true);
    std::vector<uint8_t> bytes{};
    const size_t rowBytes = pixels.info().minRowBytes();
    for (int y = 0; y < pixels.height(); ++y) {
        const uint8_t* row = static_cast<const uint8_t*>(pixels.addr(0, y));
        bytes.insert(bytes.end(), row, row + rowBytes);
    }
    append(output, corpus::fnv1a(bytes));
}

void run(Output& output, Sequence& sequence, SkCanvas& canvas, SkSurface* surface, int width,
         int height) {
    State state{};
    // Some runs restrict the root save level, which no restore can end.
    if (sequence.pick(4) == 0) {
        const SkIRect rect = random_irect(sequence, width, height).makeSorted();
        canvas.androidFramework_setDeviceClipRestriction(rect);
        if (!rect.isEmpty()) state.restrictionSaveCount = canvas.getSaveCount();
    }
    record_clip(output, canvas);
    for (int step = 0; step < 120; ++step) {
        apply_operation(sequence, canvas, state, width, height);
        record_clip(output, canvas);
        ++output.cases;
    }
    while (!state.layers.empty()) {
        restore(canvas, state);
        record_clip(output, canvas);
    }
    record_pixels(output, surface);
    ++output.cases;
}

void shared_corpus(Output& output) {
    struct Size {
        int width = 0;
        int height = 0;
    };
    const std::array<Size, 3> sizes{Size{64, 48}, Size{1, 1}, Size{20000, 8}};
    Sequence sequence{};
    for (const Size size : sizes) {
        for (int i = 0; i < 12; ++i) {
            const SkImageInfo info = SkImageInfo::MakeN32Premul(size.width, size.height);
            sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
            if (!surface) {
                append_bool(output, false);
                continue;
            }
            SkCanvas& canvas = *(*surface).getCanvas();
            canvas.clear(SK_ColorTRANSPARENT);
            run(output, sequence, canvas, surface.get(), size.width, size.height);
        }
    }
    for (int i = 0; i < 12; ++i) {
        SkNoDrawCanvas canvas(64, 48);
        run(output, sequence, canvas, nullptr, 64, 48);
    }
}

// One no-draw canvas, reset to a new size before each run, or with fresh set, a new canvas
// of that size for each run.
void reset_corpus(Output& output, bool fresh) {
    Sequence sequence{};
    SkNoDrawCanvas canvas(64, 48);
    for (int i = 0; i < 24; ++i) {
        const int width = 16 + static_cast<int>(sequence.pick(64));
        const int height = 16 + static_cast<int>(sequence.pick(48));
        if (fresh) {
            SkNoDrawCanvas created(width, height);
            run(output, sequence, created, nullptr, width, height);
        } else {
            canvas.resetCanvas(width, height);
            run(output, sequence, canvas, nullptr, width, height);
        }
    }
}

#ifndef STX_SKIA_REFERENCE
// A restriction at the root save level lasts until the canvas is reset, and only then can
// another be set.
bool reset_ends_restriction_case() {
    SkNoDrawCanvas canvas(64, 48);
    const SkIRect restriction = SkIRect::MakeLTRB(8, 8, 24, 24);
    canvas.androidFramework_setDeviceClipRestriction(restriction);
    SkCanvasPriv::ResetClip(&canvas);
    if (canvas.getDeviceClipBounds() != restriction) return false;
    canvas.resetCanvas(64, 48);
    SkCanvasPriv::ResetClip(&canvas);
    if (canvas.getDeviceClipBounds() != SkIRect::MakeWH(64, 48)) return false;
    const SkIRect next = SkIRect::MakeLTRB(0, 0, 4, 4);
    canvas.androidFramework_setDeviceClipRestriction(next);
    SkCanvasPriv::ResetClip(&canvas);
    return canvas.getDeviceClipBounds() == next;
}

// A nested restriction holds through clip resets, ends when the canvas restores past its save
// level, and can then be set again.
bool nested_restriction_case() {
    const SkImageInfo info = SkImageInfo::MakeN32Premul(32, 32);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.save();
    const SkIRect restriction = SkIRect::MakeLTRB(4, 4, 12, 12);
    canvas.androidFramework_setDeviceClipRestriction(restriction);
    canvas.save();
    canvas.clipRect(SkRect::MakeLTRB(6, 6, 30, 30));
    SkCanvasPriv::ResetClip(&canvas);
    if (canvas.getDeviceClipBounds() != restriction) return false;
    canvas.restore();
    if (canvas.getDeviceClipBounds() != restriction) return false;
    canvas.restore();
    SkCanvasPriv::ResetClip(&canvas);
    if (canvas.getDeviceClipBounds() != SkIRect::MakeWH(32, 32)) return false;
    // An empty restriction does nothing, even at the root save level.
    canvas.androidFramework_setDeviceClipRestriction(SkIRect::MakeEmpty());
    canvas.androidFramework_setDeviceClipRestriction(restriction);
    return canvas.getDeviceClipBounds() == restriction;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 1 || argc > 3) return 2;
#ifndef STX_SKIA_REFERENCE
    if (argc == 2 && std::strcmp(argv[1], "--restrict-twice") == 0) {
        SkNoDrawCanvas invalid(16, 16);
        invalid.androidFramework_setDeviceClipRestriction(SkIRect::MakeWH(8, 8));
        invalid.androidFramework_setDeviceClipRestriction(SkIRect::MakeWH(4, 4));
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--restrict-in-layer") == 0) {
        const SkImageInfo info = SkImageInfo::MakeN32Premul(16, 16);
        sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
        if (!surface) return 1;
        SkCanvas& invalid = *(*surface).getCanvas();
        invalid.saveLayer(nullptr, nullptr);
        invalid.androidFramework_setDeviceClipRestriction(SkIRect::MakeWH(8, 8));
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--null-region") == 0) {
        SkNoDrawCanvas invalid(16, 16);
        invalid.temporary_internal_getRgnClip(nullptr);
        return 0;
    }
    // A reset canvas behaves as a new one.
    Output reused{};
    Output created{};
    reset_corpus(reused, false);
    reset_corpus(created, true);
    bool valid = reused.bytes == created.bytes;
    valid = reset_ends_restriction_case() && valid;
    valid = nested_restriction_case() && valid;
    if (!valid) {
        std::fputs("android clip contract mismatch\n", stderr);
        return 1;
    }
#endif
    Output output{};
    Output reset{};
    shared_corpus(output);
    reset_corpus(reset, false);
    if (argc >= 2 && !corpus::write_file(argv[1], output)) return 2;
    if (argc == 3 && !corpus::write_file(argv[2], reset)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    const unsigned long long resetHash = corpus::fnv1a(reset.bytes);
    std::printf("android clip: %zu cases, %zu bytes, hash=%llu; reset %zu cases, hash=%llu PASS\n",
                output.cases, output.bytes.size(), hash, reset.cases, resetHash);
    return 0;
}
