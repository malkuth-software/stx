// Oracle and consistency checks that do not trust Skia's own answers.
//
// Oracles compare drawn or computed values with an independent model: exact fills, analytic
// coverage of rectangles and a disc within a stated bound, linear-gradient values against
// the interpolation formula, matrix mapping and inversion against double arithmetic, M44
// rotation orthonormality, and premultiply/unpremultiply round trips. Consistency checks
// compare two of Skia's own answers that must agree: fast bounds against the pixels a draw
// touched, device clip bounds against the pixels a clipped draw touched, and the computed byte
// size of an image against the data length SkMallocPixelRef accepts.
//
// Each check returns false on the first case outside its bound and prints the worst error it
// measured, so a later change can be compared against the recorded margin. The checks run on
// both profiles.

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkM44.h"
#include "include/core/SkMallocPixelRef.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPixelRef.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSurface.h"
#include "include/core/SkUnPreMultiply.h"
#include "gradient_result.hpp"
#include "peek_pixels.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>

namespace {

// An N32 premultiplied raster surface of the given size, or null.
sk_sp<SkSurface> make_color_surface(int width, int height) {
    const SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    return surface;
}

// An alpha-only raster surface, whose pixels are coverage, or null.
sk_sp<SkSurface> make_coverage_surface(int width, int height) {
    const SkImageInfo info = SkImageInfo::MakeA8(width, height);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    return surface;
}

// The N32 pixel at (x, y) of a surface, unpremultiplied (opaque pixels read back exactly).
SkColor color_at(SkSurface& surface, int x, int y) {
    const std::optional<SkPixmap> pixels = peek_pixels(surface);
    if (!pixels) std::abort();
    const SkColor color = (*pixels).getColor(x, y);
    return color;
}

// The coverage byte at (x, y) of an alpha-only surface.
uint8_t coverage_at(SkSurface& surface, int x, int y) {
    const std::optional<SkPixmap> pixels = peek_pixels(surface);
    if (!pixels) std::abort();
    const uint8_t value = *(*pixels).addr8(x, y);
    return value;
}

// Opaque fills of integer rectangles write exactly the paint colour inside and leave every
// other pixel untouched, with and without antialiasing. A translucent source-over fill of an
// opaque destination matches s*a + d*(1-a) per channel within one level.
bool exact_fills() {
    constexpr int width = 16;
    constexpr int height = 12;
    constexpr SkColor background = 0xFF102030;
    const SkIRect filled = SkIRect::MakeLTRB(3, 2, 11, 9);
    int worst_blend = 0;
    for (const bool antialias : {false, true}) {
        sk_sp<SkSurface> surface = make_color_surface(width, height);
        if (!surface) return false;
        SkCanvas& canvas = *(*surface).getCanvas();
        canvas.clear(background);
        SkPaint opaque;
        opaque.setAntiAlias(antialias);
        opaque.setColor(0xFFC08040);
        canvas.drawRect(SkRect::Make(filled), opaque);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const SkColor expected = filled.contains(x, y) ? 0xFFC08040 : background;
                if (color_at(*surface, x, y) != expected) return false;
            }
        }
        canvas.clear(background);
        SkPaint translucent;
        translucent.setAntiAlias(antialias);
        translucent.setColor(0x80C08040);
        canvas.drawRect(SkRect::Make(filled), translucent);
        const double alpha = 128.0 / 255.0;
        const SkColor blended = color_at(*surface, 5, 5);
        const std::array<int, 3> shifts = {16, 8, 0};
        for (const int shift : shifts) {
            const double source = static_cast<double>((0xFFC08040U >> shift) & 0xFFU);
            const double destination = static_cast<double>((background >> shift) & 0xFFU);
            const double model = source * alpha + destination * (1.0 - alpha);
            const double drawn = static_cast<double>((blended >> shift) & 0xFFU);
            const int error = static_cast<int>(std::ceil(std::abs(drawn - model) - 0.5));
            worst_blend = std::max(worst_blend, error);
        }
        // The translucent fill leaves the pixels outside it untouched too.
        if (color_at(*surface, 0, 0) != background) return false;
    }
    std::printf("exact fills: opaque exact, worst source-over error %d level(s) (bound 1)\n", worst_blend);
    const bool within = worst_blend <= 1;
    return within;
}

// Antialiased coverage of a rectangle whose left and right edges cover three quarters of a
// pixel is 0.75 * 255 within two levels in the edge columns and full inside; the summed
// coverage of a disc of radius 20 is pi * 400 within one percent.
bool analytic_coverage() {
    sk_sp<SkSurface> surface = make_coverage_surface(16, 12);
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.clear(SK_ColorTRANSPARENT);
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SK_ColorWHITE);
    canvas.drawRect(SkRect::MakeLTRB(2.25f, 2.0f, 9.75f, 9.0f), paint);
    int worst_edge = 0;
    for (int y = 2; y < 9; ++y) {
        const int left = static_cast<int>(coverage_at(*surface, 2, y));
        const int right = static_cast<int>(coverage_at(*surface, 9, y));
        worst_edge = std::max(worst_edge, std::abs(left - 191));
        worst_edge = std::max(worst_edge, std::abs(right - 191));
        for (int x = 3; x < 9; ++x) {
            if (coverage_at(*surface, x, y) != 255) return false;
        }
        if (coverage_at(*surface, 1, y) != 0 || coverage_at(*surface, 10, y) != 0) return false;
    }
    sk_sp<SkSurface> disc_surface = make_coverage_surface(64, 64);
    if (!disc_surface) return false;
    SkCanvas& disc_canvas = *(*disc_surface).getCanvas();
    disc_canvas.clear(SK_ColorTRANSPARENT);
    disc_canvas.drawCircle(32.0f, 32.0f, 20.0f, paint);
    double area = 0.0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            area += static_cast<double>(coverage_at(*disc_surface, x, y)) / 255.0;
        }
    }
    const double model = 3.14159265358979323846 * 400.0;
    const double relative = std::abs(area - model) / model;
    std::printf("analytic coverage: worst edge error %d level(s) (bound 2), disc area %.3f against "
                "%.3f, relative error %.5f (bound 0.01)\n", worst_edge, area, model, relative);
    const bool within = worst_edge <= 2 && relative <= 0.01;
    return within;
}

// A clamped black-to-white linear gradient across 256 pixels has, at pixel i, the value
// 255 * (i + 0.5) / 256 within one level, and its end pixels are within one level of 0 and 255.
bool gradient_values() {
    constexpr int width = 256;
    sk_sp<SkSurface> surface = make_color_surface(width, 1);
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.clear(SK_ColorTRANSPARENT);
    const SkColor4f stops[2] = {{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};
    const SkGradient::Colors colors(SkSpan<const SkColor4f>(stops, 2), SkTileMode::kClamp);
    const SkGradient gradient(colors, {});
    const SkPoint points[2] = {{0.0f, 0.0f}, {static_cast<float>(width), 0.0f}};
    sk_sp<SkShader> shader = shader_of(SkShaders::LinearGradient(points, gradient, nullptr));
    if (!shader) return false;
    SkPaint paint;
    paint.setShader(std::move(shader));
    paint.setDither(false);
    canvas.drawPaint(paint);
    double worst = 0.0;
    for (int x = 0; x < width; ++x) {
        const SkColor drawn = color_at(*surface, x, 0);
        const double model = 255.0 * (static_cast<double>(x) + 0.5) / static_cast<double>(width);
        const std::array<int, 3> shifts = {16, 8, 0};
        for (const int shift : shifts) {
            const double value = static_cast<double>((drawn >> shift) & 0xFFU);
            worst = std::max(worst, std::abs(value - model));
        }
    }
    std::printf("gradient values: worst error %.3f level(s) (bound 1.5)\n", worst);
    const bool within = worst <= 1.5;
    return within;
}

// One affine matrix for the mapping and inversion oracles, in SkMatrix argument order.
struct AffineCase {
    float scale_x;
    float skew_x;
    float translate_x;
    float skew_y;
    float scale_y;
    float translate_y;
};

// SkMatrix maps points as the double-precision product does, within a relative error of
// 1e-6 of the result's magnitude (about 8 float ulps); a matrix composed with its inverse is
// the identity within 1e-5, which covers the 1e6-conditioned case; SkM44 rotations are
// orthonormal within 2e-6 (about 16 float ulps over three products and a sum).
bool matrix_arithmetic() {
    const std::array<AffineCase, 6> cases = {{
        {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
        {2.5f, 0.0f, -7.25f, 0.0f, 0.75f, 3.5f},
        {0.8660254f, -0.5f, 12.0f, 0.5f, 0.8660254f, -4.0f},
        {1.5f, 0.25f, 100.0f, -0.125f, 3.0f, -50.0f},
        {-1.0f, 0.0f, 31.0f, 0.0f, 1.0f, 0.0f},
        {1000.0f, 3.0f, 0.5f, -2.0f, 0.001f, 7.0f},
    }};
    const std::array<SkPoint, 5> points = {{
        {0.0f, 0.0f}, {1.0f, 1.0f}, {-3.5f, 2.25f}, {123.456f, -78.9f}, {1.0e4f, 1.0e-3f},
    }};
    double worst_map = 0.0;
    double worst_inverse = 0.0;
    for (const AffineCase& entry : cases) {
        const SkMatrix matrix = SkMatrix::MakeAll(entry.scale_x, entry.skew_x, entry.translate_x,
                                                  entry.skew_y, entry.scale_y, entry.translate_y,
                                                  0.0f, 0.0f, 1.0f);
        for (const SkPoint point : points) {
            const SkPoint mapped = matrix.mapPoint(point);
            const double x = static_cast<double>(entry.scale_x) * point.fX +
                             static_cast<double>(entry.skew_x) * point.fY + entry.translate_x;
            const double y = static_cast<double>(entry.skew_y) * point.fX +
                             static_cast<double>(entry.scale_y) * point.fY + entry.translate_y;
            const double magnitude = std::max(1.0, std::abs(x) + std::abs(y));
            const double error = (std::abs(mapped.fX - x) + std::abs(mapped.fY - y)) / magnitude;
            worst_map = std::max(worst_map, error);
        }
        SkMatrix inverse;
        if (!matrix.invert(&inverse)) return false;
        const SkMatrix product = SkMatrix::Concat(matrix, inverse);
        const std::array<float, 6> identity = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        const std::array<float, 6> found = {product.getScaleX(), product.getSkewX(),
                                            product.getTranslateX(), product.getSkewY(),
                                            product.getScaleY(), product.getTranslateY()};
        for (size_t index = 0; index < identity.size(); ++index) {
            worst_inverse = std::max(worst_inverse,
                                     std::abs(static_cast<double>(found[index]) - identity[index]));
        }
    }
    const std::array<SkV3, 4> axes = {{{0.0f, 0.0f, 1.0f}, {1.0f, 2.0f, 3.0f},
                                       {-0.3f, 0.7f, 0.1f}, {1.0e-3f, 5.0f, -2.0f}}};
    const std::array<float, 4> angles = {0.1f, 1.0f, 2.5f, -3.0f};
    double worst_orthonormal = 0.0;
    for (const SkV3 axis : axes) {
        for (const float angle : angles) {
            const SkM44 rotation = SkM44::Rotate(axis, angle);
            for (int row = 0; row < 3; ++row) {
                for (int other = 0; other < 3; ++other) {
                    double dot = 0.0;
                    for (int column = 0; column < 3; ++column) {
                        dot += static_cast<double>(rotation.rc(row, column)) * rotation.rc(other, column);
                    }
                    const double expected = row == other ? 1.0 : 0.0;
                    worst_orthonormal = std::max(worst_orthonormal, std::abs(dot - expected));
                }
            }
        }
    }
    std::printf("matrix arithmetic: worst map error %.3g (bound 1e-6), inverse error %.3g (bound 1e-5), "
                "rotation orthonormality error %.3g (bound 2e-6)\n",
                worst_map, worst_inverse, worst_orthonormal);
    const bool within = worst_map <= 1.0e-6 && worst_inverse <= 1.0e-5 && worst_orthonormal <= 2.0e-6;
    return within;
}

// For every alpha and channel value, premultiplying the unpremultiplied colour of a
// premultiplied pixel gives that pixel back exactly, and unpremultiplying recovers the
// original channel within 255 / (2 * alpha) + 1 levels (the premultiplied byte loses that much).
bool premultiply_round_trips() {
    double worst_ratio = 0.0;
    for (int alpha = 1; alpha <= 255; ++alpha) {
        for (int channel = 0; channel <= 255; ++channel) {
            const SkPMColor premultiplied = SkPreMultiplyARGB(static_cast<U8CPU>(alpha),
                static_cast<U8CPU>(channel), static_cast<U8CPU>(channel), static_cast<U8CPU>(channel));
            const SkColor recovered = SkUnPreMultiply::PMColorToColor(premultiplied);
            const SkPMColor again = SkPreMultiplyARGB(SkColorGetA(recovered), SkColorGetR(recovered),
                                                      SkColorGetG(recovered), SkColorGetB(recovered));
            if (again != premultiplied) return false;
            const double error = std::abs(static_cast<double>(SkColorGetR(recovered)) - channel);
            const double bound = 255.0 / (2.0 * alpha) + 1.0;
            if (error > bound) return false;
            worst_ratio = std::max(worst_ratio, error / bound);
        }
    }
    std::printf("premultiply round trips: exact re-premultiply, worst error %.3f of its bound\n", worst_ratio);
    return true;
}

// The smallest rectangle holding every non-zero coverage byte, or an empty one.
SkIRect touched_bounds(SkSurface& surface, int width, int height) {
    SkIRect bounds = SkIRect::MakeEmpty();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (coverage_at(surface, x, y) == 0) continue;
            bounds.join(SkIRect::MakeXYWH(x, y, 1, 1));
        }
    }
    return bounds;
}

// One shape for the fast-bounds check: a path, how it is painted, and its transform.
struct BoundsCase {
    SkPath path;
    SkPaint::Style style;
    float stroke_width;
    SkPaint::Cap cap;
    SkPaint::Join join;
    SkMatrix matrix;
};

std::vector<BoundsCase> bounds_cases() {
    std::vector<BoundsCase> cases;
    SkPathBuilder curve;
    curve.moveTo(10.0f, 60.0f);
    curve.cubicTo(30.0f, -10.0f, 70.0f, 130.0f, 100.0f, 40.0f);
    curve.quadTo(110.0f, 100.0f, 40.0f, 110.0f);
    const SkPath curve_path = curve.detach();
    SkPathBuilder spike;
    spike.moveTo(20.0f, 100.0f);
    spike.lineTo(60.0f, 20.0f);
    spike.lineTo(64.0f, 100.0f);
    const SkPath spike_path = spike.detach();
    const SkPath circle = SkPath::Circle(64.0f, 64.0f, 30.5f);
    const SkMatrix rotated = SkMatrix::RotateDeg(17.0f, {64.0f, 64.0f});
    const SkMatrix scaled = SkMatrix::Scale(0.75f, 1.25f);
    cases.push_back({curve_path, SkPaint::kFill_Style, 0.0f, SkPaint::kButt_Cap, SkPaint::kMiter_Join, SkMatrix::I()});
    cases.push_back({curve_path, SkPaint::kStroke_Style, 6.0f, SkPaint::kRound_Cap, SkPaint::kRound_Join, rotated});
    cases.push_back({spike_path, SkPaint::kStroke_Style, 5.0f, SkPaint::kSquare_Cap, SkPaint::kMiter_Join, SkMatrix::I()});
    cases.push_back({spike_path, SkPaint::kStroke_Style, 5.0f, SkPaint::kButt_Cap, SkPaint::kBevel_Join, scaled});
    cases.push_back({circle, SkPaint::kStrokeAndFill_Style, 3.0f, SkPaint::kButt_Cap, SkPaint::kMiter_Join, rotated});
    cases.push_back({spike_path, SkPaint::kStroke_Style, 0.0f, SkPaint::kButt_Cap, SkPaint::kMiter_Join, rotated});
    return cases;
}

// Every pixel a draw touches lies inside the paint's fast bounds of the shape, mapped by the
// matrix and rounded out, widened by one pixel for antialiasing. The overshoot (bounds area
// beyond the touched pixels) is recorded, not bounded.
bool fast_bounds_hold_pixels() {
    constexpr int size = 128;
    const std::vector<BoundsCase> cases = bounds_cases();
    int worst_overshoot = 0;
    for (const BoundsCase& entry : cases) {
        sk_sp<SkSurface> surface = make_coverage_surface(size, size);
        if (!surface) return false;
        SkCanvas& canvas = *(*surface).getCanvas();
        canvas.clear(SK_ColorTRANSPARENT);
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(SK_ColorWHITE);
        paint.setStyle(entry.style);
        paint.setStrokeWidth(entry.stroke_width);
        paint.setStrokeCap(entry.cap);
        paint.setStrokeJoin(entry.join);
        if (!paint.canComputeFastBounds()) return false;
        canvas.setMatrix(entry.matrix);
        canvas.drawPath(entry.path, paint);
        SkRect storage;
        const SkRect local = paint.computeFastBounds(entry.path.getBounds(), &storage);
        const SkRect device = entry.matrix.mapRect(local);
        const SkIRect allowed = device.roundOut().makeOutset(1, 1);
        const SkIRect touched = touched_bounds(*surface, size, size);
        if (touched.isEmpty()) return false;
        if (!allowed.contains(touched)) return false;
        const int overshoot = std::max({touched.fLeft - allowed.fLeft, touched.fTop - allowed.fTop,
                                        allowed.fRight - touched.fRight, allowed.fBottom - touched.fBottom});
        worst_overshoot = std::max(worst_overshoot, overshoot);
    }
    std::printf("fast bounds: every touched pixel inside, worst overshoot %d pixel(s) (recorded)\n",
                worst_overshoot);
    return true;
}

// After a clip, a paint over the whole canvas touches only pixels inside the device clip
// bounds, and for an axis-aligned integer rectangle the two are equal.
bool clip_bounds_hold_pixels() {
    constexpr int size = 96;
    int worst_slack = 0;
    for (int shape = 0; shape < 4; ++shape) {
        sk_sp<SkSurface> surface = make_coverage_surface(size, size);
        if (!surface) return false;
        SkCanvas& canvas = *(*surface).getCanvas();
        canvas.clear(SK_ColorTRANSPARENT);
        if (shape == 0) canvas.clipRect(SkRect::MakeLTRB(10.0f, 12.0f, 70.0f, 50.0f));
        if (shape == 1) canvas.clipRect(SkRect::MakeLTRB(10.3f, 12.6f, 70.2f, 50.9f), true);
        if (shape == 2) {
            canvas.rotate(23.0f, 48.0f, 48.0f);
            canvas.clipRRect(SkRRect::MakeRectXY(SkRect::MakeLTRB(20.0f, 25.0f, 80.0f, 70.0f), 9.0f, 5.0f), true);
            canvas.resetMatrix();
        }
        if (shape == 3) {
            canvas.clipPath(SkPath::Circle(40.0f, 52.0f, 27.25f), true);
            canvas.clipRect(SkRect::MakeLTRB(0.0f, 0.0f, 55.5f, 96.0f), true);
        }
        const SkIRect clip = canvas.getDeviceClipBounds();
        SkPaint paint;
        paint.setColor(SK_ColorWHITE);
        canvas.drawPaint(paint);
        const SkIRect touched = touched_bounds(*surface, size, size);
        if (touched.isEmpty() || !clip.contains(touched)) return false;
        if (shape == 0 && touched != clip) return false;
        const int slack = std::max({touched.fLeft - clip.fLeft, touched.fTop - clip.fTop,
                                    clip.fRight - touched.fRight, clip.fBottom - touched.fBottom});
        worst_slack = std::max(worst_slack, slack);
    }
    std::printf("clip bounds: every touched pixel inside, worst slack %d pixel(s) (recorded)\n", worst_slack);
    return true;
}

// SkMallocPixelRef accepts data of exactly an image's computed byte size and refuses one byte
// less, for padded and unpadded strides of several colour types.
bool byte_size_matches_acceptance() {
    const std::array<SkColorType, 4> colors = {kAlpha_8_SkColorType, kRGB_565_SkColorType,
                                               kN32_SkColorType, kRGBA_F16_SkColorType};
    for (const SkColorType color : colors) {
        for (int width = 1; width <= 9; width += 4) {
            for (int height = 1; height <= 7; height += 3) {
                const SkImageInfo info = SkImageInfo::Make(width, height, color, kPremul_SkAlphaType);
                for (size_t padding = 0; padding <= 8; padding += 8) {
                    const size_t row_bytes = info.minRowBytes() + padding;
                    const size_t needed = info.computeByteSize(row_bytes);
                    if (SkImageInfo::ByteSizeOverflowed(needed) || needed == 0) return false;
                    sk_sp<SkData> exact = SkData::MakeZeroInitialized(needed);
                    sk_sp<SkData> short_data = SkData::MakeZeroInitialized(needed - 1);
                    if (!SkMallocPixelRef::MakeWithData(info, row_bytes, exact)) return false;
                    if (SkMallocPixelRef::MakeWithData(info, row_bytes, short_data)) return false;
                    const size_t last_row_end = (static_cast<size_t>(height) - 1) * row_bytes +
                                                info.minRowBytes();
                    if (needed != last_row_end) return false;
                }
            }
        }
    }
    std::printf("byte sizes: computed size is exactly the accepted data length\n");
    return true;
}

}  // namespace

int main() {
    if (!exact_fills()) return 1;
    if (!analytic_coverage()) return 2;
    if (!gradient_values()) return 3;
    if (!matrix_arithmetic()) return 4;
    if (!premultiply_round_trips()) return 5;
    if (!fast_bounds_hold_pixels()) return 6;
    if (!clip_bounds_hold_pixels()) return 7;
    if (!byte_size_matches_acceptance()) return 8;
    std::puts("oracle and consistency checks: PASS");
    return 0;
}
