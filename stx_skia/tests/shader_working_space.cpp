#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkShader.h"
#include "include/core/SkSurface.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/shaders/SkShaderBase.h"
#include "src/shaders/SkColorShader.h"
#include <utility>

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
template <typename T>
concept TemporaryShaderColor = requires(T&& shader) { std::move(shader).color(); };
#ifndef STX_SKIA_REFERENCE
static_assert(!TemporaryShaderColor<SkColorShader>);
#endif
bool render(const sk_sp<SkShader>& shader, const sk_sp<SkColorSpace>& destination,
            std::array<float, 4>& pixel) {
    const SkImageInfo info = SkImageInfo::Make(1, 1, kRGBA_F32_SkColorType,
                                              kPremul_SkAlphaType, destination);
    const sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixel.data(), sizeof(pixel));
    if (!surface) return false;
    SkPaint paint{};
    paint.setShader(shader);
    paint.setBlendMode(SkBlendMode::kSrc);
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.drawPaint(paint);
    return true;
}

bool same_pixel(const std::array<float, 4>& a, const std::array<float, 4>& b) {
    for (size_t channel = 0; channel < a.size(); ++channel) {
        const float delta = a[channel] - b[channel];
        if (!std::isfinite(delta) || std::fabs(delta) > 0.000001f) return false;
    }
    return true;
}

bool inherited_input_regression() {
    const sk_sp<SkColorSpace> linear = SkColorSpace::MakeSRGBLinear();
    const SkColor4f color{0.5f, 0.25f, 0.75f, 1.0f};
    const sk_sp<SkShader> base = SkShaders::Color(color, nullptr);
    if (!base) return false;
    // Null input inherits the linear destination. Explicitly declaring that
    // same linear output must not change a shader which already produces it.
    const sk_sp<SkShader> wrapped = (*base).makeWithWorkingColorSpace(nullptr, linear);
    if (!wrapped) return false;
    std::array<float, 4> direct{};
    std::array<float, 4> adjusted{};
    if (!render(base, linear, direct) || !render(wrapped, linear, adjusted)) return false;
    std::printf("inherited input: direct red %.9g, wrapped red %.9g\n", direct[0], adjusted[0]);
#ifdef STX_SKIA_REFERENCE
    // Keep the original defect observable; do not redefine it as valid output.
    const bool reproduced = !same_pixel(direct, adjusted);
    return reproduced;
#else
    const bool correct = same_pixel(direct, adjusted);
    return correct;
#endif
}

#ifndef STX_SKIA_REFERENCE
struct ObservedStage {
    SkPoint offset{};
    bool called = false;
};

class StageObserver final : public SkShaderBase {
public:
    explicit StageObserver(ObservedStage& observation) : fObservation(observation) {}
    bool isOpaque() const override { return false; }
    // This test shader contributes no output and is never type-dispatched.
    ShaderType type() const override { return ShaderType::kEmpty; }
    Factory getFactory() const override { return nullptr; }
    const char* getTypeName() const override { return "StageObserver"; }
    bool appendStages(const SkStageRec& rec, const SkShaders::MatrixRec&) const override {
        fObservation.offset = rec.fDeviceOffset;
        fObservation.called = true;
        return false;
    }
private:
    ObservedStage& fObservation; // The test keeps this record alive until the shader is destroyed.
};

bool device_offset_regression() {
    ObservedStage observed{};
    const sk_sp<SkShader> observer = sk_make_sp<StageObserver>(observed);
    const sk_sp<SkColorSpace> srgb = SkColorSpace::MakeSRGB();
    const sk_sp<SkShader> wrapped = (*observer).makeWithWorkingColorSpace(srgb);
    SkSTArenaAlloc<2048> arena{};
    const SkSurfaceProps props{};
    SkRasterPipeline pipeline(&arena);
    const SkStageRec rec{&pipeline, &arena, kRGBA_F32_SkColorType, srgb.get(),
                         SkColors::kWhite, props, SkRect::MakeEmpty(), {3.0f, -5.0f}};
    const SkShaders::MatrixRec matrices{};
    if ((*as_SB(wrapped)).appendStages(rec, matrices) || !observed.called) return false;
    const bool preserved = observed.offset == rec.fDeviceOffset;
    return preserved;
}
#endif

bool explicit_spaces(std::vector<float>& output) {
    const std::array<sk_sp<SkColorSpace>, 3> spaces{
        SkColorSpace::MakeSRGB(), SkColorSpace::MakeSRGBLinear(),
        SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB, SkNamedGamut::kDisplayP3)};
    for (const sk_sp<SkColorSpace>& source : spaces) {
        for (const sk_sp<SkColorSpace>& input : spaces) {
            for (const sk_sp<SkColorSpace>& destination : spaces) {
                for (size_t sample = 0; sample < 8; ++sample) {
                    const SkColor4f color{static_cast<float>(sample & 1),
                                         static_cast<float>((sample >> 1) & 1) * 0.5f,
                                         static_cast<float>((sample >> 2) & 1) * 0.75f,
                                         static_cast<float>(sample % 4) / 3.0f};
                    const sk_sp<SkShader> base = SkShaders::Color(color, source);
                    if (!base) return false;
                    const sk_sp<SkShader> wrapped = (*base).makeWithWorkingColorSpace(input);
                    if (!wrapped) return false;
                    const sk_sp<SkShader> explicitOutput = (*base).makeWithWorkingColorSpace(input, input);
                    if (!explicitOutput) return false;
                    std::array<float, 4> inherited{};
                    std::array<float, 4> explicitPixel{};
                    if (!render(wrapped, destination, inherited) ||
                        !render(explicitOutput, destination, explicitPixel)) return false;
                    if (!same_pixel(inherited, explicitPixel)) return false;
                    output.insert(output.end(), inherited.begin(), inherited.end());
                }
            }
        }
    }
    return true;
}
}

int main(int argc, char** argv) {
    if (!inherited_input_regression()) return 1;
#ifndef STX_SKIA_REFERENCE
    if (!device_offset_regression()) return 5;
#endif
    std::vector<float> output{};
    if (!explicit_spaces(output)) return 2;
    if (argc == 2) {
        FILE* file = std::fopen(argv[1], "wb");
        if (file == nullptr) return 3;
        const size_t written = std::fwrite(output.data(), sizeof(float), output.size(), file);
        const int closed = std::fclose(file);
        if (written != output.size() || closed != 0) return 3;
    }
    std::printf("shader working spaces: 216 pixels, %zu floats PASS\n", output.size());
    return 0;
}
