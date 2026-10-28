#include "include/core/SkBlurTypes.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkShader.h"
#include "include/core/SkSurface.h"
#include "src/core/SkDevice.h"
#include "src/core/SkSpecialImage.h"
#include "src/shaders/SkImageShader.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <utility>

template <typename T>
concept HasCoverageMaskDispatch = requires(T& value) { value.useDrawCoverageMaskForMaskFilters(); };
template <typename T>
concept HasShaderSubsetFactory = requires { &T::MakeSubset; };
template <typename T>
concept TemporarySubsetBorrow = requires(T& value) { std::move(value).subset(); };
template <typename T>
concept TemporaryPropsBorrow = requires(T& value) { std::move(value).props(); };
template <typename T>
concept TemporaryColorBorrow = requires(T& value) { std::move(value).colorInfo(); };
template <typename T>
concept TemporarySpaceBorrow = requires(T& value) { std::move(value).getColorSpace(); };
#ifndef STX_SKIA_REFERENCE
static_assert(!TemporarySubsetBorrow<SkSpecialImage>);
static_assert(!TemporaryPropsBorrow<SkSpecialImage>);
static_assert(!TemporaryColorBorrow<SkSpecialImage>);
static_assert(!TemporarySpaceBorrow<SkSpecialImage>);
static_assert(!HasCoverageMaskDispatch<SkDevice>);
static_assert(!HasShaderSubsetFactory<SkImageShader>);
#endif

namespace {
constexpr int sourceWidth = 12;
constexpr int sourceHeight = 10;
constexpr int width = 32;
constexpr int height = 26;
using Pixels = std::array<uint32_t, width * height>;

sk_sp<SkImage> make_image(const SkIRect& admitted, bool altered, bool alphaOnly) {
    const SkColorType type = alphaOnly ? kAlpha_8_SkColorType : kRGBA_8888_SkColorType;
    const SkImageInfo info = SkImageInfo::Make(sourceWidth, sourceHeight, type, kPremul_SkAlphaType);
    std::vector<uint8_t> data(info.computeMinByteSize(), 0);
    const SkPixmap pixels(info, data.data(), info.minRowBytes());
    for (int y = 0; y < sourceHeight; ++y) {
        for (int x = 0; x < sourceWidth; ++x) {
            const bool inside = admitted.contains(x, y);
            const bool change = altered && !inside;
            const SkColor color = change ? 0xf0f010d0U : SkColorSetARGB(
                    64 + (x * 13 + y * 17) % 192, (x * 37) % 256, (y * 43) % 256, (x + y) * 11);
            const SkIRect pixel = SkIRect::MakeXYWH(x, y, 1, 1);
            if (!pixels.erase(color, pixel)) return nullptr;
        }
    }
    sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(pixels);
    return image;
}

bool render(const sk_sp<SkImage>& image, const SkRect& source,
            const SkSamplingOptions& sampling, int transform, bool blur,
            SkCanvas::SrcRectConstraint constraint, Pixels& pixels) {
    pixels.fill(0);
    const SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
    const sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels.data(), width * sizeof(uint32_t));
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    if (transform == 1) {
        canvas.translate(1.25f, -0.5f);
        canvas.skew(0.125f, -0.0625f);
    } else if (transform == 2) {
        SkMatrix matrix{};
        matrix.setPerspX(0.01f);
        canvas.concat(matrix);
    }
    SkPaint paint{};
    paint.setAntiAlias(true);
    paint.setShader(SkShaders::Color(0xd049a3e1U));
    if (blur) paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, 1.25f));
    const SkRect destination = SkRect::MakeLTRB(5.25f, 4.5f, 27.75f, 21.25f);
    canvas.drawImageRect(image.get(), source, destination, sampling, &paint, constraint);
    return true;
}

bool special_ownership() {
    const SkIRect subset = SkIRect::MakeLTRB(2, 2, 9, 8);
    const SkSurfaceProps props{};
    SkBitmap bitmap{};
    if (!bitmap.tryAllocN32Pixels(sourceWidth, sourceHeight)) return false;
    bitmap.eraseColor(0xff325a91U);
    sk_sp<SkSpecialImage> view = SkSpecialImages::MakeFromRaster(subset, bitmap, props);
    sk_sp<SkSpecialImage> copy = SkSpecialImages::CopyFromRaster(subset, bitmap, props);
    if (!view || !copy) return false;
    if ((*view).isExactFit() || !(*copy).isExactFit()) return false;
    if ((*view).dimensions() != subset.size() || (*copy).dimensions() != subset.size()) return false;
    SkBitmap viewPixels{};
    SkBitmap copyPixels{};
    if (!SkSpecialImages::AsBitmap(view.get(), &viewPixels) ||
        !SkSpecialImages::AsBitmap(copy.get(), &copyPixels)) return false;
    if (viewPixels.pixelRef() != bitmap.pixelRef() || copyPixels.pixelRef() == bitmap.pixelRef()) {
        return false;
    }
    const SkIRect pixel = SkIRect::MakeXYWH(2, 2, 1, 1);
    bitmap.erase(0xffb34712U, pixel);
    if (viewPixels.getColor(0, 0) != 0xffb34712U || copyPixels.getColor(0, 0) != 0xff325a91U) {
        return false;
    }
    sk_sp<SkSpecialImage> nested = (*view).makeSubset(SkIRect::MakeXYWH(1, 1, 3, 2));
    sk_sp<SkSpecialImage> expanded = (*view).makePixelOutset();
    if (!nested || !expanded || (*nested).subset() != SkIRect::MakeLTRB(3, 3, 6, 5) ||
        (*expanded).subset() != SkIRect::MakeLTRB(1, 1, 10, 9)) return false;
    sk_sp<SkImage> backing = (*view).asImage();
    if (!backing || (*backing).dimensions() != bitmap.dimensions()) return false;
    bitmap.reset();
    backing.reset();
    expanded.reset();
    viewPixels.reset();
    copyPixels.reset();
    view.reset();
    if (!SkSpecialImages::AsBitmap(nested.get(), &viewPixels)) return false;
    nested.reset();
    if (viewPixels.getColor(0, 0) != 0xff325a91U) return false;
    if (SkSpecialImages::AsBitmap(nullptr, &viewPixels)) return false;
    if (viewPixels.getColor(0, 0) != 0xff325a91U) return false;
    return true;
}

bool render_special(const SkSpecialImage& image, SkTileMode tile,
                    const SkSamplingOptions& sampling, bool strict, Pixels& pixels) {
    pixels.fill(0);
    const SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
    const sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels.data(), width * sizeof(uint32_t));
    if (!surface) return false;
    SkMatrix matrix = SkMatrix::Scale(1.5f, 1.25f);
    matrix.postTranslate(4.25f, 3.75f);
    sk_sp<SkShader> shader = image.asShader(tile, sampling, matrix, strict);
    if (!shader) return false;
    SkPaint paint{};
    paint.setShader(std::move(shader));
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.drawPaint(paint);
    return true;
}

bool special_corpus(std::vector<uint32_t>& output) {
    if (!special_ownership()) return false;
    const SkIRect subset = SkIRect::MakeLTRB(2, 2, 9, 8);
    const SkSurfaceProps props{};
    const std::array<SkTileMode, 4> modes{SkTileMode::kClamp, SkTileMode::kRepeat,
                                         SkTileMode::kMirror, SkTileMode::kDecal};
    const std::array<SkSamplingOptions, 3> options{
        SkSamplingOptions{}, SkSamplingOptions(SkFilterMode::kLinear),
        SkSamplingOptions(SkCubicResampler::Mitchell())};
    for (int alpha = 0; alpha < 2; ++alpha) {
        sk_sp<SkImage> original = make_image(subset, false, alpha != 0);
        sk_sp<SkImage> changed = make_image(subset, true, alpha != 0);
        const sk_sp<SkSpecialImage> first = SkSpecialImages::MakeFromRaster(subset, original, props);
        const sk_sp<SkSpecialImage> second = SkSpecialImages::MakeFromRaster(subset, changed, props);
        original.reset();
        changed.reset();
        if (!first || !second) return false;
        for (const SkTileMode mode : modes) {
            for (const SkSamplingOptions& sampling : options) {
                Pixels before{};
                Pixels after{};
                if (!render_special(*first, mode, sampling, true, before) ||
                    !render_special(*second, mode, sampling, true, after)) return false;
                if (before != after) return false;
                output.insert(output.end(), before.begin(), before.end());
                if (!render_special(*first, mode, sampling, false, before)) return false;
                output.insert(output.end(), before.begin(), before.end());
            }
        }
    }
    return true;
}

bool corpus(std::vector<uint32_t>& output) {
    const std::array<SkRect, 3> sources{SkRect::MakeLTRB(2, 2, 9, 8),
                                      SkRect::MakeLTRB(2.25f, 1.75f, 8.5f, 7.25f),
                                      SkRect::MakeLTRB(-2, -1, 8, 7)};
    const std::array<SkSamplingOptions, 5> options{
        SkSamplingOptions{}, SkSamplingOptions(SkFilterMode::kLinear),
        SkSamplingOptions(SkCubicResampler::Mitchell()),
        SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear),
        SkSamplingOptions::Aniso(4)};
    size_t fastDifferences = 0;
    for (const SkRect& source : sources) {
        const SkIRect admitted = source.roundOut();
        for (int alpha = 0; alpha < 2; ++alpha) {
            const sk_sp<SkImage> original = make_image(admitted, false, alpha != 0);
            const sk_sp<SkImage> changed = make_image(admitted, true, alpha != 0);
            if (!original || !changed) return false;
            for (const SkSamplingOptions& sampling : options) {
                for (int transform = 0; transform < 3; ++transform) {
                    for (int blur = 0; blur < 2; ++blur) {
                        Pixels first{};
                        Pixels second{};
                        if (!render(original, source, sampling, transform, blur != 0,
                                    SkCanvas::kStrict_SrcRectConstraint, first) ||
                            !render(changed, source, sampling, transform, blur != 0,
                                    SkCanvas::kStrict_SrcRectConstraint, second)) return false;
                        if (first != second) return false;
                        bool drawn = false;
                        for (const uint32_t pixel : first) {
                            if (pixel != 0) drawn = true;
                        }
                        if (!drawn) return false;
                        output.insert(output.end(), first.begin(), first.end());
                        if (!render(original, source, sampling, transform, blur != 0,
                                    SkCanvas::kFast_SrcRectConstraint, first) ||
                            !render(changed, source, sampling, transform, blur != 0,
                                    SkCanvas::kFast_SrcRectConstraint, second)) return false;
                        if (first != second) ++fastDifferences;
                        output.insert(output.end(), first.begin(), first.end());
                    }
                }
            }
        }
    }
    std::printf("subset perturbations affect %zu fast cases and zero strict cases\n", fastDifferences);
    const bool exercisedOutsideSamples = fastDifferences > 0;
    return exercisedOutsideSamples;
}
}

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
    std::vector<uint32_t> pixels{};
    if (!corpus(pixels) || !special_corpus(pixels)) return 1;
    if (argc == 2) {
        FILE* file = std::fopen(argv[1], "wb");
        if (file == nullptr) return 2;
        const size_t written = std::fwrite(pixels.data(), sizeof(uint32_t), pixels.size(), file);
        const int closed = std::fclose(file);
        if (written != pixels.size() || closed != 0) return 2;
    }
    std::printf("image subsets: %zu pixels PASS\n", pixels.size());
    return 0;
}
