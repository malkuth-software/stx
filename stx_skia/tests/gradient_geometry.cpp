#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkGradient.h"
#include "src/shaders/gradients/SkConicalGradient.h"
#include "src/shaders/gradients/SkLinearGradient.h"
#include "src/shaders/gradients/SkRadialGradient.h"
#include "src/shaders/gradients/SkSweepGradient.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

namespace {
template <typename T>
concept TemporaryCenter = requires(T& value) { std::move(value).center(); };
template <typename T>
concept TemporaryStart = requires(T& value) { std::move(value).start(); };
template <typename T>
concept TemporaryFocal = requires(T& value) { std::move(value).getFocalData(); };
template <typename T>
concept TemporaryStops = requires(T& value) { std::move(value).colors(); };
template <typename T>
concept TemporaryGradientScope = requires(T& value, SkReadBuffer& buffer, SkMatrix& matrix) {
    std::move(value).unflatten(buffer, &matrix);
};
template <typename T>
concept HasGradientBitmapCache = requires(T& value) { value.cachedBitmap(); };
#ifndef STX_SKIA_REFERENCE
static_assert(!TemporaryCenter<SkRadialGradient>);
static_assert(!TemporaryCenter<SkSweepGradient>);
static_assert(!TemporaryStart<SkLinearGradient>);
static_assert(!TemporaryFocal<SkConicalGradient>);
static_assert(!TemporaryStops<SkGradientBaseShader>);
static_assert(!TemporaryGradientScope<SkGradientScope>);
static_assert(!HasGradientBitmapCache<SkGradientBaseShader>);
#endif

constexpr int width = 33;
constexpr int height = 23;
using Pixels = std::array<uint32_t, width * height>;
using Interpolation = SkGradient::Interpolation;

enum class Kind { linear, radial, sweep, conical };
struct Geometry {
    Kind kind = Kind::linear;
    SkPoint start{6, 7};
    SkPoint end{22, 7};
    float startRadius = 0;
    float endRadius = 12;
    float startAngle = 0;
    float endAngle = 360;
};

sk_sp<SkShader> make_shader(const Geometry& geometry, const SkGradient& gradient,
                           const SkMatrix* matrix) {
    sk_sp<SkShader> shader{};
    switch (geometry.kind) {
        case Kind::linear: {
            const SkPoint points[2]{geometry.start, geometry.end};
            shader = SkShaders::LinearGradient(points, gradient, matrix);
            break;
        }
        case Kind::radial:
            shader = SkShaders::RadialGradient(geometry.start, geometry.endRadius, gradient, matrix);
            break;
        case Kind::sweep:
            shader = SkShaders::SweepGradient(geometry.start, geometry.startAngle,
                                               geometry.endAngle, gradient, matrix);
            break;
        case Kind::conical:
            shader = SkShaders::TwoPointConicalGradient(geometry.start, geometry.startRadius,
                                                       geometry.end, geometry.endRadius,
                                                       gradient, matrix);
            break;
        default:
            return nullptr;
    }
    return shader;
}

bool render(const sk_sp<SkShader>& shader, bool linearDestination, Pixels& pixels) {
    pixels.fill(0xff192b47U);
    sk_sp<SkColorSpace> space = linearDestination ? SkColorSpace::MakeSRGBLinear()
                                                 : SkColorSpace::MakeSRGB();
    const SkImageInfo info = SkImageInfo::MakeN32Premul(width, height, std::move(space));
    const sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels.data(), width * sizeof(uint32_t));
    if (!surface) return false;
    SkPaint paint{};
    paint.setShader(shader);
    paint.setBlendMode(SkBlendMode::kSrc);
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.drawPaint(paint);
    return true;
}

bool factory_contract() {
    const SkColor4f stops[2]{{0.1f, 0.6f, 0.2f, 1}, {0.9f, 0.2f, 0.7f, 0.5f}};
    const SkGradient::Colors colors(stops, SkTileMode::kClamp);
    const SkGradient gradient(colors, {});
    const SkGradient::Colors oneColor(SkSpan<const SkColor4f>(stops, 1), SkTileMode::kClamp);
    const SkGradient single(oneColor, {});
    const SkGradient empty{};
    const SkMatrix singular = SkMatrix::Scale(0, 1);
    const std::array<Kind, 4> kinds{Kind::linear, Kind::radial, Kind::sweep, Kind::conical};
    for (const Kind kind : kinds) {
        const Geometry geometry{.kind = kind};
        if (make_shader(geometry, empty, nullptr)) return false;
        if (make_shader(geometry, gradient, &singular)) return false;
        const sk_sp<SkShader> solid = make_shader(geometry, single, &singular);
        // Linear/radial/sweep simplify one color before matrix inversion; conical does not.
        if (static_cast<bool>(solid) != (kind != Kind::conical)) return false;
    }
    if (SkShaders::LinearGradient(nullptr, gradient)) return false;
    if (SkShaders::RadialGradient({0, 0}, -1, gradient)) return false;
    if (SkShaders::TwoPointConicalGradient({0, 0}, -1, {1, 1}, 2, gradient)) return false;
    if (SkShaders::SweepGradient({0, 0}, 40, 20, gradient)) return false;
    const float infinite = std::numeric_limits<float>::infinity();
    if (SkShaders::SweepGradient({0, 0}, 0, infinite, gradient)) return false;
    const SkPoint infinitePoints[2]{{0, 0}, {infinite, 0}};
    if (SkShaders::LinearGradient(infinitePoints, gradient)) return false;
    return true;
}

bool corpus(std::vector<uint32_t>& output) {
    const std::array<Geometry, 19> geometries{{
        {.kind = Kind::linear},
        {.kind = Kind::linear, .end = {6, 7}},
        {.kind = Kind::linear, .end = {6.00001f, 7}},
        {.kind = Kind::radial},
        {.kind = Kind::radial, .endRadius = 0},
        {.kind = Kind::sweep},
        {.kind = Kind::sweep, .startAngle = -40, .endAngle = 260},
        {.kind = Kind::sweep, .startAngle = 90, .endAngle = 90},
        {.kind = Kind::sweep, .startAngle = 0, .endAngle = 0},
        {.kind = Kind::conical, .end = {6, 7}, .startRadius = 4, .endRadius = 12},
        {.kind = Kind::conical, .end = {6, 7}, .startRadius = 12, .endRadius = 4},
        {.kind = Kind::conical, .end = {6, 7}, .endRadius = 12},
        {.kind = Kind::conical, .startRadius = 8, .endRadius = 8},
        {.kind = Kind::conical, .endRadius = 16},
        {.kind = Kind::conical, .endRadius = 24},
        {.kind = Kind::conical, .endRadius = 8},
        {.kind = Kind::conical, .startRadius = 12, .endRadius = 0},
        {.kind = Kind::conical, .startRadius = 12, .endRadius = 5},
        {.kind = Kind::conical, .end = {6, 7}, .startRadius = 8, .endRadius = 8},
    }};
    const std::array<SkTileMode, 4> modes{SkTileMode::kClamp, SkTileMode::kRepeat,
                                         SkTileMode::kMirror, SkTileMode::kDecal};
    SkMatrix affine = SkMatrix::Scale(0.75f, 1.25f);
    affine.postSkew(0.125f, -0.0625f);
    affine.postTranslate(1.5f, -0.25f);
    SkMatrix perspective{};
    perspective.setPerspX(0.015625f);
    perspective.setPerspY(-0.0078125f);
    const std::array<SkMatrix, 3> matrices{SkMatrix::I(), affine, perspective};
    const SkColor4f stops[4]{{0.1f, 0.4f, 0.8f, 1}, {0.9f, 0.2f, 0.3f, 0.375f},
                             {0.2f, 0.85f, 0.15f, 0}, {0.7f, 0.1f, 0.8f, 0.875f}};
    const SkColor4f manyStops[9]{{0.2f, 0.2f, 0.2f, 1}, {0.8f, 0.1f, 0.3f, 0.5f},
                                 {0.8f, 0.8f, 0.8f, 0.75f}, {0.3f, 0.7f, 0.1f, 0},
                                 {0, 0, 0, 1}, {0.1f, 0.2f, 0.9f, 0.625f},
                                 {1, 1, 1, 0.5f}, {0.7f, 0.2f, 0.5f, 1}, {0.4f, 0.4f, 0.4f, 1}};
    const float manyPositions[9]{0, 0, 0, 0.25f, 0.5f, 0.5f, 0.75f, 1, 1};
    const float explicitPositions[4]{0.125f, 0.5f, 0.5f, 0.875f};
    for (const Geometry& geometry : geometries) {
        for (const SkTileMode mode : modes) {
            for (int pattern = 0; pattern < 5; ++pattern) {
                const size_t count = pattern == 0 ? 1 : pattern == 3 ? 2 : 4;
                SkSpan<const SkColor4f> colorSpan(stops, count);
                SkSpan<const float> positions{};
                if (pattern == 2) positions = SkSpan<const float>(explicitPositions);
                if (pattern == 4) {
                    colorSpan = SkSpan<const SkColor4f>(manyStops);
                    positions = SkSpan<const float>(manyPositions);
                }
                const SkGradient::Colors colors(colorSpan, positions, mode);
                for (int colorSpace = 0; colorSpace < Interpolation::kColorSpaceCount; ++colorSpace) {
                    const Interpolation::ColorSpace space = static_cast<Interpolation::ColorSpace>(colorSpace);
                    for (int premul = 0; premul < 2; ++premul) {
                        const Interpolation interpolation{
                            .fInPremul = premul ? Interpolation::InPremul::kYes
                                              : Interpolation::InPremul::kNo,
                            .fColorSpace = space,
                            .fHueMethod = static_cast<Interpolation::HueMethod>(mode)};
                        const SkGradient gradient(colors, interpolation);
                        for (size_t transform = 0; transform < matrices.size(); ++transform) {
                            const sk_sp<SkShader> shader = make_shader(geometry, gradient,
                                                                      &matrices[transform]);
                            if (!shader) return false;
                            Pixels pixels{};
                            if (!render(shader, transform == 1, pixels)) return false;
                            output.insert(output.end(), pixels.begin(), pixels.end());
                        }
                    }
                }
            }
        }
    }
    return true;
}
}

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
    if (!factory_contract()) return 1;
    std::vector<uint32_t> pixels{};
    if (!corpus(pixels)) return 1;
    if (argc == 2) {
        FILE* file = std::fopen(argv[1], "wb");
        if (file == nullptr) return 2;
        const size_t written = std::fwrite(pixels.data(), sizeof(uint32_t), pixels.size(), file);
        const int closed = std::fclose(file);
        if (written != pixels.size() || closed != 0) return 2;
    }
    std::printf("gradient geometry: %zu renders, %zu bytes PASS\n",
                pixels.size() / Pixels{}.size(), pixels.size() * sizeof(uint32_t));
    return 0;
}
