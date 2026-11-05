#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkGraphics.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

template <typename T>
concept HasTextBlobDrawing = requires(T& canvas, const SkPaint& paint) {
    canvas.drawTextBlob(nullptr, 0.0f, 0.0f, paint);
};
template <typename T>
concept HasPictureDrawing = requires(T& canvas) {
    canvas.drawPicture(nullptr);
};
template <typename T>
concept HasFontCache = requires {
    T::GetFontCacheUsed();
    T::PurgeFontCache();
};
#ifdef STX_SKIA_REFERENCE
static_assert(HasTextBlobDrawing<SkCanvas>);
static_assert(HasPictureDrawing<SkCanvas>);
static_assert(HasFontCache<SkGraphics>);
#else
static_assert(!HasTextBlobDrawing<SkCanvas>);
static_assert(!HasPictureDrawing<SkCanvas>);
static_assert(!HasFontCache<SkGraphics>);
#endif

namespace {
constexpr int maskWidth = 16;
constexpr int maskHeight = 16;
constexpr std::size_t maskStride = 19;
constexpr int outputWidth = 23;
constexpr int outputHeight = 25;
constexpr std::size_t outputStride = outputWidth * 4U + 12U;

// All 256 coverages, with source and destination padding. The mask owns a copy;
// the surface borrows the destination only while this function is active.
bool check_mask(SkColor color, bool clipped) {
    std::array<uint8_t, maskStride * maskHeight> coverage{};
    coverage.fill(0xad);
    for (int y = 0; y < maskHeight; ++y) {
        for (int x = 0; x < maskWidth; ++x) {
            coverage[static_cast<std::size_t>(y) * maskStride + x] =
                static_cast<uint8_t>(y * maskWidth + x);
        }
    }
    const SkImageInfo maskInfo = SkImageInfo::MakeA8(maskWidth, maskHeight);
    sk_sp<SkData> maskData = SkData::MakeWithCopy(coverage.data(), coverage.size());
    sk_sp<SkImage> mask = SkImages::RasterFromData(maskInfo, maskData, maskStride);
    if (!mask) return false;

    std::array<uint8_t, outputStride * outputHeight> output{};
    output.fill(0xa7);
    const SkImageInfo outputInfo = SkImageInfo::Make(outputWidth, outputHeight,
        kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(outputInfo, output.data(), outputStride);
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.clear(SK_ColorTRANSPARENT);
    if (clipped) canvas.clipRect(SkRect::MakeXYWH(7, 8, 9, 10));
    SkPaint paint;
    paint.setColor(color);
    canvas.drawImage(mask, 3.0f, 4.0f, SkSamplingOptions{}, &paint);

    for (int y = 0; y < outputHeight; ++y) {
        for (int x = 0; x < outputWidth; ++x) {
            bool covered = x >= 3 && x < 19 && y >= 4 && y < 20;
            if (clipped) covered = covered && x >= 7 && x < 16 && y >= 8 && y < 18;
            const uint8_t alpha = covered ? static_cast<uint8_t>((y - 4) * 16 + x - 3) : 0;
            const std::array<uint8_t, 4> expected = {
                static_cast<uint8_t>(SkColorGetR(color) == 255 ? alpha : 0),
                static_cast<uint8_t>(SkColorGetG(color) == 255 ? alpha : 0),
                static_cast<uint8_t>(SkColorGetB(color) == 255 ? alpha : 0), alpha};
            const std::size_t offset = static_cast<std::size_t>(y) * outputStride + x * 4U;
            for (std::size_t channel = 0; channel < expected.size(); ++channel) {
                if (output[offset + channel] != expected[channel]) return false;
            }
        }
        for (std::size_t byte = outputWidth * 4U; byte < outputStride; ++byte) {
            if (output[static_cast<std::size_t>(y) * outputStride + byte] != 0xa7) return false;
        }
    }
    return true;
}
}

int main() {
    SkGraphics::Init();
    SkGraphics::Init();
    const std::array<SkColor, 4> colors = {SK_ColorRED, SK_ColorGREEN, SK_ColorBLUE, SK_ColorWHITE};
    for (SkColor color : colors) {
        if (!check_mask(color, false) || !check_mask(color, true)) return 1;
    }
    SkGraphics::PurgeAllCaches();
    std::printf("external glyph masks: 8 scenes, 256 coverage levels; canvas %zu bytes; PASS\n", sizeof(SkCanvas));
    return 0;
}
