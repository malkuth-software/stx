// Differential corpus for the blitters: the blitters SkBlitter::Choose selects (the legacy
// 32-bit blitters and the raster pipeline blitter) on six color types with varied colors, blend
// modes, dithering, gradients, color filters and clip shaders; the rectangle and region clipping
// blitters and SkBlitterClipper; the base class's default operations; the A8 blitters; the
// sprite blitters; and the 32-bit shader blitter with a test shader context. Every call is
// issued within the destination; the resulting pixels are hashed into the corpus.

#include "differential_corpus.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkRegion.h"
#include "include/core/SkPoint.h"
#include "include/core/SkShader.h"
#include "include/core/SkSpan.h"
#include "include/core/SkSurfaceProps.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkGradient.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkBlitter.h"
#include "src/core/SkBlitter_A8.h"
#include "src/core/SkCoreBlitters.h"
#include "src/core/SkDrawTypes.h"
#include "src/core/SkMask.h"
#include "src/shaders/SkShaderBase.h"
#include "gradient_result.hpp"

#include <algorithm>
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

int pick_int(Sequence& sequence, int origin, int span) {
    const uint32_t offset = sequence.next() % static_cast<uint32_t>(span);
    const int value = origin + static_cast<int>(offset);
    return value;
}

uint8_t pick_byte(Sequence& sequence) {
    const uint8_t byte = static_cast<uint8_t>(sequence.next() >> 24);
    return byte;
}

// Owns the pixels of a destination or source pixmap. Half-float pixels hold small finite
// values; other formats hold arbitrary bytes.
struct Pixels {
    SkImageInfo fInfo;
    std::vector<uint64_t> fStorage{};  // 8-byte aligned for every pixel size

    SkPixmap pixmap() {
        const SkPixmap view(fInfo, fStorage.data(), fInfo.minRowBytes());
        return view;
    }
};

Pixels make_pixels(Sequence& sequence, SkColorType colorType, SkAlphaType alphaType, int width,
                   int height) {
    Pixels pixels;
    pixels.fInfo = SkImageInfo::Make(width, height, colorType, alphaType);
    const size_t bytes = pixels.fInfo.computeMinByteSize();
    pixels.fStorage.assign((bytes + 7) / 8, 0);
    uint8_t* raw = reinterpret_cast<uint8_t*>(pixels.fStorage.data());
    if (colorType == kRGBA_F16_SkColorType) {
        const std::array<uint16_t, 4> halves{0x0000, 0x3400, 0x3800, 0x3C00};
        uint16_t* channels = reinterpret_cast<uint16_t*>(raw);
        for (size_t i = 0; i < bytes / 2; ++i) {
            channels[i] = halves[sequence.pick(halves.size())];
        }
    } else {
        for (size_t i = 0; i < bytes; ++i) {
            raw[i] = pick_byte(sequence);
        }
    }
    return pixels;
}

void record_pixels(Output& output, const Pixels& pixels) {
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(pixels.fStorage.data());
    const std::vector<uint8_t> bytes(raw, raw + pixels.fInfo.computeMinByteSize());
    append(output, corpus::fnv1a(bytes));
}

// A mask inside bounds with arbitrary coverage, padding bits clear.
struct TestMask {
    SkIRect fBounds = {0, 0, 0, 0};
    uint32_t fRowBytes = 0;
    SkMask::Format fFormat = SkMask::kA8_Format;
    std::vector<uint8_t> fImage{};

    SkMask view() const {
        const SkMask mask(fImage.data(), fBounds, fRowBytes, fFormat);
        return mask;
    }
};

TestMask make_mask(Sequence& sequence, const SkIRect& bounds, SkMask::Format format) {
    TestMask mask;
    mask.fBounds = bounds;
    mask.fFormat = format;
    const int width = bounds.width();
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
    const size_t rowBytes = mask.fRowBytes;
    const size_t height = static_cast<size_t>(bounds.height());
    mask.fImage.assign(planes * rowBytes * height, 0);
    for (uint8_t& byte : mask.fImage) {
        byte = pick_byte(sequence);
    }
    if (format == SkMask::kBW_Format && (width & 7) != 0) {
        const uint8_t keep = static_cast<uint8_t>(0xFF00U >> (width & 7));
        for (size_t row = 0; row < height; ++row) {
            mask.fImage[row * rowBytes + rowBytes - 1] &= keep;
        }
    }
    if (format == SkMask::kARGB32_Format) {
        // Premultiplied pixels: each color channel at most the alpha (the high byte).
        uint32_t* colors = reinterpret_cast<uint32_t*>(mask.fImage.data());
        for (size_t i = 0; i < mask.fImage.size() / 4; ++i) {
            const uint32_t a = colors[i] >> 24;
            const uint32_t r = std::min((colors[i] >> 16) & 0xFF, a);
            const uint32_t g = std::min((colors[i] >> 8) & 0xFF, a);
            const uint32_t b = std::min(colors[i] & 0xFF, a);
            colors[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    return mask;
}

SkIRect random_subrect(Sequence& sequence, int width, int height) {
    const int left = pick_int(sequence, 0, width);
    const int top = pick_int(sequence, 0, height);
    const int right = left + pick_int(sequence, 1, width - left);
    const int bottom = top + pick_int(sequence, 1, height - top);
    const SkIRect rect = SkIRect::MakeLTRB(left, top, right, bottom);
    return rect;
}

// Writable runs and coverage covering width pixels in runs of up to maxRun.
struct Runs {
    std::vector<int16_t> fRuns{};
    std::vector<SkAlpha> fAlpha{};
};

Runs make_runs(Sequence& sequence, int width, int maxRun) {
    Runs runs;
    runs.fRuns.assign(static_cast<size_t>(width) + 1, 0);
    runs.fAlpha.assign(static_cast<size_t>(width) + 1, 0);
    int filled = 0;
    while (filled < width) {
        const int run = std::min(width - filled, pick_int(sequence, 1, maxRun));
        const std::array<uint8_t, 4> special{0x00, 0xFF, 0x80, 0x01};
        const uint8_t alpha = sequence.next() % 3 == 0 ? special[sequence.pick(special.size())]
                                                       : pick_byte(sequence);
        runs.fRuns[static_cast<size_t>(filled)] = static_cast<int16_t>(run);
        runs.fAlpha[static_cast<size_t>(filled)] = alpha;
        filled += run;
    }
    return runs;
}

// Issues a varied sequence of calls inside a width x height destination. maskFormats lists the
// mask formats the blitter accepts.
void drive_blitter(Sequence& sequence, SkBlitter* blitter, int width, int height,
                   const std::vector<SkMask::Format>& maskFormats) {
    for (int i = 0; i < 4; ++i) {
        const SkIRect r = random_subrect(sequence, width, height);
        (*blitter).blitH(r.fLeft, r.fTop, r.width());

        Runs runs = make_runs(sequence, r.width(), 6);
        (*blitter).blitAntiH(r.fLeft, r.fTop, runs.fAlpha.data(), runs.fRuns.data());

        (*blitter).blitV(r.fLeft, r.fTop, r.height(), pick_byte(sequence));
        (*blitter).blitV(r.fRight - 1, r.fTop, r.height(), 0xFF);
        (*blitter).blitRect(r.fLeft, r.fTop, r.width(), r.height());
        if (r.width() >= 2) {
            (*blitter).blitAntiRect(r.fLeft, r.fTop, r.width() - 2, r.height(),
                                    pick_byte(sequence), pick_byte(sequence));
            (*blitter).blitAntiH2(r.fLeft, r.fTop, pick_byte(sequence), pick_byte(sequence));
        }
        if (r.height() >= 2) {
            (*blitter).blitAntiV2(r.fLeft, r.fTop, pick_byte(sequence), pick_byte(sequence));
        }
        if (r.width() >= 4) {
            const SkRect fat = SkRect::MakeLTRB(r.fLeft + 0.25f, r.fTop + 0.5f, r.fRight - 0.75f,
                                                r.fBottom - 0.125f);
            if (fat.roundOut().width() >= 3) {
                (*blitter).blitFatAntiRect(fat);
            }
        }
    }
    for (const SkMask::Format format : maskFormats) {
        const SkIRect bounds = random_subrect(sequence, width, height);
        const TestMask mask = make_mask(sequence, bounds, format);
        // Whole mask, then a part clipped from inside on every side.
        (*blitter).blitMask(mask.view(), bounds);
        if (bounds.width() > 2 && bounds.height() > 2) {
            SkIRect inner = bounds;
            inner.inset(1, 1);
            if (format == SkMask::kARGB32_Format) {
                // Left-clipped 32-bit masks are an intentional correction; keep the left edge.
                inner.fLeft = bounds.fLeft;
            }
            (*blitter).blitMask(mask.view(), inner);
        }
    }
}

sk_sp<SkShader> make_gradient(Sequence& sequence, int width) {
    const SkColor4f stops[2] = {
        {pick_byte(sequence) / 255.0f, 0.25f, 0.75f, 1.0f},
        {0.0f, pick_byte(sequence) / 255.0f, 0.5f, sequence.next() % 2 ? 1.0f : 0.5f},
    };
    const SkGradient::Colors colors(SkSpan<const SkColor4f>(stops, 2), SkTileMode::kClamp);
    const SkGradient gradient(colors, {});
    const SkPoint points[2] = {{0, 0}, {static_cast<float>(width), 7}};
    sk_sp<SkShader> shader = shader_of(SkShaders::LinearGradient(points, gradient, nullptr));
    return shader;
}

void chosen_blitter_corpus(Output& output, Sequence& sequence) {
    const std::array<SkColorType, 6> colorTypes{
        kN32_SkColorType, kRGBA_8888_SkColorType, kRGB_565_SkColorType, kRGBA_F16_SkColorType,
        kARGB_4444_SkColorType, kBGRA_8888_SkColorType,
    };
    const std::array<SkBlendMode, 6> modes{
        SkBlendMode::kSrcOver, SkBlendMode::kSrcOver, SkBlendMode::kSrc, SkBlendMode::kClear,
        SkBlendMode::kMultiply, SkBlendMode::kPlus,
    };
    const std::array<SkColor, 6> colors{
        SK_ColorBLACK, SK_ColorWHITE, 0xFF336699, 0x80336699, 0x00FFFFFF, 0x01020304,
    };
    const float matrix[20] = {0.5f, 0, 0, 0, 0.1f,  0, 1, 0, 0, 0,  0, 0, 0.25f, 0, 0.2f,
                              0, 0, 0, 1, 0};
    const SkSurfaceProps props{};
    for (int i = 0; i < 2400; ++i) {
        const SkColorType colorType = colorTypes[sequence.pick(colorTypes.size())];
        const SkAlphaType alphaType = colorType == kRGB_565_SkColorType ? kOpaque_SkAlphaType
                                                                        : kPremul_SkAlphaType;
        const int width = pick_int(sequence, 9, 32);
        const int height = pick_int(sequence, 5, 24);
        Pixels pixels = make_pixels(sequence, colorType, alphaType, width, height);

        SkPaint paint;
        paint.setColor(colors[sequence.pick(colors.size())]);
        paint.setBlendMode(modes[sequence.pick(modes.size())]);
        const uint32_t extras = sequence.next() % 8;
        if (extras == 1) {
            paint.setDither(true);
        } else if (extras == 2) {
            paint.setShader(make_gradient(sequence, width));
        } else if (extras == 3) {
            paint.setColorFilter(SkColorFilters::Matrix(matrix));
        } else if (extras == 4) {
            paint.setShader(make_gradient(sequence, width));
            paint.setDither(true);
        }
        sk_sp<SkShader> clipShader{};
        if (sequence.next() % 10 == 0) {
            clipShader = make_gradient(sequence, width);
        }
        const bool legacy = colorType == kN32_SkColorType && extras == 0 && !clipShader &&
                            paint.getBlendMode_or(SkBlendMode::kClear) == SkBlendMode::kSrcOver;

        SkSTArenaAlloc<4096> alloc;
        const SkPixmap destination = pixels.pixmap();
        SkBlitter* blitter = SkBlitter::Choose(destination, SkMatrix::I(), paint, &alloc,
                                               SkDrawCoverage::kNo, clipShader, props,
                                               SkRect::MakeWH(width, height));
        std::vector<SkMask::Format> formats{SkMask::kA8_Format, SkMask::kBW_Format,
                                            SkMask::kLCD16_Format};
        if (legacy) {
            formats.push_back(SkMask::kARGB32_Format);
        }
        if (colorType != kN32_SkColorType) {
            formats.push_back(SkMask::k3D_Format);
        }
        drive_blitter(sequence, blitter, width, height, formats);

        const std::optional<SkBlitter::DirectBlit> direct = (*blitter).canDirectBlit();
        append_bool(output, direct.has_value());
        if (direct.has_value()) {
            append(output, (*direct).value);
        }
        append(output, static_cast<int32_t>((*blitter).requestRowsPreserved()));
        record_pixels(output, pixels);
        ++output.cases;
    }
}

// Records every forwarded call. It overrides only the required operations, so the base class
// supplies blitV, blitRect, blitAntiRect, blitAntiH2, blitAntiV2 and the A8 and BW mask paths.
class MinimalRecorder : public SkBlitter {
public:
    explicit MinimalRecorder(Output* output) : fOutput(output) {}

    void blitH(int x, int y, int width) override {
        append(*fOutput, static_cast<uint8_t>(1));
        append(*fOutput, x);
        append(*fOutput, y);
        append(*fOutput, width);
    }
    void blitAntiH(int x, int y, const SkAlpha antialias[], const int16_t runs[]) override {
        append(*fOutput, static_cast<uint8_t>(2));
        append(*fOutput, x);
        append(*fOutput, y);
        int index = 0;
        while (runs[index] > 0) {
            append(*fOutput, antialias[index]);
            append(*fOutput, runs[index]);
            index += runs[index];
        }
        append(*fOutput, static_cast<int16_t>(0));
    }

private:
    Output* fOutput;
};

// Records every call, including the operations the base class would otherwise decompose.
class FullRecorder final : public MinimalRecorder {
public:
    explicit FullRecorder(Output* output) : MinimalRecorder(output), fOutput(output) {}

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
        (*this).record(6, clip.fLeft, clip.fTop, clip.fRight, clip.fBottom);
        append(*fOutput, static_cast<uint8_t>(mask.fFormat));
    }

private:
    void record(uint8_t kind, int a, int b, int c, int d) {
        append(*fOutput, kind);
        append(*fOutput, a);
        append(*fOutput, b);
        append(*fOutput, c);
        append(*fOutput, d);
    }

    Output* fOutput;
};

SkRegion random_region(Sequence& sequence, int width, int height) {
    std::vector<SkIRect> rects(2 + sequence.pick(5));
    for (SkIRect& rect : rects) {
        rect = random_subrect(sequence, width, height);
    }
    SkRegion region;
    region.setRects(SkSpan<const SkIRect>(rects.data(), rects.size()));
    return region;
}

// Calls that may extend past the clip on every side.
void drive_clipped(Sequence& sequence, SkBlitter* blitter, int width, int height) {
    for (int i = 0; i < 6; ++i) {
        const int x = pick_int(sequence, -8, width + 8);
        const int y = pick_int(sequence, -8, height + 8);
        const int w = pick_int(sequence, 1, width + 8);
        const int h = pick_int(sequence, 1, height + 8);
        (*blitter).blitH(x, y, w);
        Runs runs = make_runs(sequence, w, 5);
        (*blitter).blitAntiH(x, y, runs.fAlpha.data(), runs.fRuns.data());
        (*blitter).blitV(x, y, h, pick_byte(sequence));
        (*blitter).blitRect(x, y, w, h);
        (*blitter).blitAntiRect(x, y, w, h, pick_byte(sequence), pick_byte(sequence));
        const SkIRect bounds = SkIRect::MakeXYWH(x, y, w, h);
        const TestMask mask = make_mask(sequence, bounds, SkMask::kA8_Format);
        (*blitter).blitMask(mask.view(), bounds);
    }
}

void clip_blitter_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 1500; ++i) {
        const int width = pick_int(sequence, 8, 40);
        const int height = pick_int(sequence, 8, 30);
        FullRecorder recorder(&output);

        SkRectClipBlitter rectClip;
        const SkIRect clipRect = random_subrect(sequence, width, height);
        rectClip.init(&recorder, clipRect);
        drive_clipped(sequence, &rectClip, width, height);

        const SkRegion region = random_region(sequence, width, height);
        if (!region.isEmpty()) {
            SkRgnClipBlitter rgnClip;
            rgnClip.init(&recorder, &region);
            drive_clipped(sequence, &rgnClip, width, height);
        }

        SkBlitterClipper clipper;
        const SkIRect bounds = random_subrect(sequence, width, height);
        const bool withBounds = sequence.next() % 2 == 0;
        SkBlitter* applied = clipper.apply(&recorder, &region, withBounds ? &bounds : nullptr);
        append_bool(output, applied == &recorder);
        drive_clipped(sequence, applied, width, height);

        recorder.blitRegion(region);
        recorder.blitRectRegion(random_subrect(sequence, width, height), region);
        const TestMask mask = make_mask(sequence, random_subrect(sequence, width, height),
                                        SkMask::kA8_Format);
        recorder.blitMaskRegion(mask.view(), region);

        // The base class's default operations, including BW masks clipped at every bit offset.
        MinimalRecorder minimal(&output);
        const std::vector<SkMask::Format> formats{SkMask::kA8_Format, SkMask::kBW_Format,
                                                  SkMask::kBW_Format};
        drive_blitter(sequence, &minimal, width, height, formats);
        ++output.cases;
    }
}

void a8_blitter_corpus(Output& output, Sequence& sequence) {
    const std::array<SkBlendMode, 3> modes{SkBlendMode::kSrcOver, SkBlendMode::kSrc,
                                           SkBlendMode::kMultiply};
    for (int i = 0; i < 800; ++i) {
        const int width = pick_int(sequence, 9, 32);
        const int height = pick_int(sequence, 5, 24);
        Pixels pixels = make_pixels(sequence, kAlpha_8_SkColorType, kPremul_SkAlphaType, width,
                                    height);
        SkPaint paint;
        paint.setAlpha(pick_byte(sequence));
        paint.setBlendMode(modes[sequence.pick(modes.size())]);
        const SkDrawCoverage coverage =
                sequence.next() % 3 == 0 ? SkDrawCoverage::kYes : SkDrawCoverage::kNo;
        if (coverage == SkDrawCoverage::kYes) {
            paint.setBlendMode(SkBlendMode::kSrcOver);
        }
        SkSTArenaAlloc<1024> alloc;
        SkBlitter* blitter = SkChooseA8Blitter(pixels.pixmap(), SkMatrix::I(), paint, &alloc,
                                               coverage, nullptr);
        append_bool(output, blitter != nullptr);
        if (blitter) {
            const std::vector<SkMask::Format> formats{SkMask::kA8_Format, SkMask::kBW_Format};
            drive_blitter(sequence, blitter, width, height, formats);
        }
        record_pixels(output, pixels);
        ++output.cases;
    }
}

void sprite_blitter_corpus(Output& output, Sequence& sequence) {
    const std::array<SkColorType, 4> dstTypes{kN32_SkColorType, kRGB_565_SkColorType,
                                              kRGBA_F16_SkColorType, kRGBA_8888_SkColorType};
    const std::array<SkColorType, 4> srcTypes{kN32_SkColorType, kAlpha_8_SkColorType,
                                              kRGB_565_SkColorType, kRGBA_8888_SkColorType};
    for (int i = 0; i < 800; ++i) {
        const int width = pick_int(sequence, 9, 32);
        const int height = pick_int(sequence, 5, 24);
        const SkColorType dstType = dstTypes[sequence.pick(dstTypes.size())];
        const SkColorType srcType = srcTypes[sequence.pick(srcTypes.size())];
        Pixels dst = make_pixels(sequence, dstType,
                                 dstType == kRGB_565_SkColorType ? kOpaque_SkAlphaType
                                                                 : kPremul_SkAlphaType,
                                 width, height);
        const SkAlphaType srcAlpha = srcType == kRGB_565_SkColorType || sequence.next() % 3 == 0
                                             ? kOpaque_SkAlphaType
                                             : kPremul_SkAlphaType;
        // The source fits inside the destination.
        const int srcWidth = pick_int(sequence, 4, width - 3);
        const int srcHeight = pick_int(sequence, 3, height - 2);
        Pixels src = make_pixels(sequence, srcType, srcAlpha, srcWidth, srcHeight);
        if (srcAlpha == kOpaque_SkAlphaType && srcType == kN32_SkColorType) {
            uint32_t* colors = reinterpret_cast<uint32_t*>(src.fStorage.data());
            for (int p = 0; p < srcWidth * srcHeight; ++p) {
                colors[p] |= 0xFF000000u;
            }
        } else if (srcType == kN32_SkColorType || srcType == kRGBA_8888_SkColorType) {
            uint8_t* bytes = reinterpret_cast<uint8_t*>(src.fStorage.data());
            for (int p = 0; p < srcWidth * srcHeight; ++p) {
                uint8_t* pixel = bytes + 4 * p;
                const uint8_t a = pixel[3];
                pixel[0] = std::min(pixel[0], a);
                pixel[1] = std::min(pixel[1], a);
                pixel[2] = std::min(pixel[2], a);
            }
        }
        const int left = pick_int(sequence, 0, width - srcWidth + 1);
        const int top = pick_int(sequence, 0, height - srcHeight + 1);

        SkPaint paint;
        const std::array<SkBlendMode, 3> modes{SkBlendMode::kSrc, SkBlendMode::kSrcOver,
                                               SkBlendMode::kSrcOver};
        paint.setBlendMode(modes[sequence.pick(modes.size())]);
        paint.setAlpha(sequence.next() % 2 ? 0xFF : pick_byte(sequence));
        paint.setColor4f({0.25f, 0.5f, 0.75f, paint.getAlphaf()});

        SkSTArenaAlloc<4096> alloc;
        SkBlitter* blitter = SkBlitter::ChooseSprite(dst.pixmap(), paint, src.pixmap(), left, top,
                                                     &alloc, nullptr);
        append_bool(output, blitter != nullptr);
        if (blitter) {
            for (int r = 0; r < 3; ++r) {
                const SkIRect rect = random_subrect(sequence, srcWidth, srcHeight)
                                             .makeOffset(left, top);
                (*blitter).blitRect(rect.fLeft, rect.fTop, rect.width(), rect.height());
            }
        }
        record_pixels(output, dst);
        ++output.cases;
    }
}

// A shader context that produces a deterministic premultiplied pattern.
class PatternContext final : public SkShaderBase::Context {
public:
    PatternContext(const SkShaderBase& shader, const SkShaderBase::ContextRec& rec, bool opaque)
            : Context(shader, rec), fOpaque(opaque) {}

    uint32_t getFlags() const override {
        return fOpaque ? SkShaderBase::kOpaqueAlpha_Flag : 0;
    }

    void shadeSpan(int x, int y, SkPMColor colors[], int count) override {
        for (int i = 0; i < count; ++i) {
            const uint32_t a = fOpaque ? 0xFF : static_cast<uint32_t>((x + i) * 37 + y * 11) & 0xFF;
            const uint32_t c = static_cast<uint32_t>((x + i) * 13 + y * 7) & 0xFF;
            const uint32_t channel = std::min(c, a);
            colors[i] = SkPackARGB32(a, channel, std::min(channel / 2 + 3, a), a / 3);
        }
    }

private:
    bool fOpaque;
};

void shader_blitter_corpus(Output& output, Sequence& sequence) {
    const sk_sp<SkShader> shader = SkShaders::Color(SK_ColorRED);
    const SkSurfaceProps props{};
    for (int i = 0; i < 600; ++i) {
        const int width = pick_int(sequence, 9, 32);
        const int height = pick_int(sequence, 5, 24);
        Pixels pixels = make_pixels(sequence, kN32_SkColorType, kPremul_SkAlphaType, width,
                                    height);
        SkPaint paint;
        paint.setShader(shader);
        const SkShaderBase::ContextRec rec(0xFF, SkShaders::MatrixRec(SkMatrix::I()),
                                           kN32_SkColorType, nullptr, props);
        PatternContext context(*as_SB(shader), rec, sequence.next() % 2 == 0);
        SkARGB32_Shader_Blitter blitter(pixels.pixmap(), paint, &context);
        const std::vector<SkMask::Format> formats{SkMask::kA8_Format, SkMask::kLCD16_Format,
                                                  SkMask::kBW_Format};
        drive_blitter(sequence, &blitter, width, height, formats);
        record_pixels(output, pixels);
        ++output.cases;
    }
}

void blitter_corpus(Output& output) {
    Sequence sequence{};
    chosen_blitter_corpus(output, sequence);
    clip_blitter_corpus(output, sequence);
    a8_blitter_corpus(output, sequence);
    sprite_blitter_corpus(output, sequence);
    shader_blitter_corpus(output, sequence);
}

#ifndef STX_SKIA_REFERENCE
// A 32-bit mask clipped from its left edge is read at the clipped column; it used to be read
// at a byte offset, shifting the pixels.
bool left_clipped_argb32_mask_reads_its_pixels() {
    Sequence sequence{};
    Pixels pixels = make_pixels(sequence, kN32_SkColorType, kPremul_SkAlphaType, 16, 4);
    SkPaint paint;
    paint.setColor(SK_ColorWHITE);
    SkSTArenaAlloc<1024> alloc;
    const SkSurfaceProps props{};
    SkBlitter* blitter = SkBlitter::Choose(pixels.pixmap(), SkMatrix::I(), paint, &alloc,
                                           SkDrawCoverage::kNo, nullptr, props,
                                           SkRect::MakeWH(16, 4));
    TestMask mask = make_mask(sequence, SkIRect::MakeLTRB(2, 0, 14, 4), SkMask::kARGB32_Format);
    uint32_t* colors = reinterpret_cast<uint32_t*>(mask.fImage.data());
    for (size_t p = 0; p < mask.fImage.size() / 4; ++p) {
        colors[p] |= 0xFF000000u;  // opaque, so source-over copies the mask pixel
    }
    const SkIRect clip = SkIRect::MakeLTRB(5, 1, 12, 3);
    (*blitter).blitMask(mask.view(), clip);
    const SkPixmap result = pixels.pixmap();
    for (int y = clip.fTop; y < clip.fBottom; ++y) {
        for (int x = clip.fLeft; x < clip.fRight; ++x) {
            const uint32_t expected = colors[(y - 0) * 12 + (x - 2)];
            if (*result.addr32(x, y) != expected) return false;
        }
    }
    return true;
}

// Bits beyond a BW mask's width are not drawn, even when the clip spans the whole mask.
bool bw_mask_padding_is_not_drawn() {
    Sequence sequence{};
    Pixels pixels = make_pixels(sequence, kN32_SkColorType, kPremul_SkAlphaType, 16, 2);
    const std::vector<uint64_t> before = pixels.fStorage;
    SkPaint paint;
    paint.setColor(SK_ColorBLUE);
    SkSTArenaAlloc<1024> alloc;
    const SkSurfaceProps props{};
    SkBlitter* blitter = SkBlitter::Choose(pixels.pixmap(), SkMatrix::I(), paint, &alloc,
                                           SkDrawCoverage::kNo, nullptr, props,
                                           SkRect::MakeWH(16, 2));
    const SkIRect bounds = SkIRect::MakeLTRB(3, 0, 8, 2);  // five pixels in one byte
    const uint8_t rows[2] = {0xFF, 0xFF};
    const SkMask mask(rows, bounds, 1, SkMask::kBW_Format);
    (*blitter).blitMask(mask, bounds);
    const SkPixmap result = pixels.pixmap();
    const SkPixmap original(pixels.fInfo, before.data(), pixels.fInfo.minRowBytes());
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool inside = x >= bounds.fLeft && x < bounds.fRight;
            const bool changed = *result.addr32(x, y) != *original.addr32(x, y);
            if (!inside && changed) return false;
            if (inside && *result.addr32(x, y) != SkPreMultiplyColor(SK_ColorBLUE)) return false;
        }
    }
    return true;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
#ifndef STX_SKIA_REFERENCE
    if (!left_clipped_argb32_mask_reads_its_pixels()) {
        std::fprintf(stderr, "left-clipped 32-bit mask was read at the wrong column\n");
        return 1;
    }
    if (!bw_mask_padding_is_not_drawn()) {
        std::fprintf(stderr, "BW mask padding bits were drawn\n");
        return 1;
    }
#endif
    Output output{};
    blitter_corpus(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("blitters: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
