// Differential corpus for SkRasterPipeline construction and execution. It records every op name;
// loads, destination loads and stores for every color type, run directly and compiled, in lowp
// and forced highp, over row extents that exercise tail patching of memory contexts; constant
// colors, set-RGB, matrix and transfer-function stage selection; extension with shared memory
// contexts; and the emboss stage's two memory contexts. Both builds must produce identical bytes.

#include "differential_corpus.h"

#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMatrix.h"
#include "modules/skcms/skcms.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpContexts.h"
#ifndef STX_SKIA_REFERENCE
#include "src/core/SkRasterPipelineOpContextTypes.h"
#endif
#include "src/core/SkRasterPipelineOpList.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

extern bool gForceHighPrecisionRasterPipeline;

#ifndef STX_SKIA_REFERENCE
// run() repoints memory contexts for row tails, so the load and store helpers refuse const ones.
template <typename Context>
concept LoadsFrom = requires(SkRasterPipeline& pipeline, Context context) {
    pipeline.appendLoad(kRGBA_8888_SkColorType, context);
};
template <typename Context>
concept StoresTo = requires(SkRasterPipeline& pipeline, Context context) {
    pipeline.appendStore(kRGBA_8888_SkColorType, context);
};
template <typename Context>
concept LoadsDstFrom = requires(SkRasterPipeline& pipeline, Context context) {
    pipeline.appendLoadDst(kRGBA_8888_SkColorType, context);
};
static_assert(LoadsFrom<SkRasterPipelineContexts::MemoryCtx*>);
static_assert(!LoadsFrom<const SkRasterPipelineContexts::MemoryCtx*>);
static_assert(StoresTo<SkRasterPipelineContexts::MemoryCtx*>);
static_assert(!StoresTo<const SkRasterPipelineContexts::MemoryCtx*>);
static_assert(LoadsDstFrom<SkRasterPipelineContexts::MemoryCtx*>);
static_assert(!LoadsDstFrom<const SkRasterPipelineContexts::MemoryCtx*>);

// Each precision's stage kernel receives exactly the table's pointer type: NoCtx for None,
// void* for Immediate, and the listed object, read through const for memory contexts.
template <SkRasterPipelineOp op, bool kHighp, typename Parameter>
constexpr bool stage_receives() {
    using Pointer = SkRasterPipelineStageContextPointer<SkRasterPipelineOpContext<op>, kHighp>;
    const bool receives = std::is_same_v<Pointer, Parameter>;
    return receives;
}
using MemoryCtx = SkRasterPipelineContexts::MemoryCtx;
using DecalTileCtx = SkRasterPipelineContexts::DecalTileCtx;
using CoordClampCtx = SkRasterPipelineContexts::CoordClampCtx;
static_assert(stage_receives<SkRasterPipelineOp::load_8888, true, const MemoryCtx*>());
static_assert(stage_receives<SkRasterPipelineOp::load_8888, false, const MemoryCtx*>());
static_assert(!stage_receives<SkRasterPipelineOp::load_8888, true, MemoryCtx*>());
static_assert(std::is_same_v<SkRasterPipelineOpContext<SkRasterPipelineOp::load_8888>::AppendObject,
                             MemoryCtx>);
static_assert(stage_receives<SkRasterPipelineOp::matrix_2x3, true, const float*>());
static_assert(!stage_receives<SkRasterPipelineOp::matrix_2x3, true, float*>());
static_assert(stage_receives<SkRasterPipelineOp::decal_x, false, DecalTileCtx*>());
static_assert(stage_receives<SkRasterPipelineOp::clamp_x_and_y, false, const CoordClampCtx*>());
static_assert(stage_receives<SkRasterPipelineOp::srcover, true, const void*>());
static_assert(stage_receives<SkRasterPipelineOp::swizzle, true, void*>());
static_assert(stage_receives<SkRasterPipelineOp::load_src, true, const float*>());
static_assert(stage_receives<SkRasterPipelineOp::load_src, false, const uint16_t*>());
static_assert(stage_receives<SkRasterPipelineOp::store_src, false, uint16_t*>());
static_assert(stage_receives<SkRasterPipelineOp::gamma_, true, const skcms_TransferFunction*>());
#endif

namespace {

using corpus::Output;
using corpus::append;
using corpus::append_bool;

constexpr int kWidth = 41;
constexpr int kRows = 3;
constexpr size_t kMaxBytesPerPixel = 16;

void record_bytes(Output& output, const std::vector<uint8_t>& bytes) {
    output.bytes.insert(output.bytes.end(), bytes.begin(), bytes.end());
}

void record_stage_names(Output& output, const SkRasterPipeline& pipeline) {
    append(output, pipeline.getNumStages());
    for (const SkRasterPipeline::StageList* stage = pipeline.getStageList(); stage != nullptr;
         stage = (*stage).prev) {
        const char* name = SkRasterPipeline::GetOpName((*stage).stage);
        output.bytes.insert(output.bytes.end(), name, name + std::strlen(name) + 1);
    }
}

// Op indices differ between the builds (the candidate removed compiler-only ops), so the shared
// corpus names a fixed list of retained ops. The candidate separately checks every index.
void op_names(Output& output) {
    const std::array<SkRasterPipelineOp, 16> ops{
            SkRasterPipelineOp::load_8888, SkRasterPipelineOp::load_8888_dst,
            SkRasterPipelineOp::store_8888, SkRasterPipelineOp::srcover,
            SkRasterPipelineOp::srcover_rgba_8888, SkRasterPipelineOp::seed_shader,
            SkRasterPipelineOp::matrix_perspective, SkRasterPipelineOp::uniform_color,
            SkRasterPipelineOp::unbounded_set_rgb, SkRasterPipelineOp::parametric,
            SkRasterPipelineOp::HLGinvish, SkRasterPipelineOp::emboss,
            SkRasterPipelineOp::swizzle, SkRasterPipelineOp::bilerp_clamp_8888,
            SkRasterPipelineOp::store_f32, SkRasterPipelineOp::debug_a_255};
    for (const SkRasterPipelineOp op : ops) {
        const char* name = SkRasterPipeline::GetOpName(op);
        output.bytes.insert(output.bytes.end(), name, name + std::strlen(name) + 1);
    }
    ++output.cases;
}

#ifndef STX_SKIA_REFERENCE
// Every op index has a distinct, non-empty name, and lowp ops are a prefix of all ops.
bool every_op_named() {
    std::set<std::string> names;
    for (int index = 0; index < kNumRasterPipelineHighpOps; ++index) {
        const char* name = SkRasterPipeline::GetOpName(static_cast<SkRasterPipelineOp>(index));
        if (name[0] == '\0' || !names.insert(name).second) return false;
    }
    const bool prefix = kNumRasterPipelineLowpOps > 0 &&
                        kNumRasterPipelineLowpOps <= kNumRasterPipelineHighpOps;
    return prefix;
}
#endif

std::vector<uint8_t> pattern_pixels(size_t bytes, uint32_t seed) {
    std::vector<uint8_t> pixels(bytes, 0);
    for (size_t i = 0; i < bytes; ++i) {
        pixels[i] = static_cast<uint8_t>(seed * 131u + static_cast<uint32_t>(i) * 29u + 7u);
    }
    return pixels;
}

// Floating-point formats load arbitrary bit patterns as NaNs and infinities, which stores may
// canonicalize differently between equivalent kernels. Give them finite values in [0, 1].
void make_finite(std::vector<uint8_t>& pixels, SkColorType colorType) {
    if (colorType == kRGBA_F32_SkColorType) {
        for (size_t i = 0; i + 4 <= pixels.size(); i += 4) {
            const float value = static_cast<float>(pixels[i]) / 255.0f;
            std::memcpy(&pixels[i], &value, sizeof(value));
        }
    } else if (colorType == kRGBA_F16_SkColorType || colorType == kRGBA_F16Norm_SkColorType ||
               colorType == kRGB_F16F16F16x_SkColorType || colorType == kA16_float_SkColorType ||
               colorType == kR16_float_SkColorType || colorType == kR16G16_float_SkColorType) {
        // Half-precision values with exponent 0b01110 and below lie in [0, 1).
        for (size_t i = 0; i + 2 <= pixels.size(); i += 2) {
            pixels[i + 1] = static_cast<uint8_t>(pixels[i + 1] & 0x3b);
        }
    }
}

// Converts every row of src to dst through float pixels. The load/store pair runs both lowp
// (where the format allows) and highp, directly and compiled.
void conversion_case(Output& output, SkColorType colorType, size_t x, size_t width, bool compile) {
    const int bytesPerPixel = SkColorTypeBytesPerPixel(colorType);
    const size_t rowBytes = static_cast<size_t>(kWidth) * kMaxBytesPerPixel;
    std::vector<uint8_t> source = pattern_pixels(rowBytes * kRows, static_cast<uint32_t>(colorType));
    make_finite(source, colorType);
    std::vector<uint8_t> destination = pattern_pixels(rowBytes * kRows, 99);
    make_finite(destination, colorType);
    const int stride = static_cast<int>(rowBytes) / bytesPerPixel;
    SkRasterPipelineContexts::MemoryCtx sourceCtx{source.data(), stride};
    SkRasterPipelineContexts::MemoryCtx destinationCtx{destination.data(), stride};
    SkSTArenaAlloc<512> arena;
    SkRasterPipeline pipeline(&arena);
    pipeline.appendLoad(colorType, &sourceCtx);
    pipeline.appendLoadDst(colorType, &destinationCtx);
    pipeline.append(SkRasterPipelineOp::srcover);
    pipeline.appendStore(colorType, &destinationCtx);
    record_stage_names(output, pipeline);
    if (compile) {
        const std::function<void(size_t, size_t, size_t, size_t)> run = pipeline.compile();
        run(x, 1, width, 2);
    } else {
        pipeline.run(x, 1, width, 2);
    }
    record_bytes(output, destination);
    // The memory contexts are restored after tail patching.
    append_bool(output, sourceCtx.pixels == source.data() && destinationCtx.pixels == destination.data());
    ++output.cases;
}

void conversion_corpus(Output& output) {
    const std::array<size_t, 4> origins{0, 1, 5, 17};
    const std::array<size_t, 6> widths{0, 1, 3, 8, 13, 24};
    for (int highp = 0; highp < 2; ++highp) {
        gForceHighPrecisionRasterPipeline = highp != 0;
        for (int type = 1; type <= kLastEnum_SkColorType; ++type) {
            const SkColorType colorType = static_cast<SkColorType>(type);
            for (const size_t x : origins) {
                for (const size_t width : widths) {
                    for (int compile = 0; compile < 2; ++compile) {
                        conversion_case(output, colorType, x, width, compile != 0);
                    }
                }
            }
        }
    }
    gForceHighPrecisionRasterPipeline = false;
}

// Runs a pipeline that ends in a float store over one row and records the stored floats.
void run_to_floats(Output& output, SkRasterPipeline& pipeline, size_t width) {
    std::vector<float> pixels(static_cast<size_t>(kWidth) * 4, -7.0f);
    SkRasterPipelineContexts::MemoryCtx ctx{pixels.data(), kWidth};
    pipeline.append(SkRasterPipelineOp::store_f32, &ctx);
    record_stage_names(output, pipeline);
    pipeline.run(0, 0, width, 1);
    for (const float value : pixels) {
        append(output, value);
    }
    ++output.cases;
}

void constant_corpus(Output& output) {
    const std::array<std::array<float, 4>, 7> colors{{{0, 0, 0, 1},
                                                      {1, 1, 1, 1},
                                                      {0.25f, 0.5f, 0.125f, 0.75f},
                                                      {0.8f, 0.2f, 0.4f, 0.5f},
                                                      {1.5f, -0.25f, 0.5f, 1},
                                                      {0.3f, 0.3f, 0.3f, 0},
                                                      {0.999f, 0.001f, 0.5f, 1}}};
    for (const std::array<float, 4>& color : colors) {
        SkSTArenaAlloc<256> arena;
        SkRasterPipeline pipeline(&arena);
        pipeline.appendConstantColor(&arena, color.data());
        run_to_floats(output, pipeline, 9);
    }
    const std::array<std::array<float, 3>, 4> rgbs{{{0, 0.5f, 1}, {1.25f, 0, 0}, {-0.5f, 0.5f, 0.5f},
                                                    {0.2f, 0.4f, 0.6f}}};
    for (const std::array<float, 3>& rgb : rgbs) {
        SkSTArenaAlloc<256> arena;
        SkRasterPipeline pipeline(&arena);
        pipeline.appendConstantColor(&arena, colors[2].data());
        pipeline.appendSetRGB(&arena, rgb.data());
        run_to_floats(output, pipeline, 6);
    }
}

void matrix_corpus(Output& output) {
    SkMatrix perspective = SkMatrix::Translate(3, 4);
    perspective.setPerspX(0.01f);
    perspective.setPerspY(-0.02f);
    const std::array<SkMatrix, 6> matrices{SkMatrix::I(),
                                           SkMatrix::Translate(2.5f, -1),
                                           SkMatrix::Scale(1.5f, 0.5f),
                                           SkMatrix::MakeAll(1.5f, 0, 3, 0, 0.75f, -2, 0, 0, 1),
                                           SkMatrix::RotateDeg(30, {4, 2}),
                                           perspective};
    for (const SkMatrix& matrix : matrices) {
        SkSTArenaAlloc<256> arena;
        SkRasterPipeline pipeline(&arena);
        pipeline.append(SkRasterPipelineOp::seed_shader);
        pipeline.appendMatrix(&arena, matrix);
        run_to_floats(output, pipeline, 11);
    }
}

void transfer_corpus(Output& output) {
    skcms_TransferFunction gamma{2.2f, 1, 0, 0, 0, 0, 0};
    skcms_TransferFunction pq{};
    skcms_TransferFunction_makePQish(&pq, -107/128.0f, 1.0f, 32/2523.0f, 2413/128.0f,
                                     -2392/128.0f, 8192/1305.0f);
    skcms_TransferFunction hlg{};
    skcms_TransferFunction_makeHLGish(&hlg, 2.0f, 2.0f, 1/0.17883277f, 0.28466892f, 0.55991073f);
    skcms_TransferFunction hlgInverse{};
    skcms_TransferFunction_invert(&hlg, &hlgInverse);
    const std::array<const skcms_TransferFunction*, 6> functions{
            skcms_sRGB_TransferFunction(), skcms_sRGB_Inverse_TransferFunction(), &gamma, &pq,
            &hlg, &hlgInverse};
    for (const skcms_TransferFunction* function : functions) {
        SkSTArenaAlloc<256> arena;
        SkRasterPipeline pipeline(&arena);
        const std::array<float, 4> color{0.2f, 0.6f, 0.9f, 1};
        pipeline.appendConstantColor(&arena, color.data());
        pipeline.appendTransferFunction(*function);
        run_to_floats(output, pipeline, 4);
    }
    // sRGB loads and stores route through the transfer function stages.
    for (int highp = 0; highp < 2; ++highp) {
        gForceHighPrecisionRasterPipeline = highp != 0;
        conversion_case(output, kSRGBA_8888_SkColorType, 3, 13, false);
    }
    gForceHighPrecisionRasterPipeline = false;
}

// Extending one pipeline with another that uses the same memory context merges its load and
// store use; the combined pipeline then patches that context once for its tail.
void extend_corpus(Output& output) {
    for (const size_t width : {size_t{5}, size_t{8}, size_t{19}}) {
        std::vector<uint8_t> pixels = pattern_pixels(static_cast<size_t>(kWidth) * 4 * kRows, 3);
        SkRasterPipelineContexts::MemoryCtx ctx{pixels.data(), kWidth};
        SkSTArenaAlloc<256> firstArena;
        SkSTArenaAlloc<256> secondArena;
        SkSTArenaAlloc<256> joinedArena;
        SkRasterPipeline first(&firstArena);
        SkRasterPipeline second(&secondArena);
        SkRasterPipeline joined(&joinedArena);
        first.append(SkRasterPipelineOp::load_8888, &ctx);
        first.append(SkRasterPipelineOp::swap_rb);
        // srcover_rgba_8888 both loads and stores ctx, widening the load-only record.
        second.append(SkRasterPipelineOp::srcover_rgba_8888, &ctx);
        second.append(SkRasterPipelineOp::store_8888, &ctx);
        joined.extend(first);
        joined.extend(second);
        joined.appendClampIfNormalized(SkImageInfo::MakeN32Premul(4, 4));
        record_stage_names(output, joined);
        joined.run(2, 0, width, kRows);
        record_bytes(output, pixels);
        ++output.cases;
    }
}

void emboss_corpus(Output& output) {
    std::vector<uint8_t> multiply = pattern_pixels(static_cast<size_t>(kWidth) * kRows, 11);
    std::vector<uint8_t> add = pattern_pixels(static_cast<size_t>(kWidth) * kRows, 12);
    SkRasterPipelineContexts::EmbossCtx ctx{{multiply.data(), kWidth}, {add.data(), kWidth}};
    for (const size_t width : {size_t{3}, size_t{8}, size_t{21}}) {
        SkSTArenaAlloc<256> arena;
        SkRasterPipeline pipeline(&arena);
        const std::array<float, 4> color{0.4f, 0.3f, 0.2f, 0.9f};
        pipeline.appendConstantColor(&arena, color.data());
        pipeline.append(SkRasterPipelineOp::emboss, &ctx);
        run_to_floats(output, pipeline, width);
        append_bool(output, ctx.mul.pixels == multiply.data() && ctx.add.pixels == add.data());
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
    Output output{};
#ifndef STX_SKIA_REFERENCE
    if (!every_op_named()) { std::puts("raster pipeline op names failed"); return 1; }
#endif
    op_names(output);
    conversion_corpus(output);
    constant_corpus(output);
    matrix_corpus(output);
    transfer_corpus(output);
    extend_corpus(output);
    emboss_corpus(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("raster pipeline corpus: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
