#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTileMode.h"
#include "include/core/SkCPURecorder.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageGenerator.h"
#include "include/core/SkPixmap.h"
#include "src/core/SkMipmap.h"
#include "src/core/SkMipmapAccessor.h"
#include "src/core/SkArenaAlloc.h"
#include "include/core/SkGraphics.h"
#include "src/image/SkImage_Raster.h"
#include "peek_pixels.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#if !defined(_WIN32)
#include <sys/mman.h>
#endif

class GrDirectContext;
template <typename T>
concept HasTextureQuery = requires(const T& image) { image.isTextureBacked(); image.textureSize(); };
template <typename T>
concept HasProtectedQuery = requires(const T& image) { image.isProtected(); };
template <typename T>
concept HasYuvRead = requires { &T::asyncRescaleAndReadPixelsYUV420; };
template <typename T>
concept AcceptsGpuContext = requires(const T& image, GrDirectContext* context, const SkPixmap& dst) {
    image.readPixels(context, dst, 0, 0);
};
#ifdef STX_SKIA_REFERENCE
static_assert(HasTextureQuery<SkImage>);
static_assert(HasProtectedQuery<SkImage>);
static_assert(HasYuvRead<SkImage>);
static_assert(AcceptsGpuContext<SkImage>);
#else
static_assert(!HasTextureQuery<SkImage>);
static_assert(!HasProtectedQuery<SkImage>);
static_assert(!HasYuvRead<SkImage>);
static_assert(!AcceptsGpuContext<SkImage>);
#endif

namespace {
constexpr int width = 9;
constexpr int height = 7;
constexpr std::size_t stride = 52;
constexpr std::size_t byteCount = (height - 1) * stride + width * 4;
using Pixels = std::array<uint8_t, byteCount>;

SkImageInfo image_info() {
    const SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType,
                                              kPremul_SkAlphaType);
    return info;
}

void fill_pixels(Pixels& pixels) {
    pixels.fill(0xad);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t offset = static_cast<std::size_t>(y) * stride + x * 4U;
            pixels[offset] = static_cast<uint8_t>(x * 20);
            pixels[offset + 1] = static_cast<uint8_t>(y * 30);
            pixels[offset + 2] = 70;
            pixels[offset + 3] = 255;
        }
    }
}

bool check_pixels(const SkImage& image, int sourceX, int sourceY) {
    const std::optional<SkPixmap> pixelsView = peek_pixels(image);
    if (!pixelsView) return false;
    const SkPixmap& pixels = *pixelsView;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const SkColor expected = SkColorSetARGB(255, (x + sourceX) * 20,
                                                    (y + sourceY) * 30, 70);
            if (pixels.getColor(x, y) != expected) return false;
        }
    }
    return true;
}

struct ReleaseState { int calls = 0; };
void release_pixels(const void*, void* context) {
    ReleaseState& state = *static_cast<ReleaseState*>(context);
    ++state.calls;
}

bool factory_ownership() {
    ReleaseState released;
    Pixels pixels{};
    fill_pixels(pixels);
    const SkImageInfo info = image_info();
    const SkPixmap source(info, pixels.data(), stride);
    sk_sp<SkImage> copy = SkImages::RasterFromPixmapCopy(source);
    if (!copy || !check_pixels(*copy, 0, 0)) return false;
    const SkImage_Base& copyBase = *as_IB(copy);
    const SkBitmap* copyBitmap = copyBase.onPeekBitmap();
    if (!copyBitmap) return false;
    const SkPixelRef* copyPixels = (*copyBitmap).pixelRef();
    if (!copyPixels) return false;
#ifdef STX_SKIA_REFERENCE
    // Capture bookkeeping keeps a second owner in the original implementation.
    if ((*copyPixels).unique()) return false;
#else
    if (!(*copyPixels).unique()) return false;
#endif
    pixels[0] = 255;
    if (!check_pixels(*copy, 0, 0)) return false;
    fill_pixels(pixels);
    sk_sp<SkImage> borrowed = SkImages::RasterFromPixmap(source, release_pixels, &released);
    if (!borrowed || !check_pixels(*borrowed, 0, 0)) return false;
    sk_sp<SkImage> secondOwner = borrowed;
    borrowed.reset();
    if (released.calls != 0) return false;
    secondOwner.reset();
    if (released.calls != 1) return false;

    const SkPixmap invalid(info, pixels.data(), 1);
    if (SkImages::RasterFromPixmap(invalid, release_pixels, &released)) return false;
    if (released.calls != 1) return false;
    sk_sp<SkData> shortData = SkData::MakeWithCopy(pixels.data(), byteCount - 1);
    if (SkImages::RasterFromData(info, shortData, stride)) return false;
    sk_sp<SkData> exactData = SkData::MakeWithCopy(pixels.data(), byteCount);
    sk_sp<SkImage> retained = SkImages::RasterFromData(info, exactData, stride);
    if (!retained) return false;
    exactData.reset();
    const bool correct = check_pixels(*retained, 0, 0);
    return correct;
}

bool bitmap_ownership() {
    Pixels pixels{};
    fill_pixels(pixels);
    const SkImageInfo info = image_info();
    SkBitmap bitmap;
    if (!bitmap.installPixels(info, pixels.data(), stride)) return false;
    sk_sp<SkImage> copied = SkImages::RasterFromBitmap(bitmap);
    if (!copied) return false;
    bitmap.eraseColor(SK_ColorBLUE);
    if (!check_pixels(*copied, 0, 0)) return false;
    fill_pixels(pixels);
    bitmap.setImmutable();
    sk_sp<SkImage> shared = SkImages::RasterFromBitmap(bitmap);
    if (!shared || (*shared).uniqueID() != bitmap.getGenerationID()) return false;
    const std::optional<SkPixmap> view = peek_pixels(*shared);
    if (!view || (*view).addr() != pixels.data()) return false;
    SkBitmap subset;
    if (!bitmap.extractSubset(&subset, SkIRect::MakeXYWH(2, 1, 4, 3))) return false;
    sk_sp<SkImage> subsetImage = SkImages::RasterFromBitmap(subset);
    if (!subsetImage || (*subsetImage).uniqueID() == (*shared).uniqueID()) return false;
    bitmap.reset();
    subset.reset();
    if (!check_pixels(*subsetImage, 2, 1) || !check_pixels(*shared, 0, 0)) return false;

    SkBitmap legacy;
    if (!(*subsetImage).asLegacyBitmap(&legacy)) return false;
    if (legacy.getColor(0, 0) != SkColorSetARGB(255, 40, 30, 70)) return false;
    if (!legacy.isImmutable()) return false;
    return true;
}

bool mipmaps_and_subsets() {
    Pixels pixels{};
    fill_pixels(pixels);
    const SkImageInfo info = image_info();
    const SkPixmap source(info, pixels.data(), stride);
    sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(source);
    if (!image) return false;
    sk_sp<SkImage> mipmapped = (*image).withDefaultMipmaps();
    if (!mipmapped || !(*mipmapped).hasMipmaps()) return false;
    if ((*mipmapped).uniqueID() == (*image).uniqueID()) return false;
    const SkImage::RequiredProperties mipProperties{.fMipmapped = true};
    const SkIRect bounds = SkIRect::MakeWH(width, height);
    sk_sp<SkImage> fullCopy = (*mipmapped).makeSubset(nullptr, bounds, mipProperties);
    if (!fullCopy || !(*fullCopy).hasMipmaps()) return false;
    const SkImage_Base& originalBase = *as_IB(mipmapped);
    const SkImage_Base& copyBase = *as_IB(fullCopy);
    sk_sp<SkMipmap> originalMips = originalBase.refMips();
    sk_sp<SkMipmap> copiedMips = copyBase.refMips();
    if (!originalMips || !copiedMips || originalMips == copiedMips) return false;
    const int levels = (*originalMips).countLevels();
    if ((*copiedMips).countLevels() != levels) return false;
    for (int i = 0; i < levels; ++i) {
        SkMipmap::Level original{};
        SkMipmap::Level copy{};
        if (!(*originalMips).getLevel(i, &original) || !(*copiedMips).getLevel(i, &copy)) return false;
        if (original.fPixmap.dimensions() != copy.fPixmap.dimensions()) return false;
        if (original.fPixmap.addr() == copy.fPixmap.addr()) return false;
        for (int y = 0; y < original.fPixmap.height(); ++y) {
            const void* originalRow = original.fPixmap.addr(0, y);
            const void* copyRow = copy.fPixmap.addr(0, y);
            const std::size_t rowSize = original.fPixmap.info().minRowBytes();
            if (std::memcmp(originalRow, copyRow, rowSize) != 0) return false;
        }
    }
    const SkIRect subsetBounds = SkIRect::MakeXYWH(2, 1, 4, 3);
    for (int useMips = 0; useMips < 2; ++useMips) {
        const SkImage::RequiredProperties properties{.fMipmapped = useMips != 0};
        sk_sp<SkImage> subset = (*mipmapped).makeSubset(nullptr, subsetBounds, properties);
        if (!subset || !check_pixels(*subset, 2, 1)) return false;
        if ((*subset).hasMipmaps() != (useMips != 0)) return false;
    }
    if ((*image).makeSubset(nullptr, SkIRect::MakeWH(width + 1, height), {})) return false;
    if ((*image).makeSubset(nullptr, SkIRect::MakeEmpty(), {})) return false;
    return true;
}

bool conversions_and_reads() {
    Pixels pixels{};
    fill_pixels(pixels);
    const SkImageInfo info = image_info();
    const SkPixmap source(info, pixels.data(), stride);
    sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(source);
    if (!image) return false;
    sk_sp<SkImage> converted = (*image).makeColorTypeAndColorSpace(
        nullptr, kBGRA_8888_SkColorType, nullptr, {});
    if (!converted || (*converted).colorType() != kBGRA_8888_SkColorType) return false;
    if (!check_pixels(*converted, 0, 0)) return false;
    if ((*image).makeColorTypeAndColorSpace(nullptr, kUnknown_SkColorType, nullptr, {})) return false;
    sk_sp<SkColorSpace> linear = SkColorSpace::MakeSRGBLinear();
    sk_sp<SkImage> reinterpreted = (*image).reinterpretColorSpace(linear);
    if (!reinterpreted || !SkColorSpace::Equals((*reinterpreted).colorSpace(), linear.get())) return false;
    const std::optional<SkPixmap> reinterpretedPixelsView = peek_pixels(*reinterpreted);
    if (!reinterpretedPixelsView) return false;
    const SkPixmap& reinterpretedPixels = *reinterpretedPixelsView;
    for (int y = 0; y < height; ++y) {
        if (std::memcmp(reinterpretedPixels.addr(0, y), source.addr(0, y), width * 4) != 0) return false;
    }
    constexpr std::size_t destinationStride = 60;
    std::array<uint8_t, destinationStride * (height + 2)> output{};
    output.fill(0xe1);
    const SkPixmap destination(info.makeWH(width + 2, height + 2), output.data(), destinationStride);
    if (!(*image).readPixels(nullptr, destination, -1, -1)) return false;
    for (int y = 0; y < height + 2; ++y) {
        for (std::size_t byte = 0; byte < destinationStride; ++byte) {
            uint8_t expected = 0xe1;
            if (y > 0 && y <= height && byte >= 4 && byte < (width + 1) * 4U) {
                expected = pixels[static_cast<std::size_t>(y - 1) * stride + byte - 4];
            }
            if (output[static_cast<std::size_t>(y) * destinationStride + byte] != expected) return false;
        }
    }
    return true;
}

bool facade_scaling() {
    Pixels pixels{};
    fill_pixels(pixels);
    const SkImageInfo info = image_info();
    const SkPixmap source(info, pixels.data(), stride);
    sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(source);
    if (!image || !peek_pixels(*image)) return false;
    if ((*image).bounds() != SkIRect::MakeWH(width, height)) return false;
    if ((*image).dimensions() != SkISize::Make(width, height)) return false;
    if ((*image).isAlphaOnly() || (*image).isOpaque()) return false;
    const SkSamplingOptions sampling;
    if ((*image).makeScaled(info, sampling) != image) return false;
    if ((*image).makeRasterImage() != image || (*image).makeRasterImage(nullptr) != image) return false;
    const SkImageInfo reducedInfo = info.makeWH(3, height);
    for (int variant = 0; variant < 3; ++variant) {
        sk_sp<SkImage> scaled;
        if (variant == 0) scaled = (*image).makeScaled(reducedInfo, sampling);
        if (variant == 1) scaled = (*image).makeScaled(nullptr, reducedInfo, sampling);
        if (variant == 2) scaled = (*image).makeScaled(nullptr, reducedInfo, sampling, SkSurfaceProps{});
        if (!scaled || (*scaled).width() != 3 || (*scaled).height() != height) return false;
        const std::optional<SkPixmap> outputView = peek_pixels(*scaled);
        if (!outputView) return false;
        const SkPixmap& output = *outputView;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < 3; ++x) {
                const SkColor expected = SkColorSetARGB(255, (3 * x + 1) * 20, y * 30, 70);
                if (output.getColor(x, y) != expected) return false;
            }
        }
    }
    if ((*image).makeScaled(info.makeWH(0, height), sampling)) return false;
    std::array<uint8_t, stride * height> destination{};
    destination.fill(0xdf);
    const SkPixmap reduced(reducedInfo, destination.data(), stride);
    if (!(*image).scalePixels(reduced, sampling)) return false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < 3; ++x) {
            if (reduced.getColor(x, y) != SkColorSetARGB(255, (3 * x + 1) * 20, y * 30, 70)) return false;
        }
        for (std::size_t byte = 12; byte < stride; ++byte) {
            if (destination[static_cast<std::size_t>(y) * stride + byte] != 0xdf) return false;
        }
    }
    const SkPixmap full(info, destination.data(), stride);
    if (!(*image).scalePixels(full, sampling)) return false;
    for (int y = 0; y < height; ++y) {
        if (std::memcmp(full.addr(0, y), source.addr(0, y), width * 4) != 0) return false;
    }
    return true;
}

bool facade_shaders() {
    Pixels pixels{};
    fill_pixels(pixels);
    const SkImageInfo info = image_info();
    const SkPixmap source(info, pixels.data(), stride);
    sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(source);
    if (!image) return false;
    const SkMatrix matrix = SkMatrix::Translate(1.0f, 1.0f);
    const SkSamplingOptions sampling;
    const SkSamplingOptions cubic(SkCubicResampler::Mitchell());
    if ((*image).makeRawShader(cubic)) return false;
    for (int raw = 0; raw < 2; ++raw) {
        for (int variant = 0; variant < 4; ++variant) {
            sk_sp<SkShader> shader;
            if (raw == 0) {
                if (variant == 0) shader = (*image).makeShader(sampling, matrix);
                if (variant == 1) shader = (*image).makeShader(sampling, &matrix);
                if (variant == 2) shader = (*image).makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat, sampling, matrix);
                if (variant == 3) shader = (*image).makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat, sampling, &matrix);
            } else {
                if (variant == 0) shader = (*image).makeRawShader(sampling, matrix);
                if (variant == 1) shader = (*image).makeRawShader(sampling, &matrix);
                if (variant == 2) shader = (*image).makeRawShader(SkTileMode::kRepeat, SkTileMode::kRepeat, sampling, matrix);
                if (variant == 3) shader = (*image).makeRawShader(SkTileMode::kRepeat, SkTileMode::kRepeat, sampling, &matrix);
            }
            if (!shader) return false;
            const SkImageInfo outputInfo = info.makeWH(width + 2, height + 2);
            sk_sp<SkSurface> surface = SkSurfaces::Raster(outputInfo);
            if (!surface) return false;
            SkPaint paint;
            paint.setShader(std::move(shader));
            SkCanvas& canvas = *(*surface).getCanvas();
            canvas.drawPaint(paint);
            const std::optional<SkPixmap> outputView = peek_pixels(*surface);
            if (!outputView) return false;
            const SkPixmap& output = *outputView;
            for (int y = 0; y < height + 2; ++y) {
                for (int x = 0; x < width + 2; ++x) {
                    int sourceX = x - 1;
                    int sourceY = y - 1;
                    if (variant >= 2) {
                        sourceX = (sourceX + width) % width;
                        sourceY = (sourceY + height) % height;
                    } else {
                        if (sourceX < 0) sourceX = 0;
                        if (sourceY < 0) sourceY = 0;
                        if (sourceX >= width) sourceX = width - 1;
                        if (sourceY >= height) sourceY = height - 1;
                    }
                    const SkColor expected = SkColorSetARGB(255, sourceX * 20, sourceY * 30, 70);
                    if (output.getColor(x, y) != expected) return false;
                }
            }
        }
    }
    return true;
}

class SolidGenerator final : public SkImageGenerator {
public:
    explicit SolidGenerator(bool succeed) : SkImageGenerator(image_info()), fSucceed(succeed) {}
protected:
    bool onGetPixels(const SkImageInfo& info, void* pixels, size_t rowBytes,
                     const Options&) override {
        if (!fSucceed) return false;
        const SkPixmap destination(info, pixels, rowBytes);
        const bool filled = destination.erase(SK_ColorGREEN);
        return filled;
    }
private:
    const bool fSucceed;
};

struct MipmapSelection {
    SkMipmapMode mode;
    float scaleX;
    float scaleY;
    int upperWidth;
    int upperHeight;
    int lowerWidth;
    int lowerHeight;
    float lowerWeight;
};

bool check_level(const std::pair<SkPixmap, SkMatrix>& selected, int expectedWidth,
                 int expectedHeight, int baseWidth, int baseHeight) {
    const SkPixmap& pixels = selected.first;
    if (pixels.width() != expectedWidth || pixels.height() != expectedHeight) return false;
    const float scaleX = static_cast<float>(expectedWidth) / baseWidth;
    const float scaleY = static_cast<float>(expectedHeight) / baseHeight;
    const SkMatrix expectedScale = SkMatrix::Scale(scaleX, scaleY);
    if (selected.second != expectedScale) return false;
    for (int y = 0; y < expectedHeight; ++y) {
        for (int x = 0; x < expectedWidth; ++x) {
            if (pixels.getColor(x, y) != SK_ColorGREEN) return false;
        }
    }
    return true;
}

bool mipmap_selection() {
    // ComputeLevel's existing half-level sharpening bias makes inverse scales
    // 2, 4 and 8 yield 0.5, 1.5 and 2.5. These expectations do not call it.
    constexpr std::array<MipmapSelection, 13> cases{{
        {SkMipmapMode::kNone, 4.0f, 4.0f, 9, 7, 0, 0, 0.0f},
        {SkMipmapMode::kNearest, 1.0f, 1.0f, 9, 7, 0, 0, 0.0f},
        {SkMipmapMode::kLinear, 0.5f, 0.5f, 9, 7, 0, 0, 0.0f},
        {SkMipmapMode::kNearest, 2.0f, 2.0f, 4, 3, 0, 0, 0.0f},
        {SkMipmapMode::kLinear, 2.0f, 2.0f, 9, 7, 4, 3, 0.5f},
        {SkMipmapMode::kNearest, 4.0f, 4.0f, 2, 1, 0, 0, 0.0f},
        {SkMipmapMode::kLinear, 4.0f, 4.0f, 4, 3, 2, 1, 0.5f},
        {SkMipmapMode::kNearest, 8.0f, 8.0f, 1, 1, 0, 0, 0.0f},
        {SkMipmapMode::kLinear, 8.0f, 8.0f, 2, 1, 1, 1, 0.5f},
        {SkMipmapMode::kLinear, 16.0f, 16.0f, 1, 1, 0, 0, 0.0f},
        {SkMipmapMode::kNearest, 1024.0f, 1024.0f, 9, 7, 0, 0, 0.0f},
        {SkMipmapMode::kLinear, 4.0f, 1.0f, 4, 3, 2, 1, 0.5f},
        {SkMipmapMode::kLinear, 0.0f, 0.0f, 9, 7, 0, 0, 0.0f},
    }};
    Pixels storage{};
    const SkPixmap source(image_info(), storage.data(), stride);
    if (!source.erase(SK_ColorGREEN)) return false;
    for (int attached = 0; attached < 2; ++attached) {
        for (const MipmapSelection& expected : cases) {
            sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(source);
            if (!image) return false;
            if (attached != 0) {
                image = (*image).withDefaultMipmaps();
                if (!image) return false;
            }
            SkArenaAlloc arena(1024);
            const SkMatrix inverse = SkMatrix::Scale(expected.scaleX, expected.scaleY);
            SkMipmapAccessor* accessor = SkMipmapAccessor::Make(&arena, image.get(),
                                                               inverse, expected.mode);
            if (!accessor) return false;
            SkMipmapAccessor* repeated = SkMipmapAccessor::Make(&arena, image.get(),
                                                               inverse, expected.mode);
            if (!repeated) return false;
            const std::pair<SkPixmap, SkMatrix> first = (*accessor).level();
            const std::pair<SkPixmap, SkMatrix> second = (*repeated).level();
            if (first.first.addr() != second.first.addr()) return false;
            // Only accessors now retain the pixel storage, even after eviction.
            image.reset();
            SkGraphics::PurgeResourceCache();
            if (!check_level(first, expected.upperWidth, expected.upperHeight, width, height)) {
                return false;
            }
            if ((*accessor).lowerWeight() != expected.lowerWeight) return false;
            if (expected.lowerWeight > 0.0f) {
                const std::pair<SkPixmap, SkMatrix> lower = (*accessor).lowerLevel();
                if (!check_level(lower, expected.lowerWidth, expected.lowerHeight, width, height)) {
                    return false;
                }
            }
        }
    }
    // A one-pixel image cannot produce any mip levels and must use its base.
    const SkImageInfo tinyInfo = image_info().makeWH(1, 1);
    const SkPixmap tinySource(tinyInfo, storage.data(), stride);
    sk_sp<SkImage> tiny = SkImages::RasterFromPixmapCopy(tinySource);
    if (!tiny) return false;
    SkArenaAlloc arena(1024);
    const SkMatrix reduced = SkMatrix::Scale(4.0f, 4.0f);
    SkMipmapAccessor* fallback = SkMipmapAccessor::Make(&arena, tiny.get(), reduced,
                                                       SkMipmapMode::kLinear);
    if (!fallback || (*fallback).lowerWeight() != 0.0f) return false;
    const std::pair<SkPixmap, SkMatrix> selected = (*fallback).level();
    if (!check_level(selected, 1, 1, 1, 1)) return false;
    constexpr std::array<SkMipmapMode, 3> modes{
        SkMipmapMode::kNone, SkMipmapMode::kNearest, SkMipmapMode::kLinear};
    for (const SkMipmapMode mode : modes) {
        std::unique_ptr<SkImageGenerator> generator = std::make_unique<SolidGenerator>(false);
        sk_sp<SkImage> failed = SkImages::DeferredFromGenerator(std::move(generator));
        if (!failed) return false;
        SkMipmapAccessor* missing = SkMipmapAccessor::Make(&arena, failed.get(), reduced, mode);
        if (missing != nullptr) return false;
    }
    return true;
}

struct ReadState {
    int calls = 0;
    std::unique_ptr<const SkImage::AsyncReadResult> result = nullptr;
};
void record_read(void* context, std::unique_ptr<const SkImage::AsyncReadResult> result) {
    ReadState& state = *static_cast<ReadState*>(context);
    ++state.calls;
    state.result = std::move(result);
}

bool generated_reads(bool succeed) {
    ReadState state;
    std::unique_ptr<SkImageGenerator> generator = std::make_unique<SolidGenerator>(succeed);
    sk_sp<SkImage> image = SkImages::DeferredFromGenerator(std::move(generator));
    if (!image) return false;
    const SkImageInfo inputInfo = image_info();
    const SkImageInfo outputInfo = inputInfo.makeWH(3, 2);
    const SkIRect sourceBounds = SkIRect::MakeXYWH(2, 1, 5, 4);
    (*image).asyncRescaleAndReadPixels(outputInfo, sourceBounds,
        SkImage::RescaleGamma::kSrc, SkImage::RescaleMode::kNearest, record_read, &state);
    if (state.calls != 1) return false;
    if (!succeed) {
        const bool absent = state.result == nullptr;
        return absent;
    }
    if (!state.result || (*state.result).count() != 1) return false;
    const SkPixmap result(outputInfo, (*state.result).data(0), (*state.result).rowBytes(0));
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 3; ++x) {
            if (result.getColor(x, y) != SK_ColorGREEN) return false;
        }
    }
    return true;
}

#if !defined(_WIN32) && !defined(STX_SKIA_REFERENCE)
bool oversized_immutable_rescale() {
    constexpr int largeHeight = (1 << 28) + 1;
    constexpr std::size_t largeStride = 8;
    constexpr std::size_t mappedBytes = static_cast<std::size_t>(largeHeight) * largeStride;
    // Reserve the declared backing without touching its pages. The rescale
    // path must reject the full extent before reading outside the first pixel.
    void* mapped = mmap(nullptr, mappedBytes, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mapped == MAP_FAILED) return false;

    bool passed = false;
    {
        const SkImageInfo largeInfo = SkImageInfo::Make(1, largeHeight,
            kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        SkBitmap bitmap;
        if (bitmap.installPixels(largeInfo, mapped, largeStride)) {
            bitmap.setImmutable();
            sk_sp<SkImage> image = SkImages::RasterFromBitmap(bitmap);
            if (image) {
                ReadState state;
                const SkImageInfo resultInfo = largeInfo.makeWH(1, 1);
                const SkIRect source = SkIRect::MakeXYWH(0, 0, 1, 1);
                (*image).asyncRescaleAndReadPixels(resultInfo, source,
                    SkImage::RescaleGamma::kSrc, SkImage::RescaleMode::kNearest,
                    record_read, &state);
                passed = state.calls == 1 && state.result == nullptr;
            }
        }
    }
    const int unmapped = munmap(mapped, mappedBytes);
    return passed && unmapped == 0;
}
#endif
}

int main() {
    if (!factory_ownership()) return 1;
    if (!bitmap_ownership()) return 2;
    if (!mipmaps_and_subsets()) return 3;
    if (!conversions_and_reads()) return 4;
    if (!generated_reads(false) || !generated_reads(true)) return 5;
#if !defined(_WIN32) && !defined(STX_SKIA_REFERENCE)
    if (!oversized_immutable_rescale()) return 9;
#endif
    if (!facade_scaling()) return 6;
    if (!facade_shaders()) return 7;
    if (!mipmap_selection()) return 8;
    std::printf("raster images: ownership, subsets, mipmaps, conversion, reads; base %zu, raster %zu bytes; PASS\n",
                sizeof(SkImage_Base), sizeof(SkImage_Raster));
    return 0;
}
