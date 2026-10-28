#include "include/core/SkBlurTypes.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkGraphics.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkShader.h"
#include "include/effects/SkShaderMaskFilter.h"
#include "include/effects/SkGradient.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRegion.h"
#include "include/core/SkSurface.h"
#include "src/core/SkMaskFilterBase.h"
#include "src/core/SkCachedData.h"
#include "include/private/SkMalloc.h"
#include "gradient_result.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <vector>
#include <utility>

#ifndef STX_SKIA_REFERENCE
template <typename T>
concept BorrowsMaskFilter = requires(T&& value) { as_MFB(std::forward<T>(value)); };
static_assert(BorrowsMaskFilter<sk_sp<SkMaskFilter>&>);
static_assert(!BorrowsMaskFilter<sk_sp<SkMaskFilter>>);
#endif

namespace {
struct CloseFile {
    void operator()(std::FILE* file) const { if (file != nullptr) std::fclose(file); }
};

class PatchFilter final : public SkMaskFilterBase {
public:
    SkMask::Format getFormat() const override { return SkMask::kA8_Format; }
    Type type() const override { return Type::kShader; }
    Factory getFactory() const override { return nullptr; }
    const char* getTypeName() const override { return "test-patch"; }
    bool filterMask(SkMaskBuilder* dst, const SkMask& src, const SkMatrix&, SkIPoint* margin) const override {
        if (src.fImage != nullptr) return false;
        *dst = SkMaskBuilder(nullptr, src.fBounds, static_cast<uint32_t>(src.fBounds.width()), SkMask::kA8_Format);
        if (margin != nullptr) *margin = {0, 0};
        return true;
    }
    static bool ownership();
protected:
    FilterReturn filterRectsToNine(SkSpan<const SkRect> rectangles, const SkMatrix&,
                                   const SkIRect&, std::optional<NinePatch>* patch,
                                   SkResourceCache*) const override {
        if (rectangles.size() != 1) return FilterReturn::kUnimplemented;
        const SkIRect bounds = rectangles[0].round();
        if (bounds.width() < 3 || bounds.height() < 3) return FilterReturn::kUnimplemented;
        SkAutoMaskFreeImage pixels(SkMaskBuilder::AllocImage(9));
        const std::array<uint8_t, 9> values = {0, 64, 128, 192, 255, 96, 32, 160, 224};
        for (size_t i = 0; i < values.size(); ++i) pixels.get()[i] = values[i];
        const SkMask mask(pixels.release(), SkIRect::MakeWH(3, 3), 3, SkMask::kA8_Format);
        (*patch).emplace(mask, bounds, SkIPoint{1, 1}, nullptr);
        return FilterReturn::kTrue;
    }
};

#ifndef STX_SKIA_REFERENCE
class FailingFilter final : public SkMaskFilterBase {
public:
    explicit FailingFilter(int& attempts) : fAttempts(attempts) {}
    SkMask::Format getFormat() const override { return SkMask::kA8_Format; }
    Type type() const override { return Type::kShader; }
    Factory getFactory() const override { return nullptr; }
    const char* getTypeName() const override { return "test-failure"; }
    bool filterMask(SkMaskBuilder* dst, const SkMask& src, const SkMatrix&, SkIPoint* margin) const override {
        *dst = SkMaskBuilder(nullptr, src.fBounds, static_cast<uint32_t>(src.fBounds.width()), SkMask::kA8_Format);
        if (margin != nullptr) *margin = {0, 0};
        if (src.fImage == nullptr) return true;
        (*dst).image() = SkMaskBuilder::AllocImage((*dst).computeImageSize());
        ++fAttempts;
        return false; // The caller must retain and release this failed result.
    }
private:
    int& fAttempts;
};

class OversizedFilter final : public SkMaskFilterBase {
public:
    explicit OversizedFilter(bool afterPixels) : fAfterPixels(afterPixels) {}
    SkMask::Format getFormat() const override { return SkMask::kA8_Format; }
    Type type() const override { return Type::kShader; }
    Factory getFactory() const override { return nullptr; }
    const char* getTypeName() const override { return "test-oversized-bounds"; }
    bool filterMask(SkMaskBuilder* dst, const SkMask& src, const SkMatrix&,
                    SkIPoint*) const override {
        if (src.fImage == nullptr && fAfterPixels) {
            *dst = SkMaskBuilder(nullptr, src.fBounds, src.fRowBytes, SkMask::kA8_Format);
            return true;
        }
        uint8_t* pixels = nullptr;
        if (src.fImage != nullptr) {
            pixels = SkMaskBuilder::AllocImage(1);
            pixels[0] = 0;
        }
        const SkIRect bounds = SkIRect::MakeLTRB(0, 0, std::numeric_limits<int>::max() - 1, 1);
        *dst = SkMaskBuilder(pixels, bounds, 1, SkMask::kA8_Format);
        return true;
    }
private:
    bool fAfterPixels;
};

class PaddedAlphaFilter final : public SkMaskFilterBase {
public:
    SkMask::Format getFormat() const override { return SkMask::kA8_Format; }
    Type type() const override { return Type::kShader; }
    Factory getFactory() const override { return nullptr; }
    const char* getTypeName() const override { return "test-padded-alpha"; }
    bool filterMask(SkMaskBuilder* dst, const SkMask& src, const SkMatrix&,
                    SkIPoint* margin) const override {
        const int width = src.fBounds.width();
        const int height = src.fBounds.height();
        if (width < 1 || width > 3 || height != 2) return false;
        const uint32_t stride = 4;
        *dst = SkMaskBuilder(nullptr, src.fBounds, stride, SkMask::kA8_Format);
        if (margin != nullptr) *margin = {0, 0};
        if (src.fImage == nullptr) return true;
        (*dst).image() = SkMaskBuilder::AllocImage((*dst).computeImageSize());
        for (int y = 0; y < height; ++y) {
            uint8_t* output = (*dst).image() + static_cast<size_t>(y) * stride;
            const uint8_t* input = src.fImage + static_cast<size_t>(y) * src.fRowBytes;
            for (uint32_t x = 0; x < stride; ++x) output[x] = 0xa5;
            for (int x = 0; x < width; ++x) output[x] = input[x];
        }
        return true;
    }
};

bool padded_alpha_extent() {
    for (const int width : {1, 2, 3}) {
        SkBitmap source;
        if (!source.tryAllocPixels(SkImageInfo::MakeA8(width, 2))) return false;
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < width; ++x) {
                *source.getAddr8(x, y) = static_cast<uint8_t>(20 + y * 10 + x);
            }
        }
        SkPaint paint;
        paint.setMaskFilter(sk_sp<SkMaskFilter>(new PaddedAlphaFilter));
        SkBitmap extracted;
        if (!source.extractAlpha(&extracted, &paint, nullptr, nullptr)) return false;
        if (extracted.width() != width || extracted.height() != 2 || extracted.rowBytes() != 4) {
            return false;
        }
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < width; ++x) {
                if (*extracted.getAddr8(x, y) != *source.getAddr8(x, y)) return false;
            }
        }
    }
    return true;
}

bool oversized_filter_bounds() {
    SkBitmap source;
    if (!source.tryAllocPixels(SkImageInfo::MakeA8(1, 1))) return false;
    *source.getAddr8(0, 0) = 42;
    for (const bool afterPixels : {false, true}) {
        SkPaint paint;
        paint.setMaskFilter(sk_sp<SkMaskFilter>(new OversizedFilter(afterPixels)));
        SkBitmap output;
        if (source.extractAlpha(&output, &paint, nullptr, nullptr) || !output.isNull()) {
            return false;
        }
    }
    return true;
}

bool failed_filter_cleanup() {
    int attempts = 0;
    std::array<uint8_t, 32 * 32> pixels{};
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(SkImageInfo::MakeA8(32, 32), pixels.data(), 32);
    if (!surface) return false;
    SkPaint paint;
    paint.setMaskFilter(sk_sp<SkMaskFilter>(new FailingFilter(attempts)));
    SkPathBuilder triangle;
    triangle.moveTo(4, 4);
    triangle.lineTo(24, 8);
    triangle.lineTo(10, 25);
    triangle.close();
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.drawPath(triangle.detach(), paint);
    if (attempts != 1) return false;

    SkBitmap source;
    if (!source.tryAllocPixels(SkImageInfo::MakeA8(2, 2))) return false;
    source.eraseColor(SK_ColorWHITE);
    SkBitmap extracted;
    if (!source.extractAlpha(&extracted, &paint, nullptr, nullptr)) return false;
    return attempts == 2 && extracted.width() == 2 && extracted.height() == 2;
}
#endif

class CacheAllocation final : public SkCachedData {
public:
    CacheAllocation(void* pixels, int& destroyed) : SkCachedData(pixels, 9), fDestroyed(destroyed) {}
    ~CacheAllocation() override { ++fDestroyed; }
private:
    int& fDestroyed;
};

bool PatchFilter::ownership() {
    int destroyed = 0;
    {
        void* pixels = sk_malloc_throw(9);
        sk_sp<SkCachedData> cache(new CacheAllocation(pixels, destroyed));
        const SkMask mask(static_cast<const uint8_t*>(pixels), SkIRect::MakeWH(3, 3), 3, SkMask::kA8_Format);
        std::optional<NinePatch> patch;
        patch.emplace(mask, SkIRect::MakeWH(20, 20), SkIPoint{1, 1}, cache.release());
        if (destroyed != 0 || (*patch).fMask.fImage != pixels) return false;
    }
    if (destroyed != 1) return false;
    {
        SkAutoMaskFreeImage pixels(SkMaskBuilder::AllocImage(9));
        const SkMask mask(pixels.release(), SkIRect::MakeWH(3, 3), 3, SkMask::kA8_Format);
        std::optional<NinePatch> patch;
        patch.emplace(mask, SkIRect::MakeWH(20, 20), SkIPoint{1, 1}, nullptr);
    }
    return true;
}

bool patch_pixels(int width, bool sides_only) {
    constexpr int height = 5;
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height, 0);
    const SkImageInfo info = SkImageInfo::MakeA8(width, height);
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels.data(), static_cast<size_t>(width));
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    if (sides_only) canvas.clipRect(SkRect::MakeXYWH(0, 1, static_cast<float>(width), 3));
    SkPaint paint;
    paint.setColor(SK_ColorWHITE);
    paint.setMaskFilter(sk_sp<SkMaskFilter>(new PatchFilter));
    canvas.drawRect(SkRect::MakeWH(static_cast<float>(width), height), paint);
    const std::array<uint8_t, 9> values = {0, 64, 128, 192, 255, 96, 32, 160, 224};
    for (int y = 0; y < height; ++y) {
        const int source_y = y == 0 ? 0 : (y == height - 1 ? 2 : 1);
        for (int x = 0; x < width; ++x) {
            const int source_x = x == 0 ? 0 : (x == width - 1 ? 2 : 1);
            uint8_t expected = values[static_cast<size_t>(source_y * 3 + source_x)];
            if (sides_only && (y == 0 || y == height - 1)) expected = 0;
            if (pixels[static_cast<size_t>(y) * width + x] != expected) {
                std::printf("patch mismatch width=%d x=%d y=%d actual=%u expected=%u\n", width, x, y,
                    pixels[static_cast<size_t>(y) * width + x], expected);
                return false;
            }
        }
    }
    return true;
}

bool shader_masks(std::FILE* file) {
    if (SkShaderMaskFilter::Make(nullptr)) return false;
    const std::array<float, 3> alphas = {0, 0.5f, 1};
    uint64_t hash = 1469598103934665603ULL;
    int cases = 0;
    for (int kind = 0; kind < 4; ++kind) {
        sk_sp<SkShader> shader;
        if (kind < 3) {
            shader = SkShaders::Color(SkColor4f{0.25f, 0.5f, 0.75f, alphas[static_cast<size_t>(kind)]}, nullptr);
        } else {
            const std::array<SkColor4f, 2> colors = {{{1, 0, 0, 0.1f}, {0, 1, 0, 0.9f}}};
            const SkGradient::Colors gradientColors(SkSpan<const SkColor4f>(colors.data(), colors.size()),
                SkSpan<const float>(), SkTileMode::kMirror, nullptr);
            const SkGradient gradient(gradientColors, SkGradient::Interpolation());
            const SkPoint points[2] = {{-3, 1}, {13, 11}};
            shader = shader_of(SkShaders::LinearGradient(points, gradient));
        }
        sk_sp<SkMaskFilter> owner = SkShaderMaskFilter::Make(std::move(shader));
        if (!owner) return false;
        const SkMaskFilterBase& filter = *as_MFB(owner);
        for (const int width : {1, 7, 16, 33}) {
            for (const int height : {1, 7}) {
                for (const int origin : {-3, 0, 5}) {
                    for (const int padding : {0, 13}) {
                        const size_t stride = static_cast<size_t>(width + padding);
#ifdef STX_SKIA_REFERENCE
                        const size_t extent = static_cast<size_t>(height) * stride;
#else
                        const size_t extent = static_cast<size_t>(height - 1) * stride + width;
#endif
                        std::vector<uint8_t> pixels(extent);
                        for (size_t i = 0; i < extent; ++i) pixels[i] = static_cast<uint8_t>(i * 79 + 23);
                        const SkIRect bounds = SkIRect::MakeXYWH(origin, -origin, width, height);
                        const SkMask input(pixels.data(), bounds, static_cast<uint32_t>(stride), SkMask::kA8_Format);
                        for (int transform = 0; transform < 3; ++transform) {
                            SkMatrix matrix = SkMatrix::I();
                            if (transform == 1) matrix.setScaleTranslate(0.8f, 1.2f, 2.5f, -1.25f);
                            if (transform == 2) matrix.setRotate(21);
                            SkAutoMaskFreeImage outputOwner;
                            SkMaskBuilder output;
                            SkIPoint margin = {9, 9};
                            const bool ok = filter.filterMask(&output, input, matrix, &margin);
                            outputOwner.reset(output.image());
                            if (!ok || !outputOwner || output.fBounds != bounds || output.fRowBytes != static_cast<uint32_t>(width) ||
                                margin.x() != 0 || margin.y() != 0) return false;
                            const size_t bytes = output.computeImageSize();
                            for (int y = 0; y < height; ++y) {
                                for (int x = 0; x < width; ++x) {
                                    const uint8_t actual = output.fImage[static_cast<size_t>(y) * width + x];
                                    if (kind == 0 && actual != 0) return false;
                                    if (kind == 2 && actual != pixels[static_cast<size_t>(y) * stride + x]) return false;
                                    hash ^= actual;
                                    hash *= 1099511628211ULL;
                                }
                            }
                            if (file != nullptr && std::fwrite(output.fImage, 1, bytes, file) != bytes) return false;
                            ++cases;
                        }
                    }
                }
            }
        }
        SkMaskBuilder metadata;
        const SkMask source(nullptr, SkIRect::MakeXYWH(-3, 2, 7, 5), 0, SkMask::kA8_Format);
        if (!filter.filterMask(&metadata, source, SkMatrix::I(), nullptr) || metadata.fImage != nullptr ||
            metadata.fBounds != source.fBounds || metadata.fRowBytes != 7) return false;
#ifndef STX_SKIA_REFERENCE
        const std::array<uint8_t, 16> small{};
        const SkMask invalid(small.data(), SkIRect::MakeWH(4, 2), 2, SkMask::kA8_Format);
        SkIPoint margin = {9, 9};
        if (filter.filterMask(&metadata, invalid, SkMatrix::I(), &margin) || margin != SkIPoint{9, 9} ||
            metadata.fBounds != source.fBounds || metadata.fImage != nullptr) return false;
        if (filter.filterMask(&metadata, metadata, SkMatrix::I(), nullptr)) return false;
#endif
    }
    std::printf("shader masks: %d cases hash=%llu PASS\n", cases, static_cast<unsigned long long>(hash));
    return true;
}

void draw_shape(SkCanvas& canvas, const SkPaint& paint, int shape) {
    const SkRect rectangle = SkRect::MakeXYWH(19.25f, 17.5f, 149, 103);
    switch (shape) {
        case 0:
            canvas.drawRect(rectangle, paint);
            break;
        case 1: {
            SkRRect rounded;
            rounded.setRectXY(rectangle, 12, 8);
            canvas.drawRRect(rounded, paint);
        } break;
        case 2: {
            SkPathBuilder ring(SkPathFillType::kEvenOdd);
            ring.addRect(rectangle);
            ring.addRect(SkRect::MakeXYWH(28, 26, 125, 84));
            canvas.drawPath(ring.detach(), paint);
        } break;
        case 3: {
            SkPathBuilder curve;
            curve.moveTo(15, 20);
            curve.cubicTo(90, 0, 190, 125, 20, 132);
            curve.lineTo(45, 77);
            curve.close();
            canvas.drawPath(curve.detach(), paint);
        } break;
        case 5: {
            const SkVector radii[4] = {{4, 11}, {19, 6}, {7, 23}, {14, 9}};
            SkRRect rounded;
            rounded.setRectRadii(rectangle, radii);
            canvas.drawRRect(rounded, paint);
        } break;
        default:
            canvas.drawOval(rectangle, paint);
            break;
    }
}

bool rendering(std::FILE* file) {
#ifndef STX_SKIA_REFERENCE
    if (SkMaskFilter::MakeBlur(static_cast<SkBlurStyle>(-1), 2.0f) ||
        SkMaskFilter::MakeBlur(static_cast<SkBlurStyle>(4), 2.0f)) return false;
#endif
    constexpr int width = 211;
    constexpr int height = 157;
    constexpr size_t stride = width * 4 + 12;
    uint64_t hash = 1469598103934665603ULL;
    int cases = 0;
    for (const SkBlurStyle style : {kNormal_SkBlurStyle, kSolid_SkBlurStyle, kOuter_SkBlurStyle, kInner_SkBlurStyle}) {
        for (const float sigma : {0.5f, 3.0f, 9.0f}) {
            for (int shape = 0; shape < 6; ++shape) {
                for (int clip = 0; clip < 4; ++clip) {
                    std::vector<uint8_t> pixels(stride * height, 0xA5);
                    const SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
                    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels.data(), stride);
                    if (!surface) return false;
                    SkCanvas& canvas = *(*surface).getCanvas();
                    canvas.clear(0x30201008);
                    if (clip == 1) canvas.clipRect(SkRect::MakeXYWH(0, 40, 210, 30));
                    if (clip == 2) {
                        SkRegion region(SkIRect::MakeXYWH(10, 10, 25, 130));
                        region.op(SkIRect::MakeXYWH(140, 5, 35, 130), SkRegion::kUnion_Op);
                        canvas.clipRegion(region);
                    }
                    if (clip == 3) {
                        canvas.clipRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(12, 8, 170, 130), 11, 7), SkClipOp::kIntersect, true);
                        canvas.translate(1.25f, 2.5f);
                        canvas.scale(0.9f, 1.1f);
                    }
                    SkPaint paint;
                    paint.setColor(0xB0D08030);
                    paint.setAntiAlias(true);
                    paint.setMaskFilter(SkMaskFilter::MakeBlur(style, sigma, clip != 2));
                    // Repetition exercises retained cache hits as well as first creation.
                    draw_shape(canvas, paint, shape);
                    draw_shape(canvas, paint, shape);
                    for (int y = 0; y < height; ++y) {
                        for (size_t x = width * 4; x < stride; ++x) {
                            if (pixels[static_cast<size_t>(y) * stride + x] != 0xA5) return false;
                        }
                    }
                    for (const uint8_t value : pixels) { hash ^= value; hash *= 1099511628211ULL; }
                    if (file != nullptr && std::fwrite(pixels.data(), 1, pixels.size(), file) != pixels.size()) return false;
                    ++cases;
                }
            }
        }
    }
    std::printf("mask filters: %d render cases hash=%llu PASS\n", cases, static_cast<unsigned long long>(hash));
    return true;
}
}

int main(int argc, char** argv) {
    std::unique_ptr<std::FILE, CloseFile> file;
    if (argc == 2) {
        file.reset(std::fopen(argv[1], "wb"));
        if (!file) return 2;
    }
    if (!PatchFilter::ownership() || !patch_pixels(33, false) || !patch_pixels(33, true)) return 3;
#ifndef STX_SKIA_REFERENCE
    if (!failed_filter_cleanup()) return 6;
    if (!padded_alpha_extent()) return 7;
    if (!oversized_filter_bounds()) return 8;
    for (const int width : {32766, 32767, 32768, 50000}) {
        if (!patch_pixels(width, false) || !patch_pixels(width, true)) return 4;
    }
#endif
    if (!rendering(file.get()) || !shader_masks(file.get())) return 5;
    SkGraphics::PurgeAllCaches();
    return 0;
}
