#include "include/core/SkCanvas.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSurface.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

namespace {
struct CloseFile {
    void operator()(std::FILE* file) const { if (file != nullptr) std::fclose(file); }
};

bool group_opacity() {
    std::array<uint32_t, 16 * 16> pixels{};
    const SkImageInfo info = SkImageInfo::MakeN32Premul(16, 16);
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels.data(), 16 * sizeof(uint32_t));
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.clear(SK_ColorBLACK);
    canvas.saveLayerAlpha(nullptr, 128);
    SkPaint paint;
    paint.setColor(SK_ColorWHITE);
    canvas.drawRect(SkRect::MakeXYWH(2, 2, 10, 10), paint);
    canvas.drawRect(SkRect::MakeXYWH(5, 5, 10, 10), paint);
    canvas.restore();
    // Overlap is shaded once by the group alpha, not once per child.
    if (pixels[3 * 16 + 3] != 0xff808080U || pixels[7 * 16 + 7] != 0xff808080U ||
        pixels[14 * 16 + 14] != 0xff808080U || pixels[0] != 0xff000000U) return false;
    canvas.clear(SK_ColorBLACK);
    canvas.skew(0.2f, 0.1f);
    const SkRect reversed = SkRect::MakeLTRB(10, 10, 2, 2);
    canvas.saveLayer(&reversed, nullptr);
    canvas.drawPaint(paint);
    canvas.restore();
    for (const uint32_t pixel : pixels) if (pixel != 0xff000000U) return false;
    return true;
}

bool scenes(std::FILE* file) {
    constexpr int width = 97;
    constexpr int height = 73;
    constexpr size_t stride = width * 4 + 12;
    alignas(uint32_t) std::array<uint8_t, stride * height> pixels{};
    uint64_t hash = 1469598103934665603ULL;
    int cases = 0;
    for (int transform = 0; transform < 4; ++transform) {
        for (int flags = 0; flags < 4; ++flags) {
            for (int effect = 0; effect < 4; ++effect) {
                for (int extent = 0; extent < 4; ++extent) {
                    pixels.fill(0x5a);
                    const SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
                    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels.data(), stride);
                    if (!surface) return false;
                    SkCanvas& canvas = *(*surface).getCanvas();
                    canvas.clear(0xff12345aU);
                    SkPaint base;
                    base.setColor(0xffb34821U);
                    canvas.drawOval(SkRect::MakeXYWH(8, 12, 78, 52), base);
                    canvas.save();
                    canvas.clipRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(3, 4, 90, 65), 6, 9), true);
                    if (transform == 1) { canvas.translate(7, -3); canvas.rotate(13); }
                    if (transform == 2) { canvas.scale(0.8f, 1.2f); canvas.skew(0.2f, -0.1f); }
                    if (transform == 3) { canvas.translate(90, 0); canvas.scale(-1, 1); }
                    SkPaint restore;
                    restore.setAlphaf(0.61f);
                    if (effect == 1) restore.setBlendMode(SkBlendMode::kSrc);
                    if (effect == 2) restore.setBlendMode(SkBlendMode::kMultiply);
                    if (effect == 3) {
                        const float matrix[20] = {1, 0, 0, 0, .13f, 0, .7f, 0, 0, .09f,
                                                 0, 0, .8f, 0, .04f, 0, 0, 0, .6f, .2f};
                        restore.setColorFilter(SkColorFilters::Matrix(matrix));
                    }
                    const std::array<SkRect, 3> bounds = {{SkRect::MakeXYWH(8.25f, 7.5f, 60, 45),
                        SkRect::MakeXYWH(110, 100, 20, 20), SkRect::MakeEmpty()}};
                    const SkRect* selected = extent == 0 ? nullptr : &bounds[static_cast<size_t>(extent - 1)];
                    SkCanvas::SaveLayerFlags options = 0;
                    if ((flags & 1) != 0) options |= SkCanvas::kInitWithPrevious_SaveLayerFlag;
                    if ((flags & 2) != 0) options |= SkCanvas::kF16ColorType;
                    const int before = canvas.getSaveCount();
                    const SkCanvas::SaveLayerRec layer(selected, &restore, options);
                    canvas.saveLayer(layer);
                    base.setAntiAlias(true);
                    base.setColor(0xc0f08020U);
                    canvas.drawRect(SkRect::MakeXYWH(5, 5, 55, 38), base);
                    canvas.saveLayerAlpha(nullptr, 103);
                    base.setColor(0xff30d080U);
                    canvas.drawOval(SkRect::MakeXYWH(20, 15, 56, 41), base);
                    canvas.drawOval(SkRect::MakeXYWH(40, 23, 37, 42), base);
                    canvas.restore();
                    canvas.restore();
                    if (canvas.getSaveCount() != before) return false;
                    canvas.restore();
                    for (size_t i = 0; i < pixels.size(); ++i) { hash ^= pixels[i]; hash *= 1099511628211ULL; }
                    if (file != nullptr && std::fwrite(pixels.data(), 1, pixels.size(), file) != pixels.size()) return false;
                    ++cases;
                }
            }
        }
    }
    std::printf("layers: %d cases hash=%llu PASS\n", cases, static_cast<unsigned long long>(hash));
    return true;
}
}

int main(int argc, char** argv) {
    std::unique_ptr<std::FILE, CloseFile> file;
    if (argc > 1) {
        file.reset(std::fopen(argv[1], "wb"));
        if (!file) return 1;
    }
    const bool correct = group_opacity() && scenes(file.get());
    return correct ? 0 : 1;
}
