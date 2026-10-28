#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkShader.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkGradient.h"
#include "src/core/SkRasterClip.h"
#include "gradient_result.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {
bool sample_constant_clip(int width, int drawLeft, int drawWidth, int sampleX,
                          uint8_t& alpha) {
    const SkImageInfo info = SkImageInfo::MakeN32Premul(width, 2);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.clear(SK_ColorTRANSPARENT);
    canvas.clipShader(SkShaders::Color(SkColorSetARGB(128, 0, 0, 0)));
    SkPaint paint;
    paint.setColor(SK_ColorWHITE);
    canvas.drawRect(SkRect::MakeXYWH(drawLeft, 0, drawWidth, 2), paint);
    SkPixmap pixels;
    if (!(*surface).peekPixels(&pixels)) return false;
    alpha = SkColorGetA(pixels.getColor(sampleX, 0));
    return true;
}

bool ordinary_case() {
    uint8_t alpha = 0;
    if (!sample_constant_clip(16, 0, 16, 2, alpha)) return false;
    return alpha > 0 && alpha < 255;
}

#ifndef STX_SKIA_REFERENCE
bool sample_gradient_clip(int width, int deviceOffset, int sampleX, uint8_t& alpha) {
    const SkImageInfo info = SkImageInfo::MakeN32Premul(width, 2);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.clear(SK_ColorTRANSPARENT);
    canvas.translate(-static_cast<float>(deviceOffset), 0);

    const std::array<SkColor4f, 2> colors = {{{0, 0, 0, 0}, {0, 0, 0, 1}}};
    const std::array<float, 2> stops = {0, 1};
    const SkGradient::Colors gradientColors(
            SkSpan<const SkColor4f>(colors.data(), colors.size()),
            SkSpan<const float>(stops.data(), stops.size()),
            SkTileMode::kClamp, SkColorSpace::MakeSRGB());
    const SkGradient::Interpolation interpolation;
    const SkGradient gradient(gradientColors, interpolation);
    const SkPoint points[2] = {{8000, 0}, {9000, 0}};
    canvas.clipShader(shader_of(SkShaders::LinearGradient(points, gradient)));
    canvas.clipShader(SkShaders::Color(SkColorSetARGB(192, 0, 0, 0)));

    SkPaint paint;
    paint.setColor(SK_ColorWHITE);
    canvas.drawRect(SkRect::MakeXYWH(8400, 0, 200, 2), paint);
    SkPixmap pixels;
    if (!(*surface).peekPixels(&pixels)) return false;
    alpha = SkColorGetA(pixels.getColor(sampleX, 0));
    return true;
}

bool tiled_case() {
    uint8_t ordinary = 0;
    uint8_t firstTile = 0;
    uint8_t secondTile = 0;
    if (!sample_constant_clip(16, 0, 16, 2, ordinary)) return false;
    if (!sample_constant_clip(9000, 0, 9000, 2, firstTile)) return false;
    if (!sample_constant_clip(9000, 8400, 200, 8500, secondTile)) return false;
    return ordinary == firstTile && ordinary == secondTile;
}

bool translated_gradient_case() {
    uint8_t local = 0;
    uint8_t tiled = 0;
    if (!sample_gradient_clip(400, 8300, 200, local)) return false;
    if (!sample_gradient_clip(9000, 0, 8500, tiled)) return false;
    const int difference = static_cast<int>(local) - static_cast<int>(tiled);
    return local > 0 && local < 255 && difference >= -1 && difference <= 1;
}

bool reused_clip_destination_case() {
    SkRasterClip source(SkIRect::MakeWH(16, 2));
    SkRasterClip destination(SkIRect::MakeWH(16, 2));
    destination.op(SkShaders::Color(SkColorSetARGB(128, 0, 0, 0)));
    source.translate(1, 0, &destination);
    if (destination.clipShader()) return false;
    source.op(SkShaders::Color(SkColorSetARGB(128, 0, 0, 0)));
    source.translate(2, 0, &destination);
    return destination.clipShader() != nullptr;
}
#endif
}

int main() {
    bool valid = ordinary_case();
#ifndef STX_SKIA_REFERENCE
    valid = tiled_case() && valid;
    valid = translated_gradient_case() && valid;
    valid = reused_clip_destination_case() && valid;
#endif
    if (!valid) {
        std::fputs("tiled clip shader mismatch\n", stderr);
        return 1;
    }
    std::puts("tiled clip shader PASS");
    return 0;
}
