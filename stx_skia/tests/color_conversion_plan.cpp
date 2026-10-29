#include "include/core/SkAlphaType.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurfaceProps.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkColorFilterPriv.h"
#include "src/core/SkColorSpaceXformSteps.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpContexts.h"
#include "src/core/SkRasterPipelineOpList.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace {
template <typename T>
concept TemporaryPipelinePlan = requires(T&& plan, SkRasterPipeline* pipeline) {
    std::move(plan).apply(pipeline);
};
#ifndef STX_SKIA_REFERENCE
static_assert(!TemporaryPipelinePlan<SkColorSpaceXformSteps>);
#endif

void append_bytes(std::vector<uint8_t>& output, const void* bytes, size_t length) {
    const uint8_t* first = static_cast<const uint8_t*>(bytes);
    output.insert(output.end(), first, first + length);
}

bool identity_and_alpha(std::vector<uint8_t>& output) {
    const sk_sp<SkColorSpace> srgb = SkColorSpace::MakeSRGB();
    const SkColorSpaceXformSteps identity{};
    const std::array<float, 4> original{0.125f, 0.25f, 0.5f, 0.75f};
    std::array<float, 4> color = original;
    identity.apply(color.data());
    if (identity.fFlags.mask() != 0 || color != original) return false;
    const SkColorSpaceXformSteps unpremultiply(srgb.get(), kPremul_SkAlphaType,
                                              srgb.get(), kUnpremul_SkAlphaType);
    if (unpremultiply.fFlags.mask() != 1) return false;
    const SkColorSpaceXformSteps premultiply(srgb.get(), kUnpremul_SkAlphaType,
                                            srgb.get(), kPremul_SkAlphaType);
    if (premultiply.fFlags.mask() != 16) return false;
    const SkColorSpaceXformSteps opaqueDestination(srgb.get(), kPremul_SkAlphaType,
                                                  srgb.get(), kOpaque_SkAlphaType);
    if (opaqueDestination.fFlags.mask() != 0) return false;
    const std::array<float, 8> alphas{0.0f, -0.0f, 0.125f, 1.0f,
        std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
    for (float alpha : alphas) {
        std::array<float, 4> input{0.125f, -0.25f, 0.5f, alpha};
        unpremultiply.apply(input.data());
        if (std::memcmp(&input[3], &alpha, sizeof(float)) != 0) return false;
        append_bytes(output, input.data(), sizeof(input));
    }
    color = original;
    premultiply.apply(color.data());
    const std::array<float, 4> expected{0.09375f, 0.1875f, 0.375f, 0.75f};
    if (color != expected) return false;
    return true;
}

// A plan into an HLG destination applies the inverse OOTF. Black there was NaN in the scalar
// path before it was corrected (docs/CONVERSION.md, "HLG black"), so those plans are recorded in
// color-plans-corrected.raw and the rest stay shared with the reference.
bool conversion_plans(std::vector<uint8_t>& output, std::vector<uint8_t>& corrected,
                      size_t& planCount) {
    skcms_TransferFunction hlgUnit = SkNamedTransferFn::kHLG;
    hlgUnit.c = 1.0f;
    const std::array<sk_sp<SkColorSpace>, 8> spaces{
        nullptr, SkColorSpace::MakeSRGB(), SkColorSpace::MakeSRGBLinear(),
        SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB, SkNamedGamut::kDisplayP3),
        SkColorSpace::MakeRGB(SkNamedTransferFn::kLinear, SkNamedGamut::kRec2020),
        SkColorSpace::MakeRGB(SkNamedTransferFn::kPQ, SkNamedGamut::kRec2020),
        SkColorSpace::MakeRGB(SkNamedTransferFn::kHLG, SkNamedGamut::kRec2020),
        SkColorSpace::MakeRGB(hlgUnit, SkNamedGamut::kDisplayP3)};
    for (size_t index = 1; index < spaces.size(); ++index) {
        if (!spaces[index]) return false;
    }
    const std::array<SkAlphaType, 3> types{kOpaque_SkAlphaType, kPremul_SkAlphaType,
                                        kUnpremul_SkAlphaType};
    for (const sk_sp<SkColorSpace>& src : spaces) {
        for (const sk_sp<SkColorSpace>& dst : spaces) {
            for (SkAlphaType srcAlpha : types) {
                for (SkAlphaType dstAlpha : types) {
                    // One plan feeds both a scalar operation and a tail-sized raster batch.
                    const SkColorSpaceXformSteps plan(src.get(), srcAlpha, dst.get(), dstAlpha);
                    std::vector<uint8_t>& record = plan.fFlags.dst_ootf ? corrected : output;
                    const uint32_t flags = plan.fFlags.mask();
                    append_bytes(record, &flags, sizeof(flags));
                    constexpr size_t sampleCount = 17;
                    // One flat RGBA array per buffer matches raster-stage pointer arithmetic.
                    std::array<float, sampleCount * 4> inputs{};
                    std::array<float, sampleCount * 4> scalar{};
                    std::array<float, sampleCount * 4> raster{};
                    for (size_t sample = 0; sample < sampleCount; ++sample) {
                        const float alpha = srcAlpha == kOpaque_SkAlphaType ? 1.0f
                            : static_cast<float>(sample % 5) * 0.25f;
                        const float scale = srcAlpha == kPremul_SkAlphaType ? alpha : 1.0f;
                        const size_t offset = sample * 4;
                        inputs[offset] = static_cast<float>(sample % 7) * 0.2f * scale;
                        inputs[offset + 1] = static_cast<float>((sample * 3) % 11) * 0.1f * scale;
                        inputs[offset + 2] = static_cast<float>((sample * 5) % 13) * 0.1f * scale;
                        inputs[offset + 3] = alpha;
                        for (size_t channel = 0; channel < 4; ++channel) {
                            scalar[offset + channel] = inputs[offset + channel];
                        }
                        plan.apply(scalar.data() + offset);
                        if (scalar[offset + 3] != alpha) return false;
                    }
                    SkSTArenaAlloc<2048> arena{};
                    SkRasterPipelineContexts::MemoryCtx source{inputs.data(), 17};
                    SkRasterPipelineContexts::MemoryCtx destination{raster.data(), 17};
                    SkRasterPipeline pipeline(&arena);
                    pipeline.append(SkRasterPipelineOp::load_f32, &source);
                    plan.apply(&pipeline);
                    pipeline.append(SkRasterPipelineOp::store_f32, &destination);
                    pipeline.run(0, 0, 17, 1);
                    for (size_t sample = 0; sample < sampleCount; ++sample) {
                        const size_t offset = sample * 4;
                        if (raster[offset + 3] != inputs[offset + 3]) return false;
                        // These paths use different approximations. Compare each
                        // with its own reference output, not with the other path.
                        append_bytes(record, scalar.data() + offset, 4 * sizeof(float));
                        append_bytes(record, raster.data() + offset, 4 * sizeof(float));
                    }
                    ++planCount;
                }
            }
        }
    }
    return true;
}

#ifndef STX_SKIA_REFERENCE
// The scalar inverse OOTF keeps black black and gives a finite colour for a negative luminance,
// as the raster stage does. Before the correction black encoded to 16.5 on arm64 and -15.2 on
// x86-64, from the sign bits of a NaN.
bool hlg_destination_black() {
    const sk_sp<SkColorSpace> hlg = SkColorSpace::MakeRGB(SkNamedTransferFn::kHLG,
                                                         SkNamedGamut::kRec2020);
    const std::array<sk_sp<SkColorSpace>, 3> sources{
        SkColorSpace::MakeSRGB(), SkColorSpace::MakeSRGBLinear(),
        SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB, SkNamedGamut::kDisplayP3)};
    for (const sk_sp<SkColorSpace>& source : sources) {
        const SkColorSpaceXformSteps plan(source.get(), kPremul_SkAlphaType, hlg.get(),
                                          kPremul_SkAlphaType);
        if (!plan.fFlags.dst_ootf) return false;
        std::array<float, 4> black{0.0f, 0.0f, 0.0f, 1.0f};
        plan.apply(black.data());
        if (black[0] != 0.0f || black[1] != 0.0f || black[2] != 0.0f || black[3] != 1.0f) {
            return false;
        }
        std::array<float, 4> negative{-0.25f, -0.125f, -0.0625f, 1.0f};
        plan.apply(negative.data());
        for (const float channel : negative) {
            if (!std::isfinite(channel)) return false;
        }
    }
    return true;
}
#endif

bool filter_modes(std::vector<uint8_t>& output, size_t& sampleCount) {
    const std::array<sk_sp<SkColorSpace>, 2> spaces{
        SkColorSpace::MakeSRGB(), SkColorSpace::MakeSRGBLinear()};
    for (int modeValue = 0; modeValue <= static_cast<int>(SkBlendMode::kLastMode); ++modeValue) {
        const SkBlendMode mode = static_cast<SkBlendMode>(modeValue);
        for (const sk_sp<SkColorSpace>& space : spaces) {
            for (float alpha : {0.0f, 0.375f, 1.0f}) {
                const SkColor4f source{0.25f, 0.625f, 0.875f, alpha};
                const sk_sp<SkColorFilter> filter = SkColorFilters::Blend(source, space, mode);
                const uint32_t present = filter ? 1U : 0U;
                append_bytes(output, &present, sizeof(present));
                if (mode == SkBlendMode::kDst && filter) return false;
                if (filter) {
                    SkBlendMode queriedMode = SkBlendMode::kClear;
                    SkColor queriedColor = 0;
                    if (!(*filter).asAColorMode(&queriedColor, &queriedMode)) return false;
                    append_bytes(output, &queriedColor, sizeof(queriedColor));
                    const uint32_t storedMode = static_cast<uint32_t>(queriedMode);
                    append_bytes(output, &storedMode, sizeof(storedMode));
                }
                for (size_t sample = 0; sample < 12; ++sample) {
                    const SkColor4f input{static_cast<float>(sample % 3) * 0.5f,
                                         static_cast<float>((sample * 2) % 5) * 0.25f,
                                         static_cast<float>((sample * 3) % 4) / 3.0f,
                                         static_cast<float>(sample % 4) / 3.0f};
                    const SkColor4f result = filter ? (*filter).filterColor4f(input, space.get(), space.get())
                                                    : input;
                    append_bytes(output, &result, sizeof(result));
                    ++sampleCount;
                }
            }
        }
    }
    if (SkColorFilters::Blend(SK_ColorRED, static_cast<SkBlendMode>(-1))) return false;
    if (SkColorFilters::Blend(SK_ColorRED, static_cast<SkBlendMode>(999))) return false;
    const sk_sp<SkColorFilter> gaussian = SkColorFilterPriv::MakeGaussian();
    if (!gaussian) return false;
    float previous = -1.0f;
    for (size_t sample = 0; sample <= 32; ++sample) {
        const float alpha = static_cast<float>(sample) / 32.0f;
        const SkColor4f first{0.25f, 0.5f, 1.0f, alpha};
        const SkColor4f second{1.0f, 0.125f, 0.25f, alpha};
        const SkColor4f a = (*gaussian).filterColor4f(first, nullptr, nullptr);
        const SkColor4f b = (*gaussian).filterColor4f(second, nullptr, nullptr);
        if (a != b || !std::isfinite(a.fA) || a.fA < previous) return false;
        if (std::fabs(a.fR - 1.0f) > 0.000002f || a.fR != a.fG || a.fG != a.fB) return false;
        previous = a.fA;
        append_bytes(output, &a, sizeof(a));
        ++sampleCount;
    }
    return true;
}

bool write_bytes(const char* path, const std::vector<uint8_t>& bytes) {
    FILE* file = std::fopen(path, "wb");
    if (file == nullptr) return false;
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    const int closed = std::fclose(file);
    return written == bytes.size() && closed == 0;
}
}

int main(int argc, char** argv) {
    if (argc != 1 && argc != 3) return 4;
    std::vector<uint8_t> output{};
    std::vector<uint8_t> corrected{};
    size_t plans = 0;
    size_t samples = 0;
    if (!identity_and_alpha(output)) return 1;
    if (!conversion_plans(output, corrected, plans)) return 2;
    if (!filter_modes(output, samples)) return 3;
#ifndef STX_SKIA_REFERENCE
    if (!hlg_destination_black()) return 5;
#endif
    if (argc == 3 && (!write_bytes(argv[1], output) || !write_bytes(argv[2], corrected))) return 4;
    std::printf("color plans: %zu plans, %zu filter samples, %zu bytes (%zu corrected) PASS\n", plans,
                samples, output.size(), corrected.size());
    return 0;
}
