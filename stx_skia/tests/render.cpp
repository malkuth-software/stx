#include "include/core/SkCanvas.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRegion.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkShaderMaskFilter.h"
#include "include/core/SkMaskFilter.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
constexpr int width = 173;
constexpr int height = 129;
constexpr std::size_t stride = width * 4U + 28U;

void draw_scene(SkCanvas& canvas, int variation) {
    canvas.clear(0x60402010U);
    canvas.save();
    canvas.translate(3.25F, 1.75F);
    canvas.rotate(static_cast<float>(variation - 4) * 7.0F);
    canvas.scale(0.85F, 1.1F);
    SkPaint paint;
    paint.setAntiAlias(variation % 2 == 0);
    paint.setColor(0xA0E04020U);
    canvas.drawRect(SkRect::MakeXYWH(4, 9, 60, 35), paint);
    SkRRect rounded;
    const SkVector radii[4] = {{2, 7}, {14, 5}, {8, 12}, {0, 0}};
    rounded.setRectRadii(SkRect::MakeXYWH(9, 12, 120, 88), radii);
    canvas.clipRRect(rounded, SkClipOp::kIntersect, true);
    const std::array<SkColor4f, 3> colours = {{{1, 0, 0, 0.3F}, {0, 1, 0, 1}, {0, 0, 1, 0.7F}}};
    const std::array<float, 3> stops = {0, 0.4F, 1};
    SkGradient::Interpolation interpolation;
    interpolation.fInPremul = SkGradient::Interpolation::InPremul::kYes;
    const SkTileMode mode = variation % 3 == 0 ? SkTileMode::kClamp
        : variation % 3 == 1 ? SkTileMode::kRepeat : SkTileMode::kMirror;
    const SkGradient::Colors gradient_colours(
        SkSpan<const SkColor4f>(colours.data(), colours.size()),
        SkSpan<const float>(stops.data(), stops.size()), mode, SkColorSpace::MakeSRGB());
    const SkGradient gradient(gradient_colours, interpolation);
    const SkPoint points[2] = {{10, 10}, {70, 95}};
    paint.setShader(SkShaders::LinearGradient(points, gradient));
    canvas.drawRRect(rounded, paint);
    paint.setShader(SkShaders::RadialGradient({60, 50}, 40, gradient));
    canvas.drawOval(SkRect::MakeXYWH(22, 15, 100, 70), paint);
    paint.setShader(nullptr);
    paint.setColor(0xD04070E0U);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(3.5F);
    const float dash[4] = {5, 3, 1, 2};
    paint.setPathEffect(SkDashPathEffect::Make(SkSpan<const float>(dash, 4), 1.5F));
    SkPathBuilder path(SkPathFillType::kEvenOdd);
    path.moveTo(7, 5);
    path.lineTo(145, 11);
    path.quadTo(80, 42, 145, 85);
    path.cubicTo(80, 120, 20, 0, 7, 5);
    path.close();
    canvas.drawPath(path.detach(), paint);
    canvas.restore();
    canvas.save();
    SkRegion region;
    region.setRect(SkIRect::MakeXYWH(0, 0, 120, 80));
    region.op(SkIRect::MakeXYWH(90, 30, 50, 90), SkRegion::kUnion_Op);
    canvas.clipRegion(region, SkClipOp::kIntersect);
    canvas.clipPath(SkPath::Oval(SkRect::MakeXYWH(0, 0, 160, 125)), SkClipOp::kIntersect, true);
    paint.setPathEffect(nullptr);
    paint.setStyle(SkPaint::kFill_Style);
    paint.setShader(SkShaders::SweepGradient({70, 60}, gradient));
    canvas.drawRect(SkRect::MakeWH(170, 128), paint);
    canvas.restore();
    paint.setShader(nullptr);
    paint.setPathEffect(nullptr);
    paint.setStyle(SkPaint::kFill_Style);
    paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, 3.25F, false));
    canvas.drawRRect(rounded, paint);
    paint.setMaskFilter(nullptr);
    const SkBlendMode modes[6] = {SkBlendMode::kSrcOver, SkBlendMode::kSrc,
        SkBlendMode::kMultiply, SkBlendMode::kScreen, SkBlendMode::kDarken, SkBlendMode::kLighten};
    paint.setBlendMode(modes[variation % 6]);
    const SkRect layer_bounds = SkRect::MakeXYWH(0, 0, 150, 110);
    canvas.saveLayerAlpha(&layer_bounds, 137);
    canvas.drawOval(SkRect::MakeXYWH(25, 35, 65, 60), paint);
    canvas.drawOval(SkRect::MakeXYWH(60, 35, 65, 60), paint);
    canvas.restore();
    std::array<std::uint32_t, 63> pixels{};
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = 0xFF000000U | static_cast<std::uint32_t>(i * 19473U);
    }
    const SkImageInfo info = SkImageInfo::MakeN32Premul(9, 7);
    const sk_sp<SkData> data = SkData::MakeWithCopy(pixels.data(), sizeof(pixels));
    const sk_sp<SkImage> image = SkImages::RasterFromData(info, data, 9U * 4U);
    const SkSamplingOptions sampling(variation % 2 == 0 ? SkFilterMode::kNearest : SkFilterMode::kLinear);
    SkPaint filtered;
    const float matrix[20] = {1, 0, 0, 0, 0.1F, 0, 0.5F, 0, 0, 0,
        0, 0, 1, 0, 0, 0, 0, 0, 0.7F, 0};
    filtered.setColorFilter(SkColorFilters::Matrix(matrix));
    canvas.drawImageRect(image.get(), SkRect::MakeXYWH(1, 1, 5, 4),
        SkRect::MakeXYWH(93, 70, 75, 54), sampling, &filtered, SkCanvas::kStrict_SrcRectConstraint);
    const SkSamplingOptions cubic(SkCubicResampler::Mitchell());
    const SkSamplingOptions mipmapped(SkFilterMode::kLinear, SkMipmapMode::kLinear);
    canvas.drawImageRect(image.get(), SkRect::MakeXYWH(50, 100, 4, 3),
        variation % 2 == 0 ? cubic : mipmapped, nullptr);
    std::array<std::uint8_t, 64> mask_pixels{};
    for (std::size_t i = 0; i < mask_pixels.size(); ++i) mask_pixels[i] = static_cast<std::uint8_t>(i * 4U);
    const SkImageInfo mask_info = SkImageInfo::MakeA8(8, 8);
    const sk_sp<SkData> mask_data = SkData::MakeWithCopy(mask_pixels.data(), mask_pixels.size());
    const sk_sp<SkImage> mask = SkImages::RasterFromData(mask_info, mask_data, 8);
    paint.setBlendMode(SkBlendMode::kSrcOver);
    paint.setMaskFilter(SkShaderMaskFilter::Make((*mask).makeShader(
        SkTileMode::kRepeat, SkTileMode::kMirror, SkSamplingOptions())));
    canvas.drawRect(SkRect::MakeXYWH(1, 100, 35, 20), paint);
    paint.setMaskFilter(nullptr);
    canvas.drawImage(mask, 140, 10, SkSamplingOptions(), &paint);

}
}

int main(int argc, char** argv) {
    FILE* output = nullptr;
    if (argc == 2) output = std::fopen(argv[1], "wb");
    if (argc == 2 && output == nullptr) return 2;
    double draw_seconds = 0.0;
    std::uint64_t hash = 1469598103934665603ULL;
    for (int format = 0; format < 2; ++format) {
        for (int variation = 0; variation < 12; ++variation) {
            std::vector<std::uint8_t> pixels(stride * height, 0xA5U);
            const SkImageInfo info = SkImageInfo::Make(width, height,
                format == 0 ? kRGBA_8888_SkColorType : kBGRA_8888_SkColorType,
                kPremul_SkAlphaType, SkColorSpace::MakeSRGB());
            sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels.data(), stride);
            if (!surface) return 3;
            SkCanvas& canvas = *(*surface).getCanvas();
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            draw_scene(canvas, variation);
            const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
            draw_seconds += std::chrono::duration<double>(end - start).count();
            if (canvas.getSaveCount() != 1) return 4;
            for (int y = 0; y < height; ++y) {
                const std::size_t row = static_cast<std::size_t>(y) * stride;
                for (std::size_t x = width * 4U; x < stride; ++x) {
                    if (pixels[row + x] != 0xA5U) return 5;
                }
            }
            for (const std::uint8_t value : pixels) {
                hash ^= value;
                hash *= 1099511628211ULL;
            }
            if (output != nullptr && std::fwrite(pixels.data(), 1, pixels.size(), output) != pixels.size()) return 6;
        }
    }
    if (output != nullptr && std::fclose(output) != 0) return 7;
    std::printf("cases=24 hash=%llu PASS\n", static_cast<unsigned long long>(hash));
    std::printf("draw_seconds=%.9f\n", draw_seconds);
    return 0;
}
