// Differential corpus for the canvas, the bitmap device and skcpu::Draw: primitives with every
// paint style, cap and join, hairlines, point modes (including the direct-blit and dashed-line
// fast paths), regions, arcs and nested rounded rectangles, under integer, fractional, rotated,
// skewed and perspective transforms and rectangle, rounded-rectangle, path and region clips;
// layers; images drawn as sprites, through subsets, nine-patches and lattices; vertices, patches
// and atlases; blurred shapes (mask and nine-patch paths); a device wide enough to be drawn in
// tiles; and canvas queries and pixel transfer. Pixels are hashed into the corpus.

#include "differential_corpus.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkClipOp.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkM44.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRSXform.h"
#include "include/core/SkRect.h"
#include "include/core/SkRegion.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkSpan.h"
#include "include/core/SkTileMode.h"
#include "include/core/SkVertices.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"
#include "gradient_result.hpp"
#include "dash_result.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

// The clip stack and its device served only the PDF, SVG and XPS backends; the CPU canvas clips
// through SkRasterClipStack. Their originals remain in baseline/skia.
#ifndef STX_SKIA_REFERENCE
#if __has_include("src/core/SkClipStack.h") || __has_include("src/core/SkClipStackDevice.h")
#error The clip stack and its device must not be part of the candidate.
#endif
// Drawables and annotations are withdrawn: the CPU device ignored annotations, and no consumer
// draws drawables. Expressions, not member pointers, so that overloads count.
template <typename T>
concept HasDrawableDrawing = requires(T& canvas) { canvas.drawDrawable(nullptr, 0.0f, 0.0f); };
static_assert(!HasDrawableDrawing<SkCanvas>);
template <typename T>
concept HasAnnotations = requires(T& canvas, const SkRect& rect) {
    canvas.drawAnnotation(rect, "key", nullptr);
};
static_assert(!HasAnnotations<SkCanvas>);
#if __has_include("include/core/SkDrawable.h") || __has_include("include/core/SkAnnotation.h") || \
    __has_include("src/core/SkAnnotationKeys.h")
#error Drawable and annotation headers must not be part of the candidate.
#endif
// The forwarding, paint-filtering, null and stacked canvases, canvas-state transfer and the
// Android framework helpers had no user in the library or GUI.Forms.
#if __has_include("include/utils/SkNWayCanvas.h") || \
    __has_include("include/utils/SkPaintFilterCanvas.h") || \
    __has_include("include/utils/SkNullCanvas.h") || \
    __has_include("include/utils/SkCanvasStateUtils.h") || \
    __has_include("src/utils/SkCanvasStack.h") || \
    __has_include("include/android/SkAndroidFrameworkUtils.h")
#error Wrapper canvas and canvas-state headers must not be part of the candidate.
#endif
// Shadow drawing is withdrawn with its tessellator, polygon utilities and priority queue: no
// consumer drew shadows through the canvas.
#if __has_include("include/utils/SkShadowUtils.h") || \
    __has_include("src/utils/SkShadowTessellator.h") || \
    __has_include("src/utils/SkPolyUtils.h") || \
    __has_include("src/core/SkDrawShadowInfo.h") || \
    __has_include("src/core/SkTDPQueue.h")
#error Shadow, polygon and priority-queue headers must not be part of the candidate.
#endif
// Documents, the camera, JSON writing, distance fields, parsing, path and float formatting
// helpers and the CoreGraphics image bridge had no user in the library or GUI.Forms.
#if __has_include("include/core/SkDocument.h") || __has_include("include/docs/SkPDFDocument.h") || \
    __has_include("include/utils/SkCamera.h") || __has_include("include/utils/SkParse.h") || \
    __has_include("include/utils/SkParsePath.h") || __has_include("include/utils/mac/SkCGUtils.h") || \
    __has_include("src/core/SkDistanceFieldGen.h") || __has_include("src/utils/SkJSONWriter.h") || \
    __has_include("src/utils/SkFloatToDecimal.h") || __has_include("src/utils/SkOSPath.h")
#error Document, camera, parsing and other unused utility headers must not be part of the candidate.
#endif
// The 2x2 matrix helper served only the font scaler, and the Windows COM, stream and
// DirectWrite helpers only the removed codecs and font backends.
#if __has_include("src/utils/SkMatrix22.h") || __has_include("src/utils/win/SkHRESULT.h") || \
    __has_include("src/utils/win/SkIStream.h") || __has_include("src/utils/win/SkTScopedComPtr.h") || \
    __has_include("src/utils/win/SkAutoCoInitialize.h") || \
    __has_include("src/utils/win/SkDWriteGeometrySink.h")
#error Font-scaler and Windows helper headers must not be part of the candidate.
#endif
// Upstream directories the candidate never compiled, and the headers only they used, live in
// baseline/skia: GPU, codecs, path ops, PDF, encoders, PartitionAlloc, XML, XPS, SVG, the
// Android utilities and capture. One header from each is probed.
#if __has_include("src/gpu/ganesh/GrCaps.h") || __has_include("src/codec/SkCodecPriv.h") || \
    __has_include("src/pathops/SkPathOpsTypes.h") || __has_include("src/pdf/SkPDFDevice.h") || \
    __has_include("src/encode/SkImageEncoderPriv.h") || __has_include("src/xml/SkXMLWriter.h") || \
    __has_include("include/codec/SkCodec.h") || __has_include("include/gpu/ganesh/GrDirectContext.h") || \
    __has_include("src/core/SkRandom.h")
#error Uncompiled upstream directories must not be part of the candidate.
#endif
#endif
// The canvas's shadow entry point, detected by expression so that overloads count.
struct SkDrawShadowRec;
template <typename T>
concept HasShadowDrawing = requires(T& canvas, const SkPath& path, const SkDrawShadowRec& rec) {
    canvas.private_draw_shadow_rec(path, rec);
};
#ifndef STX_SKIA_REFERENCE
static_assert(!HasShadowDrawing<SkCanvas>);
#else
static_assert(HasShadowDrawing<SkCanvas>);
#endif
#ifdef STX_SKIA_REFERENCE
// The reference canvas still has both, so the expressions above do detect them.
template <typename T>
concept HasDrawableDrawing = requires(T& canvas) { canvas.drawDrawable(nullptr, 0.0f, 0.0f); };
static_assert(HasDrawableDrawing<SkCanvas>);
template <typename T>
concept HasAnnotations = requires(T& canvas, const SkRect& rect) {
    canvas.drawAnnotation(rect, "key", nullptr);
};
static_assert(HasAnnotations<SkCanvas>);
#endif

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

// Coordinates in [origin, origin + span) at eighth steps.
float pick_coordinate(Sequence& sequence, int origin, int span) {
    const int whole = pick_int(sequence, origin, span);
    const float fraction = static_cast<float>(sequence.next() % 8) * 0.125f;
    const float value = static_cast<float>(whole) + fraction;
    return value;
}

SkRect random_rect(Sequence& sequence, int origin, int span) {
    const float l = pick_coordinate(sequence, origin, span);
    const float t = pick_coordinate(sequence, origin, span);
    const SkRect rect = SkRect::MakeXYWH(l, t, pick_coordinate(sequence, 1, span / 2 + 1),
                                         pick_coordinate(sequence, 1, span / 2 + 1));
    return rect;
}

SkColor random_color(Sequence& sequence) {
    const std::array<SkColor, 6> colors{
        SK_ColorBLACK, 0xFF336699, 0x80E04020, 0x40FFFFFF, 0xFFFFFFFF, 0x10203040,
    };
    const SkColor color = colors[sequence.pick(colors.size())];
    return color;
}

SkPath random_path(Sequence& sequence, int origin, int span) {
    SkPathBuilder builder{};
    const int contours = 1 + static_cast<int>(sequence.next() % 2);
    for (int contour = 0; contour < contours; ++contour) {
        builder.moveTo(pick_coordinate(sequence, origin, span),
                       pick_coordinate(sequence, origin, span));
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
                builder.conicTo(p1, p2, 0.5f);
            } else {
                builder.cubicTo(p1, p2, p3);
            }
        }
        if (sequence.next() % 3 != 0) {
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

sk_sp<SkShader> make_gradient(Sequence& sequence) {
    const SkColor4f stops[2] = {
        {1.0f, 0.25f, 0.0f, 1.0f},
        {0.0f, 0.5f, 1.0f, sequence.next() % 2 ? 1.0f : 0.25f},
    };
    const SkGradient::Colors colors(SkSpan<const SkColor4f>(stops, 2), SkTileMode::kMirror);
    const SkGradient gradient(colors, {});
    const SkPoint points[2] = {{0, 0}, {pick_coordinate(sequence, 10, 60), 17}};
    sk_sp<SkShader> shader = shader_of(SkShaders::LinearGradient(points, gradient, nullptr));
    return shader;
}

SkPaint random_paint(Sequence& sequence) {
    SkPaint paint;
    paint.setColor(random_color(sequence));
    paint.setAntiAlias(sequence.next() % 2 == 0);
    const std::array<SkPaint::Style, 4> styles{SkPaint::kFill_Style, SkPaint::kFill_Style,
                                               SkPaint::kStroke_Style,
                                               SkPaint::kStrokeAndFill_Style};
    paint.setStyle(styles[sequence.pick(styles.size())]);
    const std::array<float, 5> widths{0.0f, 0.5f, 1.0f, 2.5f, 6.0f};
    paint.setStrokeWidth(widths[sequence.pick(widths.size())]);
    const std::array<SkPaint::Cap, 3> caps{SkPaint::kButt_Cap, SkPaint::kRound_Cap,
                                           SkPaint::kSquare_Cap};
    paint.setStrokeCap(caps[sequence.pick(caps.size())]);
    const std::array<SkPaint::Join, 3> joins{SkPaint::kMiter_Join, SkPaint::kRound_Join,
                                             SkPaint::kBevel_Join};
    paint.setStrokeJoin(joins[sequence.pick(joins.size())]);
    paint.setStrokeMiter(sequence.next() % 2 ? 4.0f : 1.0f);
    const std::array<SkBlendMode, 5> modes{SkBlendMode::kSrcOver, SkBlendMode::kSrcOver,
                                           SkBlendMode::kSrc, SkBlendMode::kMultiply,
                                           SkBlendMode::kPlus};
    paint.setBlendMode(modes[sequence.pick(modes.size())]);
    const uint32_t extra = sequence.next() % 10;
    if (extra == 0) {
        paint.setShader(make_gradient(sequence));
    } else if (extra == 1) {
        paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, 1.5f, false));
    } else if (extra == 2) {
        const float dash[4] = {4, 2, 1, 3};
        paint.setPathEffect(effect_of(SkDashPathEffect::Make(SkSpan<const float>(dash, 4), 0.5f)));
    } else if (extra == 3) {
        const float matrix[20] = {0.5f, 0, 0, 0, 0.1f,  0, 1, 0, 0, 0,  0, 0, 0.25f, 0, 0.2f,
                                  0, 0, 0, 1, 0};
        paint.setColorFilter(SkColorFilters::Matrix(matrix));
    } else if (extra == 4) {
        paint.setDither(true);
        paint.setShader(make_gradient(sequence));
    }
    return paint;
}

SkM44 random_transform(Sequence& sequence) {
    const uint32_t kind = sequence.next() % 7;
    if (kind == 0) {
        return SkM44();
    }
    if (kind == 1) {
        return SkM44::Translate(static_cast<float>(pick_int(sequence, -8, 17)),
                                static_cast<float>(pick_int(sequence, -8, 17)));
    }
    if (kind == 2) {
        return SkM44::Translate(pick_coordinate(sequence, -8, 17), pick_coordinate(sequence, -8, 17));
    }
    if (kind == 3) {
        return SkM44::Scale(1.75f, 0.625f);
    }
    if (kind == 4) {
        return SkM44(SkMatrix::RotateDeg(static_cast<float>(pick_int(sequence, -60, 121)),
                                         SkPoint{48, 40}));
    }
    if (kind == 5) {
        SkMatrix skew;
        skew.setSkew(0.25f, -0.125f);
        return SkM44(skew);
    }
    SkMatrix perspective = SkMatrix::I();
    perspective.setPerspX(0.002f);
    perspective.setPerspY(-0.001f);
    return SkM44(perspective);
}

void random_clip(SkCanvas& canvas, Sequence& sequence) {
    const SkClipOp op = sequence.next() % 3 == 0 ? SkClipOp::kDifference : SkClipOp::kIntersect;
    const bool aa = sequence.next() % 2 == 0;
    const uint32_t kind = sequence.next() % 5;
    if (kind == 0) {
        canvas.clipRect(random_rect(sequence, -10, 100), op, aa);
    } else if (kind == 1) {
        canvas.clipRRect(SkRRect::MakeRectXY(random_rect(sequence, -10, 100), 6, 9), op, aa);
    } else if (kind == 2) {
        canvas.clipPath(random_path(sequence, -10, 110), op, aa);
    } else if (kind == 3) {
        SkRegion region;
        region.setRect(SkIRect::MakeXYWH(pick_int(sequence, 0, 40), pick_int(sequence, 0, 40),
                                         50, 45));
        region.op(SkIRect::MakeXYWH(pick_int(sequence, 20, 50), pick_int(sequence, 10, 50), 30,
                                    40),
                  SkRegion::kUnion_Op);
        canvas.clipRegion(region, op);
    }
}

void draw_primitive(SkCanvas& canvas, Sequence& sequence) {
    const SkPaint paint = random_paint(sequence);
    const uint32_t kind = sequence.next() % 15;
    if (kind == 0) {
        canvas.drawRect(random_rect(sequence, -10, 100), paint);
    } else if (kind == 1) {
        canvas.drawIRect(SkIRect::MakeXYWH(pick_int(sequence, -5, 80), pick_int(sequence, -5, 80),
                                           pick_int(sequence, 1, 40), pick_int(sequence, 1, 40)),
                         paint);
    } else if (kind == 2) {
        canvas.drawOval(random_rect(sequence, -10, 100), paint);
    } else if (kind == 3) {
        const SkVector radii[4] = {{2, 7}, {9, 5}, {6, 6}, {0, 0}};
        SkRRect rrect;
        rrect.setRectRadii(random_rect(sequence, -10, 100), radii);
        canvas.drawRRect(rrect, paint);
    } else if (kind == 4) {
        const SkRect outer = random_rect(sequence, -10, 100);
        SkRRect outerRRect = SkRRect::MakeRectXY(outer, 8, 5);
        SkRect inner = outer;
        inner.inset(outer.width() / 4, outer.height() / 4);
        canvas.drawDRRect(outerRRect, SkRRect::MakeOval(inner), paint);
    } else if (kind == 5) {
        canvas.drawCircle(pick_coordinate(sequence, 0, 100), pick_coordinate(sequence, 0, 80),
                          pick_coordinate(sequence, 0, 30), paint);
    } else if (kind == 6) {
        canvas.drawArc(random_rect(sequence, -10, 100), pick_coordinate(sequence, -180, 360),
                       pick_coordinate(sequence, -300, 600), sequence.next() % 2 == 0, paint);
    } else if (kind == 7) {
        canvas.drawRoundRect(random_rect(sequence, -10, 100), pick_coordinate(sequence, 0, 9),
                             pick_coordinate(sequence, 0, 9), paint);
    } else if (kind == 8 || kind == 9) {
        std::vector<SkPoint> points(1 + sequence.pick(9));
        for (SkPoint& point : points) {
            point = {pick_coordinate(sequence, -10, 110), pick_coordinate(sequence, -10, 90)};
        }
        const std::array<SkCanvas::PointMode, 3> modes{
            SkCanvas::kPoints_PointMode, SkCanvas::kLines_PointMode,
            SkCanvas::kPolygon_PointMode};
        canvas.drawPoints(modes[sequence.pick(modes.size())],
                          SkSpan<const SkPoint>(points.data(), points.size()), paint);
    } else if (kind == 10) {
        // Two-point lines with a dash take the dashed-line fast path.
        SkPaint dashed = paint;
        const float dash[2] = {3, 2};
        dashed.setPathEffect(effect_of(SkDashPathEffect::Make(SkSpan<const float>(dash, 2), 0)));
        dashed.setStyle(SkPaint::kStroke_Style);
        dashed.setStrokeWidth(sequence.next() % 2 ? 2.0f : 1.0f);
        const float y = pick_coordinate(sequence, 0, 90);
        canvas.drawLine(pick_coordinate(sequence, -5, 30), y, pick_coordinate(sequence, 40, 70),
                        sequence.next() % 2 ? y : y + 13, dashed);
    } else if (kind == 11) {
        canvas.drawPath(random_path(sequence, -10, 110), paint);
    } else if (kind == 12) {
        SkRegion region;
        region.setRect(SkIRect::MakeXYWH(pick_int(sequence, 0, 50), pick_int(sequence, 0, 50),
                                         30, 25));
        region.op(SkIRect::MakeXYWH(pick_int(sequence, 10, 50), pick_int(sequence, 10, 50), 25,
                                    30),
                  SkRegion::kXOR_Op);
        canvas.drawRegion(region, paint);
    } else if (kind == 13) {
        canvas.drawPaint(paint);
    } else {
        canvas.drawColor(SkColor4f::FromColor(random_color(sequence)),
                         sequence.next() % 2 ? SkBlendMode::kSrcOver : SkBlendMode::kMultiply);
    }
}

// A bitmap-backed canvas destination with deterministic initial pixels.
struct Target {
    SkBitmap fBitmap;

    void record(Output& output) const {
        const SkPixmap& pixmap = fBitmap.pixmap();
        std::vector<uint8_t> bytes{};
        for (int y = 0; y < pixmap.height(); ++y) {
            const uint8_t* row = static_cast<const uint8_t*>(pixmap.addr(0, y));
            bytes.insert(bytes.end(), row, row + pixmap.info().minRowBytes());
        }
        append(output, corpus::fnv1a(bytes));
    }
};

Target make_target(Sequence& sequence, SkColorType colorType, int width, int height) {
    Target target;
    const SkAlphaType alphaType = colorType == kRGB_565_SkColorType ? kOpaque_SkAlphaType
                                                                    : kPremul_SkAlphaType;
    target.fBitmap.allocPixels(SkImageInfo::Make(width, height, colorType, alphaType));
    target.fBitmap.eraseColor(random_color(sequence));
    return target;
}

void primitive_corpus(Output& output, Sequence& sequence) {
    const std::array<SkColorType, 4> colorTypes{kN32_SkColorType, kN32_SkColorType,
                                                kRGB_565_SkColorType, kRGBA_F16_SkColorType};
    for (int i = 0; i < 1200; ++i) {
        Target target = make_target(sequence, colorTypes[sequence.pick(colorTypes.size())], 96,
                                    80);
        SkCanvas canvas(target.fBitmap);
        for (int step = 0; step < 8; ++step) {
            const uint32_t action = sequence.next() % 10;
            if (action == 0) {
                canvas.save();
                canvas.concat(random_transform(sequence));
            } else if (action == 1) {
                canvas.save();
                random_clip(canvas, sequence);
            } else if (action == 2) {
                const SkRect bounds = random_rect(sequence, -10, 100);
                SkPaint layerPaint;
                layerPaint.setAlpha(static_cast<uint8_t>(64 + sequence.pick(192)));
                canvas.saveLayer(sequence.next() % 2 ? &bounds : nullptr, &layerPaint);
            } else if (action == 3 && canvas.getSaveCount() > 1) {
                canvas.restore();
            } else {
                draw_primitive(canvas, sequence);
            }
        }
        canvas.restoreToCount(1);
        target.record(output);
        ++output.cases;
    }
}

sk_sp<SkImage> make_image(Sequence& sequence, SkColorType colorType, int width, int height,
                          bool opaque) {
    const SkImageInfo info = SkImageInfo::Make(width, height, colorType,
                                               opaque ? kOpaque_SkAlphaType : kPremul_SkAlphaType);
    const size_t rowBytes = info.minRowBytes();
    std::vector<uint8_t> bytes(rowBytes * static_cast<size_t>(height));
    for (size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<uint8_t>(sequence.next() >> 24);
    }
    if (colorType == kN32_SkColorType) {
        for (size_t p = 0; p + 3 < bytes.size(); p += 4) {
            const uint8_t a = opaque ? 0xFF : bytes[p + 3];
            bytes[p + 3] = a;
            for (size_t c = 0; c < 3; ++c) {
                bytes[p + c] = std::min(bytes[p + c], a);
            }
        }
    }
    const sk_sp<SkData> data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    sk_sp<SkImage> image = SkImages::RasterFromData(info, data, rowBytes);
    return image;
}

void image_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 800; ++i) {
        Target target = make_target(sequence, kN32_SkColorType, 96, 80);
        SkCanvas canvas(target.fBitmap);
        const bool opaque = sequence.next() % 2 == 0;
        const sk_sp<SkImage> image = make_image(sequence, kN32_SkColorType, 17, 13, opaque);
        const sk_sp<SkImage> alpha = make_image(sequence, kAlpha_8_SkColorType, 9, 7, false);
        const std::array<SkSamplingOptions, 4> samplings{
            SkSamplingOptions(), SkSamplingOptions(SkFilterMode::kLinear),
            SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear),
            SkSamplingOptions(SkCubicResampler::Mitchell()),
        };
        for (int step = 0; step < 6; ++step) {
            SkPaint paint;
            paint.setAlpha(sequence.next() % 2 ? 0xFF : 0x90);
            paint.setColor4f({0.25f, 0.5f, 0.75f, paint.getAlphaf()});
            if (sequence.next() % 6 == 0) {
                paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, 1.25f, false));
            }
            const SkSamplingOptions& sampling = samplings[sequence.pick(samplings.size())];
            const uint32_t kind = sequence.next() % 7;
            if (sequence.next() % 4 == 0) {
                canvas.save();
                canvas.concat(random_transform(sequence));
            }
            if (kind == 0) {
                // Integer offsets take the sprite path.
                canvas.drawImage(image, static_cast<float>(pick_int(sequence, -5, 90)),
                                 static_cast<float>(pick_int(sequence, -5, 75)), sampling, &paint);
            } else if (kind == 1) {
                canvas.drawImage(alpha, pick_coordinate(sequence, -5, 90),
                                 pick_coordinate(sequence, -5, 75), sampling, &paint);
            } else if (kind == 2) {
                const SkRect src = SkRect::MakeXYWH(pick_coordinate(sequence, -2, 10),
                                                    pick_coordinate(sequence, -2, 8),
                                                    pick_coordinate(sequence, 1, 12),
                                                    pick_coordinate(sequence, 1, 9));
                const SkCanvas::SrcRectConstraint constraint =
                        sequence.next() % 2 ? SkCanvas::kStrict_SrcRectConstraint
                                            : SkCanvas::kFast_SrcRectConstraint;
                canvas.drawImageRect(image.get(), src, random_rect(sequence, -10, 100), sampling,
                                     &paint, constraint);
            } else if (kind == 3) {
                const SkIRect src = SkIRect::MakeXYWH(pick_int(sequence, 0, 6),
                                                      pick_int(sequence, 0, 5),
                                                      pick_int(sequence, 1, 10),
                                                      pick_int(sequence, 1, 7));
                canvas.drawImageRect(image.get(), SkRect::Make(src),
                                     random_rect(sequence, -10, 100), sampling, &paint,
                                     SkCanvas::kStrict_SrcRectConstraint);
            } else if (kind == 4) {
                canvas.drawImageNine(image.get(), SkIRect::MakeLTRB(4, 3, 12, 10),
                                     random_rect(sequence, -10, 100),
                                     sequence.next() % 2 ? SkFilterMode::kLinear
                                                         : SkFilterMode::kNearest,
                                     &paint);
            } else if (kind == 5) {
                const int xDivs[2] = {3, 11};
                const int yDivs[2] = {2, 9};
                std::array<SkCanvas::Lattice::RectType, 9> types{};
                std::array<SkColor, 9> colors{};
                for (size_t cell = 0; cell < types.size(); ++cell) {
                    const uint32_t type = sequence.next() % 3;
                    types[cell] = type == 0   ? SkCanvas::Lattice::kDefault
                                  : type == 1 ? SkCanvas::Lattice::kTransparent
                                              : SkCanvas::Lattice::kFixedColor;
                    colors[cell] = random_color(sequence);
                }
                const SkIRect bounds = SkIRect::MakeLTRB(1, 1, 16, 12);
                SkCanvas::Lattice lattice{};
                lattice.fXDivs = xDivs;
                lattice.fYDivs = yDivs;
                lattice.fRectTypes = types.data();
                lattice.fXCount = 2;
                lattice.fYCount = 2;
                lattice.fBounds = &bounds;
                lattice.fColors = colors.data();
                canvas.drawImageLattice(image.get(), lattice, random_rect(sequence, -10, 100),
                                        SkFilterMode::kNearest, &paint);
            } else {
                SkPaint shaded = paint;
                shaded.setShader((*image).makeShader(SkTileMode::kRepeat, SkTileMode::kMirror,
                                                     sampling));
                canvas.drawRect(random_rect(sequence, -10, 100), shaded);
            }
            canvas.restoreToCount(1);
        }
        target.record(output);
        ++output.cases;
    }
}

void mesh_corpus(Output& output, Sequence& sequence) {
    const std::array<SkBlendMode, 5> modes{SkBlendMode::kModulate, SkBlendMode::kSrc,
                                           SkBlendMode::kDst, SkBlendMode::kMultiply,
                                           SkBlendMode::kSrcOver};
    for (int i = 0; i < 800; ++i) {
        Target target = make_target(sequence, kN32_SkColorType, 96, 80);
        SkCanvas canvas(target.fBitmap);
        if (sequence.next() % 3 == 0) {
            canvas.concat(random_transform(sequence));
        }
        const sk_sp<SkImage> image = make_image(sequence, kN32_SkColorType, 17, 13, true);
        SkPaint paint;
        paint.setColor(random_color(sequence));
        if (sequence.next() % 2) {
            paint.setShader((*image).makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat,
                                                SkSamplingOptions(SkFilterMode::kLinear)));
        }
        const SkBlendMode mode = modes[sequence.pick(modes.size())];
        const uint32_t kind = sequence.next() % 3;
        if (kind == 0) {
            const int count = 3 + static_cast<int>(sequence.pick(7));
            std::vector<SkPoint> positions(static_cast<size_t>(count));
            std::vector<SkPoint> texs(static_cast<size_t>(count));
            std::vector<SkColor> colors(static_cast<size_t>(count));
            for (int v = 0; v < count; ++v) {
                positions[static_cast<size_t>(v)] = {pick_coordinate(sequence, -10, 110),
                                                     pick_coordinate(sequence, -10, 90)};
                texs[static_cast<size_t>(v)] = {pick_coordinate(sequence, 0, 17),
                                                pick_coordinate(sequence, 0, 13)};
                colors[static_cast<size_t>(v)] = random_color(sequence);
            }
            const SkVertices::VertexMode vertexMode = sequence.next() % 2
                                                              ? SkVertices::kTriangles_VertexMode
                                                              : SkVertices::kTriangleStrip_VertexMode;
            const bool withTexs = sequence.next() % 2 == 0;
            const bool withColors = sequence.next() % 2 == 0;
            const sk_sp<SkVertices> vertices = SkVertices::MakeCopy(
                    vertexMode, count, positions.data(), withTexs ? texs.data() : nullptr,
                    withColors ? colors.data() : nullptr);
            canvas.drawVertices(vertices, mode, paint);
        } else if (kind == 1) {
            SkPoint cubics[12] = {};
            for (SkPoint& point : cubics) {
                point = {pick_coordinate(sequence, 0, 90), pick_coordinate(sequence, 0, 75)};
            }
            const SkColor colors[4] = {random_color(sequence), random_color(sequence),
                                       random_color(sequence), random_color(sequence)};
            const SkPoint texs[4] = {{0, 0}, {17, 0}, {17, 13}, {0, 13}};
            canvas.drawPatch(cubics, sequence.next() % 2 ? colors : nullptr,
                             sequence.next() % 2 ? texs : nullptr, mode, paint);
        } else {
            const size_t count = 1 + sequence.pick(5);
            std::vector<SkRSXform> xforms(count);
            std::vector<SkRect> texs(count);
            std::vector<SkColor> colors(count);
            for (size_t a = 0; a < count; ++a) {
                xforms[a] = SkRSXform::MakeFromRadians(pick_coordinate(sequence, 0, 2) * 0.5f + 0.5f,
                                                       pick_coordinate(sequence, -3, 7) * 0.25f,
                                                       pick_coordinate(sequence, 0, 80),
                                                       pick_coordinate(sequence, 0, 60), 4, 3);
                texs[a] = SkRect::MakeXYWH(static_cast<float>(pick_int(sequence, 0, 8)),
                                           static_cast<float>(pick_int(sequence, 0, 6)), 9, 7);
                colors[a] = random_color(sequence);
            }
            const SkRect cull = SkRect::MakeWH(96, 80);
            SkPaint atlasPaint;
            atlasPaint.setAlpha(sequence.next() % 2 ? 0xFF : 0x80);
            canvas.drawAtlas(image.get(), SkSpan<const SkRSXform>(xforms.data(), count),
                             SkSpan<const SkRect>(texs.data(), count),
                             sequence.next() % 2 ? SkSpan<const SkColor>(colors.data(), count)
                                                 : SkSpan<const SkColor>(),
                             mode, SkSamplingOptions(), sequence.next() % 2 ? &cull : nullptr,
                             &atlasPaint);
        }
        target.record(output);
        ++output.cases;
    }
}

// A device wider than the scan converters' fixed-point limit is drawn in tiles.
void tiled_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 40; ++i) {
        Target target = make_target(sequence, kN32_SkColorType, 8300, 12);
        SkCanvas canvas(target.fBitmap);
        for (int step = 0; step < 6; ++step) {
            SkPaint paint = random_paint(sequence);
            paint.setPathEffect(nullptr);
            const float left = pick_coordinate(sequence, 8100, 150);
            const uint32_t kind = sequence.next() % 4;
            if (kind == 0) {
                canvas.drawRect(SkRect::MakeXYWH(left, 1.5f, 120, 7), paint);
            } else if (kind == 1) {
                canvas.drawOval(SkRect::MakeXYWH(left, 0.5f, 140, 10), paint);
            } else if (kind == 2) {
                SkPathBuilder builder;
                builder.moveTo(left, 1);
                builder.lineTo(left + 150, 6);
                builder.lineTo(left + 20, 11);
                builder.close();
                canvas.drawPath(builder.detach(), paint);
            } else {
                const SkPoint points[4] = {{left, 2}, {left + 90, 9}, {8191.5f, 5}, {8250, 3}};
                canvas.drawPoints(SkCanvas::kPolygon_PointMode, SkSpan<const SkPoint>(points, 4),
                                  paint);
            }
        }
        target.record(output);
        ++output.cases;
    }
}

void record_rect(Output& output, const SkRect& rect) {
    append(output, rect.fLeft);
    append(output, rect.fTop);
    append(output, rect.fRight);
    append(output, rect.fBottom);
}

void query_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 1000; ++i) {
        Target target = make_target(sequence, kN32_SkColorType, 64, 48);
        SkCanvas canvas(target.fBitmap);
        canvas.save();
        canvas.concat(random_transform(sequence));
        random_clip(canvas, sequence);
        if (sequence.next() % 2) {
            random_clip(canvas, sequence);
        }
        append(output, canvas.getSaveCount());
        const SkMatrix total = canvas.getTotalMatrix();
        for (int k = 0; k < 9; ++k) {
            append(output, total[k]);
        }
        record_rect(output, canvas.getLocalClipBounds());
        const SkIRect device = canvas.getDeviceClipBounds();
        append(output, device.fLeft);
        append(output, device.fTop);
        append(output, device.fRight);
        append(output, device.fBottom);
        append_bool(output, canvas.isClipEmpty());
        append_bool(output, canvas.isClipRect());
        for (int k = 0; k < 4; ++k) {
            append_bool(output, canvas.quickReject(random_rect(sequence, -20, 90)));
        }
        append_bool(output, canvas.quickReject(random_path(sequence, -20, 90)));

        SkPaint paint = random_paint(sequence);
        canvas.drawRect(random_rect(sequence, -5, 60), paint);
        canvas.restore();

        // Pixel transfer through the canvas.
        std::array<uint32_t, 6 * 5> block{};
        for (size_t p = 0; p < block.size(); ++p) {
            block[p] = 0xFF000000u | static_cast<uint32_t>(p * 2654435761u);
        }
        const SkImageInfo blockInfo = SkImageInfo::MakeN32Premul(6, 5);
        const int wx = pick_int(sequence, -4, 64);
        const int wy = pick_int(sequence, -4, 48);
        append_bool(output, canvas.writePixels(blockInfo, block.data(), 6 * 4, wx, wy));
        std::array<uint32_t, 7 * 6> readBack{};
        const SkImageInfo readInfo = SkImageInfo::MakeN32Premul(7, 6);
        append_bool(output, canvas.readPixels(readInfo, readBack.data(), 7 * 4,
                                              pick_int(sequence, -4, 64),
                                              pick_int(sequence, -4, 48)));
        for (const uint32_t pixel : readBack) {
            append(output, pixel);
        }
        target.record(output);
        ++output.cases;
    }
}

// Draws a case with and without its blur on a fresh target. Returns false when the blur
// was lost, which is how c046c51's rejected mask queries showed: the draw fell back to the
// unblurred geometry.
bool blur_applied(Output& output, const SkPath* path, const SkPoint* points, size_t count,
                  const SkPaint& blurred) {
    SkBitmap withBlur;
    SkBitmap without;
    withBlur.allocN32Pixels(96, 80);
    without.allocN32Pixels(96, 80);
    withBlur.eraseColor(0xFF808080);
    without.eraseColor(0xFF808080);
    SkPaint plain = blurred;
    plain.setMaskFilter(nullptr);
    SkCanvas blurredCanvas(withBlur);
    SkCanvas plainCanvas(without);
    if (path != nullptr) {
        blurredCanvas.drawPath(*path, blurred);
        plainCanvas.drawPath(*path, plain);
    } else {
        blurredCanvas.drawPoints(SkCanvas::kPoints_PointMode, SkSpan<const SkPoint>(points, count),
                                 blurred);
        plainCanvas.drawPoints(SkCanvas::kPoints_PointMode, SkSpan<const SkPoint>(points, count),
                               plain);
    }
    const Target target{withBlur};
    target.record(output);
    ++output.cases;
    bool differs = false;
    for (int y = 0; y < withBlur.height(); ++y) {
        const void* blurredRow = withBlur.getAddr(0, y);
        const void* plainRow = without.getAddr(0, y);
        if (std::memcmp(blurredRow, plainRow, withBlur.info().minRowBytes()) != 0) {
            differs = true;
        }
    }
    return differs;
}

// Blurred inverse fills and half-pixel butt- and square-capped points must stay blurred.
bool blur_regressions(Output& output) {
    SkPathBuilder builder;
    builder.moveTo(20, 15);
    builder.lineTo(70, 25);
    builder.lineTo(35, 65);
    builder.close();
    builder.moveTo(30, 30);
    builder.lineTo(50, 30);
    builder.lineTo(40, 45);
    builder.close();
    const SkPath triangles = builder.detach();
    const std::array<SkPathFillType, 2> inverseFills{SkPathFillType::kInverseWinding,
                                                     SkPathFillType::kInverseEvenOdd};
    const std::array<SkPoint, 3> points{SkPoint{10.5f, 12.25f}, SkPoint{60, 18},
                                        SkPoint{33, 70.5f}};
    const std::array<SkPaint::Cap, 2> caps{SkPaint::kButt_Cap, SkPaint::kSquare_Cap};
    for (const float sigma : {1.5f, 4.0f}) {
        for (const bool antiAlias : {false, true}) {
            SkPaint paint;
            paint.setColor(0xC0204080);
            paint.setAntiAlias(antiAlias);
            paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, sigma, false));
            for (const SkPathFillType fill : inverseFills) {
                const SkPath inverse = triangles.makeFillType(fill);
                if (!blur_applied(output, &inverse, nullptr, 0, paint)) return false;
            }
            for (const SkPaint::Cap cap : caps) {
                SkPaint pointPaint = paint;
                pointPaint.setStrokeWidth(0.5f);
                pointPaint.setStrokeCap(cap);
                if (!blur_applied(output, nullptr, points.data(), points.size(), pointPaint)) {
                    return false;
                }
            }
        }
    }
    return true;
}

void canvas_corpus(Output& output) {
    Sequence sequence{};
    primitive_corpus(output, sequence);
    image_corpus(output, sequence);
    mesh_corpus(output, sequence);
    tiled_corpus(output, sequence);
    query_corpus(output, sequence);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
    Output output{};
    canvas_corpus(output);
    if (!blur_regressions(output)) {
        std::puts("canvas device: blur regression failed");
        return 1;
    }
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("canvas device: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
