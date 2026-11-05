#include "include/core/SkBlender.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkShader.h"
#include "include/core/SkSurface.h"
#include "include/core/SkVertices.h"
#include "include/effects/SkGradient.h"
#include "gradient_result.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

template <typename T>
concept HasProgrammableMesh = requires { &T::drawMesh; };
template <typename T>
concept HasShaderCapabilities = requires { &T::capabilities; };
template <typename T>
concept HasRuntimeLerp = requires { T::Lerp(0.5f, nullptr, nullptr); };
#ifdef STX_SKIA_REFERENCE
static_assert(HasProgrammableMesh<SkCanvas>);
static_assert(HasShaderCapabilities<SkSurface>);
static_assert(HasRuntimeLerp<SkColorFilters>);
#else
static_assert(!HasProgrammableMesh<SkCanvas>);
static_assert(!HasShaderCapabilities<SkSurface>);
static_assert(!HasRuntimeLerp<SkColorFilters>);
#if __has_include("include/effects/SkRuntimeEffect.h")
#error Runtime shader compiler headers must not be exposed by the candidate.
#endif
#endif

namespace {
struct CloseFile {
    void operator()(std::FILE* file) const { if (file != nullptr) std::fclose(file); }
};
constexpr int width = 47;
constexpr int height = 39;
using Pixels = std::array<uint32_t, width * height>;

sk_sp<SkShader> gradient(bool source, bool opaque) {
    const std::array<SkPoint, 2> points = source
        ? std::array<SkPoint, 2>{{{1.5f, 4.0f}, {31.0f, 38.0f}}}
        : std::array<SkPoint, 2>{{{5.0f, 33.5f}, {44.0f, 2.0f}}};
    const SkColor alpha = opaque ? 0xff000000U : 0x68000000U;
    const std::array<SkColor, 3> colors = source
        ? std::array<SkColor, 3>{{alpha | 0x00c43119U, 0xff20c5e4U, 0x00000000U}}
        : std::array<SkColor, 3>{{0xff3918d3U, alpha | 0x0099a522U, 0x400302e8U}};
    const std::array<SkScalar, 3> stops{{0.0f, 0.35f, 1.0f}};
    SkMatrix local;
    local.setSkew(source ? 0.125f : -0.25f, 0.0625f);
    std::array<SkColor4f, 3> rgba{};
    for (size_t index = 0; index < colors.size(); ++index) rgba[index] = SkColor4f::FromColor(colors[index]);
    const SkGradient::Colors gradientColors(SkSpan<const SkColor4f>(rgba.data(), rgba.size()),
                                           SkSpan<const SkScalar>(stops.data(), stops.size()), SkTileMode::kMirror);
    const SkGradient description(gradientColors, SkGradient::Interpolation{});
    sk_sp<SkShader> base = shader_of(SkShaders::LinearGradient(points.data(), description));
    if (!base) return nullptr;
    sk_sp<SkShader> shader = (*base).makeWithLocalMatrix(local);
    return shader;
}

bool draw(Pixels& pixels, SkBlendMode mode, int alphaCase, int transform, bool wrapped, bool vertices) {
    const SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels.data(), width * sizeof(uint32_t));
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.clear(0x60203040U);
    canvas.translate(2.25f, -1.5f);
    if (transform == 1) canvas.skew(0.25f, -0.125f);
    if (transform == 2) canvas.scale(0.75f, 1.25f);
    sk_sp<SkShader> source = gradient(true, (alphaCase & 1) != 0);
    sk_sp<SkShader> destination = gradient(false, (alphaCase & 2) != 0);
    if (!source || !destination) return false;
    SkPaint paint;
    paint.setAntiAlias(true);
    if (vertices) {
        const std::array<SkPoint, 4> positions{{{0.0f, 0.0f}, {42.0f, 2.0f}, {40.0f, 35.0f}, {1.0f, 32.0f}}};
        const std::array<SkPoint, 4> coordinates{{{2.0f, 5.0f}, {21.0f, 0.0f}, {37.0f, 28.0f}, {3.0f, 31.0f}}};
        const std::array<SkColor, 4> colors{{0xff3261e8U, 0x803a90cbU, 0xffeac727U, 0x206c05dbU}};
        sk_sp<SkVertices> mesh = SkVertices::MakeCopy(SkVertices::kTriangleFan_VertexMode, 4,
                                                     positions.data(), coordinates.data(), colors.data());
        if (!mesh) return false;
        paint.setShader(std::move(source));
        canvas.drawVertices(mesh, mode, paint);
    } else {
        sk_sp<SkShader> shader;
        if (wrapped) shader = SkShaders::Blend(SkBlender::Mode(mode), std::move(destination), std::move(source));
        else shader = SkShaders::Blend(mode, std::move(destination), std::move(source));
        if (!shader) return false;
        paint.setShader(std::move(shader));
        canvas.drawPaint(paint);
    }
    return true;
}

bool opacity() {
    for (int modeIndex = 0; modeIndex <= static_cast<int>(SkBlendMode::kLastMode); ++modeIndex) {
        const SkBlendMode mode = static_cast<SkBlendMode>(modeIndex);
        for (int alpha = 0; alpha < 4; ++alpha) {
            const SkColor sourceColor = (alpha & 1) != 0 ? 0xff345678U : 0x60345678U;
            const SkColor destinationColor = (alpha & 2) != 0 ? 0xffb4c21eU : 0x80b4c21eU;
            sk_sp<SkShader> shader = SkShaders::Blend(mode, SkShaders::Color(destinationColor), SkShaders::Color(sourceColor));
            if (!shader) return false;
            if (!(*shader).isOpaque()) continue;
            Pixels pixels{};
            const SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
            sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels.data(), width * sizeof(uint32_t));
            if (!surface) return false;
            SkPaint paint;
            paint.setShader(std::move(shader));
            paint.setBlendMode(SkBlendMode::kSrc);
            SkCanvas& canvas = *(*surface).getCanvas();
            canvas.drawPaint(paint);
            for (uint32_t pixel : pixels) if ((pixel >> 24) != 255) return false;
        }
    }
    return true;
}
}

int main(int argc, char** argv) {
    if (!opacity()) return 1;
#ifndef STX_SKIA_REFERENCE
    sk_sp<SkShader> invalid = SkShaders::Blend(static_cast<SkBlendMode>(-1), SkShaders::Color(SK_ColorRED), SkShaders::Color(SK_ColorBLUE));
    if (invalid) return 1;
#endif
    std::unique_ptr<std::FILE, CloseFile> output;
    if (argc > 1) {
        output.reset(std::fopen(argv[1], "wb"));
        if (!output) return 1;
    }
    uint64_t hash = 1469598103934665603ULL;
    size_t cases = 0;
    for (int modeIndex = 0; modeIndex <= static_cast<int>(SkBlendMode::kLastMode); ++modeIndex) {
        const SkBlendMode mode = static_cast<SkBlendMode>(modeIndex);
        for (int alpha = 0; alpha < 4; ++alpha) {
            for (int transform = 0; transform < 3; ++transform) {
                Pixels direct{};
                Pixels wrapped{};
                if (!draw(direct, mode, alpha, transform, false, false)) return 1;
                if (!draw(wrapped, mode, alpha, transform, true, false) || direct != wrapped) return 1;
                for (int vertexCase = 0; vertexCase < 2; ++vertexCase) {
                    if (vertexCase != 0 && !draw(direct, mode, alpha, transform, false, true)) return 1;
                    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(direct.data());
                    for (size_t i = 0; i < sizeof(direct); ++i) { hash ^= bytes[i]; hash *= 1099511628211ULL; }
                    if (output && std::fwrite(bytes, 1, sizeof(direct), output.get()) != sizeof(direct)) return 1;
                    ++cases;
                }
            }
        }
    }
    if (output && std::fflush(output.get()) != 0) return 1;
    std::printf("shader blends: %zu scenes hash=%llu PASS\n", cases, static_cast<unsigned long long>(hash));
    return 0;
}
