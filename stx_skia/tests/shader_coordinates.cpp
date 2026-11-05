#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkShader.h"
#include "include/core/SkSurface.h"
#include "src/shaders/SkLocalMatrixShader.h"
#include "src/shaders/SkTransformShader.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpList.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

namespace {
template <typename T>
concept TemporaryLocalMatrix = requires(T&& value) { std::move(value).localMatrix(); };
template <typename T>
concept TemporaryCTM = requires(T&& value) { std::move(value).ctm(); };
template <typename T>
concept TemporaryImage = requires(T&& value) { std::move(value).isAImage(nullptr, nullptr); };
template <typename T>
concept TemporaryShaderBase = requires(T&& value) { as_SB(std::move(value)); };
template <typename T>
concept FragmentProcessorMatrix = requires(T& value) { value.applyForFragmentProcessor(SkMatrix::I()); };
#ifndef STX_SKIA_REFERENCE
static_assert(!TemporaryShaderBase<sk_sp<SkShader>>);
static_assert(!FragmentProcessorMatrix<SkShaders::MatrixRec>);
static_assert(!TemporaryLocalMatrix<SkLocalMatrixShader>);
static_assert(!TemporaryCTM<SkCTMShader>);
static_assert(!TemporaryImage<SkLocalMatrixShader>);
#endif

constexpr int width = 19;
constexpr int height = 13;
constexpr uint32_t background = 0xff173963U;
using Pixels = std::array<uint32_t, width * height>;

bool render(const sk_sp<SkShader>& shader, const SkMatrix& transform, Pixels& pixels) {
    pixels.fill(background);
    const SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
    const sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels.data(), width * sizeof(uint32_t));
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.concat(transform);
    SkPaint paint{};
    paint.setShader(shader);
    paint.setBlendMode(SkBlendMode::kSrc);
    canvas.drawPaint(paint);
    return true;
}

sk_sp<SkImage> make_image() {
    const SkImageInfo info = SkImageInfo::MakeN32Premul(7, 5);
    const sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) return nullptr;
    SkCanvas& canvas = *(*surface).getCanvas();
    SkPaint paint{};
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 7; ++x) {
            const SkColor color = SkColorSetARGB(255, x * 31, y * 47, (x + y) * 19);
            paint.setColor(color);
            const SkRect rectangle = SkRect::MakeXYWH(static_cast<float>(x), static_cast<float>(y), 1, 1);
            canvas.drawRect(rectangle, paint);
        }
    }
    sk_sp<SkImage> image = (*surface).makeImageSnapshot();
    return image;
}

bool coordinate_cases(std::vector<uint32_t>& output) {
    const sk_sp<SkImage> image = make_image();
    if (!image) return false;
    SkMatrix first{};
    first.setScaleTranslate(1.5f, 0.75f, 2.0f, -1.0f);
    SkMatrix second{};
    second.setSkew(0.25f, -0.125f);
    const SkMatrix combined = SkShaderBase::ConcatLocalMatrices(second, first);
    const std::array<SkTileMode, 3> modes{SkTileMode::kClamp, SkTileMode::kRepeat, SkTileMode::kMirror};
    const std::array<SkFilterMode, 2> filters{SkFilterMode::kNearest, SkFilterMode::kLinear};
    SkMatrix perspective{};
    perspective.setPerspX(0.015625f);
    perspective.setPerspY(-0.0078125f);
    const std::array<SkMatrix, 3> transforms{SkMatrix::I(), combined, perspective};
    for (const SkTileMode mode : modes) {
        for (const SkFilterMode filter : filters) {
            const SkSamplingOptions sampling(filter);
            const sk_sp<SkShader> base = (*image).makeShader(mode, mode, sampling);
            if (!base) return false;
            const sk_sp<SkShader> inner = (*base).makeWithLocalMatrix(first);
            const sk_sp<SkShader> nested = (*inner).makeWithLocalMatrix(second);
            const sk_sp<SkShader> flattened = (*base).makeWithLocalMatrix(combined);
            SkMatrix reported{};
            std::array<SkTileMode, 2> reportedModes{};
            SkImage* reportedImage = (*nested).isAImage(&reported, reportedModes.data());
            if (reportedImage != image.get() || reported != combined ||
                reportedModes[0] != mode || reportedModes[1] != mode) return false;
            const SkRect subset = SkRect::MakeLTRB(1.5f, 1.5f, 4.5f, 3.5f);
            const sk_sp<SkShader> clamped = SkShaders::CoordClamp(nested, subset);
            if (!clamped) return false;
            for (const SkMatrix& transform : transforms) {
                Pixels nestedPixels{};
                Pixels flatPixels{};
                Pixels clampPixels{};
                if (!render(nested, transform, nestedPixels) ||
                    !render(flattened, transform, flatPixels) ||
                    !render(clamped, transform, clampPixels)) return false;
                if (nestedPixels != flatPixels) return false;
                output.insert(output.end(), nestedPixels.begin(), nestedPixels.end());
                output.insert(output.end(), clampPixels.begin(), clampPixels.end());
            }
        }
    }
    // With identity coordinates, all pixels beyond each clamp boundary sample
    // the same edge. This checks clamp behavior independently of the reference.
    const SkSamplingOptions nearest(SkFilterMode::kNearest);
    const sk_sp<SkShader> base = (*image).makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat, nearest);
    const SkRect subset = SkRect::MakeLTRB(2.5f, 1.5f, 4.5f, 3.5f);
    const sk_sp<SkShader> clamped = SkShaders::CoordClamp(base, subset);
    Pixels pixels{};
    if (!render(clamped, SkMatrix::I(), pixels)) return false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int edgeX = x < 2 ? 2 : (x > 4 ? 4 : x);
            const int edgeY = y < 1 ? 1 : (y > 3 ? 3 : y);
            if (pixels[y * width + x] != pixels[edgeY * width + edgeX]) return false;
        }
    }
    const SkRect inverted = SkRect::MakeLTRB(4, 0, 1, 3);
    if (SkShaders::CoordClamp(base, inverted) || SkShaders::CoordClamp(nullptr, subset)) return false;
    return true;
}

bool transform_child_failure() {
    const sk_sp<SkShader> empty = SkShaders::Empty();
    const SkShaderBase& child = *as_SB(empty);
    const SkTransformShader transform(child, false);
    SkSTArenaAlloc<2048> arena{};
    const SkSurfaceProps props{};
    SkRasterPipeline pipeline(&arena);
    const SkStageRec rec{&pipeline, &arena, kRGBA_8888_SkColorType, nullptr,
                         SkColors::kWhite, props, SkRect::MakeEmpty()};
    const SkShaders::MatrixRec identity{};
    const bool appended = transform.appendStages(rec, identity);
#ifdef STX_SKIA_REFERENCE
    // Preserve evidence that the original wrapper masks its child's failure.
    return appended;
#else
    return !appended;
#endif
}

bool transform_updates() {
    const sk_sp<SkImage> image = make_image();
    if (!image) return false;
    const SkSamplingOptions nearest{};
    const sk_sp<SkShader> shader = (*image).makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat, nearest);
    if (!shader) return false;
    const SkShaderBase& child = *as_SB(shader);
    SkTransformShader transform(child, false);
    Pixels pixels{};
    SkRasterPipelineContexts::MemoryCtx destination{pixels.data(), width};
    SkSTArenaAlloc<4096> arena{};
    const SkSurfaceProps props{};
    SkRasterPipeline pipeline(&arena);
    const SkStageRec rec{&pipeline, &arena, kRGBA_8888_SkColorType, nullptr,
                         SkColors::kWhite, props, SkRect::MakeEmpty()};
    const SkShaders::MatrixRec identity{};
    if (!transform.appendStages(rec, identity)) return false;
    pipeline.append(SkRasterPipelineOp::store_8888, &destination);
    pipeline.run(0, 0, width, height);
    const Pixels original = pixels;
    const SkMatrix shifted = SkMatrix::Translate(1, 2);
    if (!transform.update(shifted)) return false;
    pipeline.run(0, 0, width, height);
    const Pixels moved = pixels;
    if (moved == original) return false;
    SkMatrix perspective{};
    perspective.setPerspX(0.125f);
    if (transform.update(perspective)) return false;
    pipeline.run(0, 0, width, height);
    if (pixels != moved) return false;
    if (!transform.update(SkMatrix::I())) return false;
    pipeline.run(0, 0, width, height);
    const bool restored = pixels == original;
    return restored;
}

bool matrix_state() {
    SkSTArenaAlloc<2048> arena{};
    const SkSurfaceProps props{};
    SkRasterPipeline pipeline(&arena);
    const SkStageRec rec{&pipeline, &arena, kRGBA_8888_SkColorType, nullptr,
                         SkColors::kWhite, props, SkRect::MakeEmpty()};
    const SkShaders::MatrixRec identity{};
    if (identity.hasPendingMatrix() || identity.rasterPipelineCoordsAreSeeded()) return false;
    const std::optional<SkShaders::MatrixRec> seeded = identity.apply(rec);
    if (!seeded || !(*seeded).rasterPipelineCoordsAreSeeded() ||
        (*seeded).hasPendingMatrix() || pipeline.getNumStages() != 1) return false;
    const std::optional<SkShaders::MatrixRec> repeated = (*seeded).apply(rec);
    if (!repeated || pipeline.getNumStages() != 1) return false;

    SkMatrix scale{};
    scale.setScale(2, 4);
    SkShaders::MatrixRec scaled = (*seeded).concat(scale);
    scaled.markTotalMatrixInvalid();
    const std::optional<SkShaders::MatrixRec> applied = scaled.apply(rec);
    if (!applied || (*applied).totalMatrixIsValid() || (*applied).hasPendingMatrix() ||
        (*applied).totalMatrix() != scale || pipeline.getNumStages() != 2) return false;
    const std::optional<SkMatrix> inverse = (*applied).totalInverse();
    const SkMatrix expectedInverse = SkMatrix::Scale(0.5f, 0.25f);
    if (!inverse || *inverse != expectedInverse) return false;

    SkMatrix singular{};
    singular.setScale(0, 1);
    const SkShaders::MatrixRec invalid = (*applied).concat(singular);
    const SkRasterPipeline::StageList* previous = pipeline.getStageList();
    const int stageCount = pipeline.getNumStages();
    if (invalid.apply(rec) || invalid.totalInverse()) return false;
    if (pipeline.getStageList() != previous || pipeline.getNumStages() != stageCount) return false;
    return true;
}

#ifndef STX_SKIA_REFERENCE
bool large_translations() {
    const std::array<uint8_t, 12> colors{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255};
    const SkImageInfo info = SkImageInfo::Make(3, 1, kRGBA_8888_SkColorType, kOpaque_SkAlphaType);
    const SkPixmap source(info, colors.data(), colors.size());
    const sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(source);
    if (!image) return false;
    const std::array<float, 8> translations{
        2147483520.0f, -2147483520.0f, 2147483648.0f, -2147483648.0f,
        4294967296.0f, -4294967296.0f,
        std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
    const SkSamplingOptions linear(SkFilterMode::kLinear);
    const SkImageInfo outputInfo = SkImageInfo::MakeN32Premul(width, height);
    for (const float translation : translations) {
        const SkMatrix local = SkMatrix::Translate(translation, 0);
        const sk_sp<SkShader> shader = (*image).makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
                                                         linear, local);
        if (!shader) return false;
        Pixels pixels{};
        if (!render(shader, SkMatrix::I(), pixels)) return false;
        const SkPixmap output(outputInfo, pixels.data(), width * sizeof(uint32_t));
        const SkColor expected = translation > 0 ? SK_ColorRED : SK_ColorBLUE;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (output.getColor(x, y) != expected) return false;
            }
        }
    }
    return true;
}
#endif

bool empty_and_singular() {
    const sk_sp<SkShader> empty = SkShaders::Empty();
    SkMatrix singular{};
    singular.setScale(0.0f, 1.0f);
    const sk_sp<SkImage> image = make_image();
    if (!image) return false;
    const SkSamplingOptions sampling{};
    const sk_sp<SkShader> base = (*image).makeShader(SkTileMode::kClamp, SkTileMode::kClamp, sampling);
    const sk_sp<SkShader> wrapped = (*base).makeWithLocalMatrix(singular);
    const std::array<sk_sp<SkShader>, 2> shaders{empty, wrapped};
    for (const sk_sp<SkShader>& shader : shaders) {
        Pixels pixels{};
        if (!render(shader, SkMatrix::I(), pixels)) return false;
        for (const uint32_t pixel : pixels) {
            if (pixel != background) return false;
        }
    }
    return true;
}
}

int main(int argc, char** argv) {
    std::vector<uint32_t> output{};
    if (!coordinate_cases(output)) return 1;
    if (!empty_and_singular()) return 2;
    if (!matrix_state()) return 4;
    if (!transform_child_failure()) return 5;
    if (!transform_updates()) return 6;
#ifndef STX_SKIA_REFERENCE
    // The upstream integrality test narrows these values outside int range.
    if (!large_translations()) return 7;
#endif
    if (argc == 2) {
        FILE* file = std::fopen(argv[1], "wb");
        if (file == nullptr) return 3;
        const size_t written = std::fwrite(output.data(), sizeof(uint32_t), output.size(), file);
        const int closed = std::fclose(file);
        if (written != output.size() || closed != 0) return 3;
    }
    std::printf("shader coordinates: %zu pixels PASS\n", output.size());
    return 0;
}
