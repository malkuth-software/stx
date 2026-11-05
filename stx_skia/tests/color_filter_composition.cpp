#include "include/core/SkAlphaType.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkShader.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSerialProcs.h"
#include "include/core/SkSurfaceProps.h"
#include "include/effects/SkColorMatrix.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkColorFilterPriv.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpContexts.h"
#include "src/core/SkRasterPipelineOpList.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"
#include "src/effects/colorfilters/SkWorkingFormatColorFilter.h"
#include "src/shaders/SkColorFilterShader.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace {
template <typename T>
concept TemporaryBaseView = requires(T&& value) { as_CFB(std::move(value)); };
#ifndef STX_SKIA_REFERENCE
static_assert(!TemporaryBaseView<sk_sp<SkColorFilter>>);
#endif

void append_color(std::vector<float>& output, const SkColor4f& color) {
    output.push_back(color.fR);
    output.push_back(color.fG);
    output.push_back(color.fB);
    output.push_back(color.fA);
}

bool close_colors(const SkColor4f& a, const SkColor4f& b) {
    const std::array<float, 4> differences{a.fR-b.fR, a.fG-b.fG, a.fB-b.fB, a.fA-b.fA};
    for (float difference : differences) {
        if (!std::isfinite(difference) || std::fabs(difference) > 0.000003f) return false;
    }
    return true;
}

bool pipeline_color(const SkColorFilter& filter, const SkColor4f& input,
                    SkColorSpace* destination, SkColor4f& output) {
    SkSTArenaAlloc<2048> arena{};
    const SkPMColor4f source = input.premul();
    SkPMColor4f result{};
    SkRasterPipelineContexts::MemoryCtx memory{&result, 0};
    const SkSurfaceProps props{};
    SkRasterPipeline pipeline(&arena);
    pipeline.appendConstantColor(&arena, source.vec());
    const SkStageRec rec{&pipeline, &arena, kRGBA_F32_SkColorType,
                         destination, input, props, SkRect::MakeEmpty()};
    const SkColorFilterBase& base = *as_CFB(&filter);
    if (!base.appendStages(rec, input.fA == 1.0f)) return false;
    pipeline.append(SkRasterPipelineOp::store_f32, &memory);
    pipeline.run(0, 0, 1, 1);
    const SkPMColor4f pinned = result.pinAlpha();
    output = pinned.unpremul();
    return true;
}

bool composition(std::vector<float>& output) {
    const sk_sp<SkColorFilter> empty{};
    if (SkColorFilters::Compose(empty, empty)) return false;
    for (int alphaChange = 0; alphaChange < 2; ++alphaChange) {
        SkColorMatrix innerMatrix{};
        innerMatrix.setScale(0.5f, 0.25f, 0.75f, alphaChange ? 0.5f : 1.0f);
        SkColorMatrix outerMatrix{};
        outerMatrix.postTranslate(0.125f, 0.25f, 0.0625f, 0.0f);
        sk_sp<SkColorFilter> inner = SkColorFilters::Matrix(innerMatrix);
        sk_sp<SkColorFilter> outer = SkColorFilters::Matrix(outerMatrix);
        if (!inner || !outer) return false;
        if ((*outer).makeComposed(nullptr).get() != outer.get()) return false;
        if (SkColorFilters::Compose(empty, inner).get() != inner.get()) return false;
        if ((*inner).makeWithWorkingColorSpace(nullptr).get() != inner.get()) return false;
        sk_sp<SkColorFilter> combined = SkColorFilters::Compose(outer, inner);
        if (!combined || (*combined).isAlphaUnchanged() != (alphaChange == 0)) return false;
        for (int sample = 0; sample < 64; ++sample) {
            const SkColor4f input{static_cast<float>(sample & 3) / 3.0f,
                                 static_cast<float>((sample >> 2) & 3) / 3.0f,
                                 static_cast<float>((sample >> 4) & 3) / 3.0f,
                                 static_cast<float>(sample % 4 + 1) * 0.25f};
            const SkColor4f intermediate = (*inner).filterColor4f(input, nullptr, nullptr);
            const SkColor4f expected = (*outer).filterColor4f(intermediate, nullptr, nullptr);
            const SkColor4f result = (*combined).filterColor4f(input, nullptr, nullptr);
            if (!close_colors(expected, result)) return false;
            append_color(output, result);
        }
        // Composition retains both filters independently of the caller's handles.
        inner.reset();
        outer.reset();
        const SkColor4f result = (*combined).filterColor4f(SkColors::kWhite, nullptr, nullptr);
        append_color(output, result);
    }
    return true;
}

bool working_spaces(std::vector<float>& output) {
    const sk_sp<SkColorSpace> srgb = SkColorSpace::MakeSRGB();
    const sk_sp<SkColorSpace> linear = SkColorSpace::MakeSRGBLinear();
    SkColorMatrix matrix{};
    matrix.setSaturation(0.375f);
    const sk_sp<SkColorFilter> child = SkColorFilters::Matrix(matrix);
    const std::array<sk_sp<SkColorSpace>, 3> spaces{nullptr, srgb, linear};
    const std::array<sk_sp<SkColorFilter>, 4> filters{
        SkColorFilters::LinearToSRGBGamma(), SkColorFilters::SRGBToLinearGamma(),
        (*child).makeWithWorkingColorSpace(srgb), (*child).makeWithWorkingColorSpace(linear)};
    for (const sk_sp<SkColorFilter>& filter : filters) {
        if (!filter || !(*filter).isAlphaUnchanged()) return false;
        for (const sk_sp<SkColorSpace>& space : spaces) {
            for (int sample = 0; sample < 64; ++sample) {
                const SkColor4f input{static_cast<float>(sample & 3) / 3.0f,
                                     static_cast<float>((sample >> 2) & 3) / 3.0f,
                                     static_cast<float>((sample >> 4) & 3) / 3.0f,
                                     static_cast<float>(sample % 5) * 0.25f};
                const SkColor4f scalar = (*filter).filterColor4f(input, space.get(), space.get());
                SkColor4f raster{};
                if (!pipeline_color(*filter, input, space.get(), raster)) return false;
                // Scalar and raster math have separate approximation paths; keep
                // both in the exact reference artifact rather than equating them.
                if (!std::isfinite(scalar.fR) || !std::isfinite(raster.fR)) return false;
                if (std::fabs(scalar.fA - input.fA) > 0.000003f) return false;
                if (std::fabs(raster.fA - input.fA) > 0.000003f) return false;
                append_color(output, scalar);
                append_color(output, raster);
            }
        }
    }
    skcms_TransferFunction linearTF{};
    skcms_Matrix3x3 gamut{};
    (*linear).transferFn(&linearTF);
    (*linear).toXYZD50(&gamut);
    const SkAlphaType unpremul = kUnpremul_SkAlphaType;
    if (SkColorFilterPriv::WithWorkingFormat(nullptr, &linearTF, &gamut, &unpremul)) return false;
    for (unsigned int mask = 0; mask < 8; ++mask) {
        const skcms_TransferFunction* tf = (mask & 1U) ? &linearTF : nullptr;
        const skcms_Matrix3x3* colors = (mask & 2U) ? &gamut : nullptr;
        const SkAlphaType* alpha = (mask & 4U) ? &unpremul : nullptr;
        SkWorkingFormatCalculator calculator(tf, colors, alpha);
        SkAlphaType actualAlpha = kUnknown_SkAlphaType;
        const sk_sp<SkColorSpace> actualSpace = calculator.workingFormat(srgb, &actualAlpha);
        if (!actualSpace || actualAlpha != (alpha ? unpremul : kPremul_SkAlphaType)) return false;
        const SkSerialProcs procs{};
        SkBinaryWriteBuffer writer(procs);
        calculator.flatten(writer);
        std::array<uint32_t, 32> storage{};
        if (writer.bytesWritten() > sizeof(storage)) return false;
        writer.writeToMemory(storage.data());
        SkReadBuffer reader(storage.data(), writer.bytesWritten());
        if (reader.readBool() != (tf == nullptr)) return false;
        if (reader.readBool() != (colors == nullptr)) return false;
        if (reader.readBool() != (alpha == nullptr)) return false;
        if (tf) {
            std::array<float, 7> decoded{};
            if (!reader.readScalarArray(decoded)) return false;
            const std::array<float, 7> expected{linearTF.g, linearTF.a, linearTF.b, linearTF.c,
                                              linearTF.d, linearTF.e, linearTF.f};
            if (decoded != expected) return false;
            output.insert(output.end(), decoded.begin(), decoded.end());
        }
        if (colors) {
            std::array<float, 9> decoded{};
            if (!reader.readScalarArray(decoded)) return false;
            for (size_t row = 0; row < 3; ++row) {
                for (size_t column = 0; column < 3; ++column) {
                    if (decoded[row * 3 + column] != gamut.vals[row][column]) return false;
                }
            }
            output.insert(output.end(), decoded.begin(), decoded.end());
        }
        if (alpha && reader.readInt() != unpremul) return false;
        if (!reader.isValid() || !reader.eof()) return false;
    }
    return true;
}

bool filtered_shaders(std::vector<float>& output) {
    const SkColor4f color{0.25f, 0.5f, 0.75f, 1.0f};
    const sk_sp<SkShader> source = SkShaders::Color(color, nullptr);
    for (bool changesAlpha : {false, true}) {
        SkColorMatrix matrix{};
        matrix.setScale(0.5f, 1.0f, 0.25f, changesAlpha ? 0.5f : 1.0f);
        const sk_sp<SkColorFilter> filter = SkColorFilters::Matrix(matrix);
        if (SkColorFilterShader::Make(nullptr, 1.0f, filter)) return false;
        if (SkColorFilterShader::Make(source, 1.0f, nullptr).get() != source.get()) return false;
        for (float alpha : {0.0f, 0.5f, 1.0f}) {
            const sk_sp<SkShader> shader = SkColorFilterShader::Make(source, alpha, filter);
            if (!shader || (*shader).isOpaque() != (alpha == 1.0f && !changesAlpha)) return false;
            SkPMColor4f pixel{};
            const SkImageInfo info = SkImageInfo::Make(1, 1, kRGBA_F32_SkColorType, kPremul_SkAlphaType);
            const sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, &pixel, sizeof(pixel));
            if (!surface) return false;
            SkPaint paint{};
            paint.setShader(shader);
            paint.setBlendMode(SkBlendMode::kSrc);
            SkCanvas& canvas = *(*surface).getCanvas();
            canvas.drawPaint(paint);
            const SkColor4f input{color.fR, color.fG, color.fB, alpha};
            const SkColor4f expected = (*filter).filterColor4f(input, nullptr, nullptr);
            const SkColor4f actual = pixel.unpremul();
            if (!close_colors(actual, expected)) return false;
            append_color(output, actual);
        }
    }
    return true;
}

struct StageTrace {
    std::array<int, 2> order{};
    std::array<bool, 2> opaque{};
    size_t count = 0;
};

class TracedFilter final : public SkColorFilterBase {
public:
    TracedFilter(StageTrace& trace, int index, bool succeeds, bool preservesAlpha)
        : fTrace(trace), fIndex(index), fSucceeds(succeeds), fPreservesAlpha(preservesAlpha) {}
    Factory getFactory() const override { return nullptr; }
    const char* getTypeName() const override { return "TracedFilter"; }
    Type type() const override { return Type::kNoop; }
    bool onIsAlphaUnchanged() const override { return fPreservesAlpha; }
    bool appendStages(const SkStageRec&, bool opaque) const override {
        if (fTrace.count >= fTrace.order.size()) return false;
        fTrace.order[fTrace.count] = fIndex;
        fTrace.opaque[fTrace.count] = opaque;
        ++fTrace.count;
        return fSucceeds;
    }
private:
    StageTrace& fTrace; // Test-local trace outlives filters and their composed owner.
    const int fIndex;
    const bool fSucceeds;
    const bool fPreservesAlpha;
};

bool stage_order() {
    for (bool innerSucceeds : {false, true}) {
        for (bool outerSucceeds : {false, true}) {
            for (bool preservesAlpha : {false, true}) {
                StageTrace trace{};
                sk_sp<SkColorFilter> inner = sk_make_sp<TracedFilter>(trace, 1, innerSucceeds, preservesAlpha);
                sk_sp<SkColorFilter> outer = sk_make_sp<TracedFilter>(trace, 2, outerSucceeds, true);
                const sk_sp<SkColorFilter> composed = SkColorFilters::Compose(outer, inner);
                SkSTArenaAlloc<256> arena{};
                const SkSurfaceProps props{};
                SkRasterPipeline pipeline(&arena);
                const SkStageRec rec{&pipeline, &arena, kRGBA_F32_SkColorType, nullptr,
                                     SkColors::kWhite, props, SkRect::MakeEmpty()};
                const bool result = (*as_CFB(composed)).appendStages(rec, true);
                if (result != (innerSucceeds && outerSucceeds)) return false;
                if (trace.count != (innerSucceeds ? 2U : 1U)) return false;
                if (trace.order[0] != 1 || !trace.opaque[0]) return false;
                if (innerSucceeds && (trace.order[1] != 2 || trace.opaque[1] != preservesAlpha)) return false;
            }
        }
    }
    return true;
}
}

int main(int argc, char** argv) {
    std::vector<float> output{};
    if (!composition(output)) return 1;
    if (!working_spaces(output)) return 2;
    if (!stage_order()) return 3;
    if (!filtered_shaders(output)) return 5;
    if (argc == 2) {
        FILE* file = std::fopen(argv[1], "wb");
        if (file == nullptr) return 4;
        const size_t written = std::fwrite(output.data(), sizeof(float), output.size(), file);
        const int closed = std::fclose(file);
        if (written != output.size() || closed != 0) return 4;
    }
    std::printf("color composition/working spaces: %zu floats PASS\n", output.size());
    return 0;
}
