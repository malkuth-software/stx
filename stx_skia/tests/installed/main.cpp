#include "include/core/SkFlattenable.h"
#if __has_include("src/core/SkMessageBus.h")
#error "The installed CPU package must not expose the removed generic message bus"
#endif

#if __has_include("include/core/SkExecutor.h") || __has_include("src/core/SkTaskGroup.h")
#error "The installed CPU package must not expose the removed Skia scheduler"
#endif

#include <stx_skia/pixel_storage.hpp>
#include <stx_skia/swizzle.hpp>
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkImage.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkShader.h"
#include "include/core/SkTileMode.h"
#include <array>
#include "include/effects/SkColorMatrix.h"
#include <vector>
#include <cstddef>

template <typename T>
concept HasEffectFactoryRegistry = requires { &T::Register; };
static_assert(!HasEffectFactoryRegistry<SkFlattenable>);

template <typename T>
concept HasImageFiltering = requires(T& paint) { paint.getImageFilter(); };
static_assert(!HasImageFiltering<SkPaint>);
template <typename T>
concept HasProgrammableMesh = requires { &T::drawMesh; };
static_assert(!HasProgrammableMesh<SkCanvas>);
// Drawables and annotations are withdrawn; expressions, not member pointers, so overloads count.
template <typename T>
concept HasDrawableDrawing = requires(T& canvas) { canvas.drawDrawable(nullptr, 0.0f, 0.0f); };
static_assert(!HasDrawableDrawing<SkCanvas>);
template <typename T>
concept HasAnnotations = requires(T& canvas, const SkRect& rect) {
    canvas.drawAnnotation(rect, "key", nullptr);
};
static_assert(!HasAnnotations<SkCanvas>);
#if __has_include("include/core/SkDrawable.h") || __has_include("include/core/SkAnnotation.h")
#error Drawable and annotation headers must not be installed.
#endif
#if __has_include("include/utils/SkNWayCanvas.h") || \
    __has_include("include/utils/SkPaintFilterCanvas.h") || \
    __has_include("include/utils/SkNullCanvas.h") || \
    __has_include("include/utils/SkCanvasStateUtils.h")
#error Wrapper canvas and canvas-state headers must not be installed.
#endif
#if __has_include("include/utils/SkShadowUtils.h")
#error Shadow utilities must not be installed.
#endif
#if __has_include("include/core/SkDocument.h") || __has_include("include/utils/SkCamera.h") || \
    __has_include("include/utils/SkParse.h") || __has_include("include/utils/SkParsePath.h") || \
    __has_include("include/utils/mac/SkCGUtils.h")
#error Document, camera, parsing and CoreGraphics bridge headers must not be installed.
#endif
#if __has_include("include/effects/SkRuntimeEffect.h")
#error Runtime compiler headers must not be installed.
#endif
#if __has_include("include/svg/SkSVGCanvas.h") || \
    __has_include("include/codec/SkCodec.h") || \
    __has_include("include/ports/SkImageGeneratorCG.h") || \
    __has_include("include/android/SkImageAndroid.h") || \
    __has_include("include/effects/Sk1DPathEffect.h") || \
    __has_include("include/private/chromium/GrPromiseImageTexture.h") || \
    __has_include("include/private/SkAnySubclass.h") || \
    __has_include("include/private/SkJpegGainmapEncoder.h") || \
    __has_include("modules/skcms/src/skcms_internals.h")
#error The installed package exposes a removed subsystem.
#endif

namespace {
struct ConsumerBase {
    virtual ~ConsumerBase() = default;
};
struct ConsumerDerived final : ConsumerBase {};
}

int main() {
    // Skia's private no-RTTI build must not disable RTTI for application types.
    ConsumerDerived consumer;
    ConsumerBase* base = &consumer;
    ConsumerDerived* recovered = dynamic_cast<ConsumerDerived*>(base);
    if (recovered != &consumer) return 4;
    // The swap kernel is reachable through the full package too.
    std::array<std::byte, 4> rgba{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    std::array<std::byte, 4> bgra{};
    if (stx_skia::swap_red_blue(bgra, rgba) != stx_skia::SwizzleResult::ok ||
        bgra[0] != std::byte{3}) return 12;
    const stx_skia::SwizzleResult rows = stx_skia::swap_red_blue_rows(
            bgra, stx_skia::RowBytes(4), bgra, stx_skia::RowBytes(4), stx_skia::PixelCount(1),
            stx_skia::RowCount(1));
    if (rows != stx_skia::SwizzleResult::ok || bgra != rgba) return 13;
    std::vector<std::byte> pixels(16U * 16U * 4U, std::byte{0xFF});
    sk_sp<SkData> data = stx_skia::take_pixel_data(pixels);
    if (!data || !pixels.empty()) return 1;
    const SkImageInfo info = SkImageInfo::MakeN32Premul(16, 16, SkColorSpace::MakeSRGB());
    sk_sp<SkImage> image = SkImages::RasterFromData(info, data, 16U * 4U);
    if (!image) return 2;
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) return 3;
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.drawImage(image, 0, 0);
    if (!(*surface).notifyContentWillChange(SkSurface::kRetain_ContentChangeMode)) return 5;
    SkColorMatrix matrix{};
    matrix.setScale(0.5f, 0.25f, 0.75f, 1.0f);
    const sk_sp<SkColorFilter> filter = SkColorFilters::Matrix(matrix);
    if (!filter) return 6;
    const sk_sp<SkColorFilter> composed = (*filter).makeComposed(filter);
    const SkColor4f adjusted = (*composed).filterColor4f(SkColors::kWhite, nullptr, nullptr);
    if (adjusted.fR != 0.25f || adjusted.fG != 0.0625f ||
        adjusted.fB != 0.5625f || adjusted.fA != 1.0f) return 7;
    const SkSamplingOptions sampling{};
    const sk_sp<SkShader> imageShader = (*image).makeShader(SkTileMode::kRepeat,
                                                           SkTileMode::kMirror, sampling);
    if (!imageShader) return 8;
    const SkMatrix local = SkMatrix::Translate(2, -3);
    const sk_sp<SkShader> wrapped = (*imageShader).makeWithLocalMatrix(local);
    if (!wrapped) return 9;
    SkMatrix reported{};
    std::array<SkTileMode, 2> modes{};
    SkImage* borrowedImage = (*wrapped).isAImage(&reported, modes.data());
    if (borrowedImage != image.get() || reported != local ||
        modes[0] != SkTileMode::kRepeat || modes[1] != SkTileMode::kMirror) return 10;
    const sk_sp<SkShader> filtered = (*wrapped).makeWithColorFilter(composed);
    if (!filtered) return 11;
    SkPaint paint{};
    paint.setShader(filtered);
    canvas.drawPaint(paint);
    return 0;
}
