// The recorded paint workload for the instruction-set comparison (docs/ISA_PROFILES.md).
//
// Twelve cases draw what GUI.Forms' paint profile spends its time on: clears, opaque and
// translucent rectangle fills, antialiased rounded rectangles, strokes, two- and three-stop
// gradients, image copies, images scaled with linear and with nearest sampling, tinted A8
// masks (text-like coverage), clipped draws and antialiased paths. Each case draws into a
// fresh BGRA premultiplied surface (the format GUI.Forms renders in) and, separately, again
// with the raster pipeline forced to its high-precision stages, so the SIMD profiles'
// low-precision stages and their high-precision stages can each be compared with the scalar
// profile, which has only high precision.
//
//   stx_skia_isa_workload_tests <default.raw> <highp.raw>
//       draws every case at 320x200 and writes each case's pixels: a 32-byte name, the width
//       and height as little-endian uint32, then the rows without padding. Padding bytes of the
//       surface must stay untouched.
//   stx_skia_isa_workload_tests --benchmark
//       draws every case at 1280x800 and prints one CSV row per case and precision: the median
//       of the per-frame times in nanoseconds and the number of frames timed.

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkGradient.h"
#include "gradient_result.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

extern bool gForceHighPrecisionRasterPipeline;

namespace {

constexpr std::size_t name_bytes = 32;
constexpr std::size_t row_padding = 32;
constexpr std::uint8_t padding_value = 0xA5U;

// Shared inputs, built once per process: an opaque and a translucent 256x256 image, and an
// A8 sheet of sixteen 12x16 glyph-like coverage cells.
struct Inputs {
    sk_sp<SkImage> opaque_image;
    sk_sp<SkImage> translucent_image;
    sk_sp<SkImage> glyphs;
};

struct Frame {
    float width = 0;
    float height = 0;
};

sk_sp<SkImage> make_image(std::uint8_t alpha) {
    constexpr int side = 256;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(side) * side);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const std::uint32_t r = static_cast<std::uint32_t>((x * 7 + y * 3) & 0xFF);
            const std::uint32_t g = static_cast<std::uint32_t>((x ^ y) & 0xFF);
            const std::uint32_t b = static_cast<std::uint32_t>((y * 5 + (x >> 2)) & 0xFF);
            // Premultiplied: every channel is scaled by alpha, rounding down.
            const std::uint32_t pr = r * alpha / 255U;
            const std::uint32_t pg = g * alpha / 255U;
            const std::uint32_t pb = b * alpha / 255U;
            pixels[static_cast<std::size_t>(y) * side + static_cast<std::size_t>(x)] =
                (static_cast<std::uint32_t>(alpha) << 24) | (pr << 16) | (pg << 8) | pb;
        }
    }
    const SkImageInfo info = SkImageInfo::Make(side, side, kBGRA_8888_SkColorType,
        kPremul_SkAlphaType, SkColorSpace::MakeSRGB());
    const sk_sp<SkData> data = SkData::MakeWithCopy(pixels.data(), pixels.size() * 4U);
    sk_sp<SkImage> image = SkImages::RasterFromData(info, data, static_cast<std::size_t>(side) * 4U);
    return image;
}

sk_sp<SkImage> make_glyphs() {
    constexpr int cell_width = 12;
    constexpr int cell_height = 16;
    constexpr int cells = 16;
    constexpr int width = cell_width * cells;
    std::vector<std::uint8_t> coverage(static_cast<std::size_t>(width) * cell_height);
    for (int cell = 0; cell < cells; ++cell) {
        for (int y = 0; y < cell_height; ++y) {
            for (int x = 0; x < cell_width; ++x) {
                // A ring whose radius and thickness vary per cell, with a linear edge ramp, so
                // every coverage level from 0 to 255 occurs.
                const float dx = static_cast<float>(x) - 5.5F;
                const float dy = static_cast<float>(y) - 7.5F;
                const float radius = 3.0F + static_cast<float>(cell % 4);
                const float thickness = 1.0F + static_cast<float>(cell / 4) * 0.5F;
                const float distance = dx * dx + dy * dy;
                const float edge = (distance - radius * radius) / (radius * 2.0F);
                const float inside = thickness - (edge < 0 ? -edge : edge);
                const float clamped = std::clamp(inside, 0.0F, 1.0F);
                coverage[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(cell * cell_width + x)] =
                    static_cast<std::uint8_t>(clamped * 255.0F + 0.5F);
            }
        }
    }
    const SkImageInfo info = SkImageInfo::MakeA8(width, cell_height);
    const sk_sp<SkData> data = SkData::MakeWithCopy(coverage.data(), coverage.size());
    sk_sp<SkImage> image = SkImages::RasterFromData(info, data, static_cast<std::size_t>(width));
    return image;
}

SkColor panel_colour(int index, std::uint8_t alpha) {
    const std::uint32_t mix = static_cast<std::uint32_t>(index) * 0x9E3779B9U;
    const SkColor colour = (static_cast<std::uint32_t>(alpha) << 24) | (mix & 0x00FFFFFFU);
    return colour;
}

// The rectangle grid every fill case uses: a full-window background, twelve panels, a column
// of 37-pixel-wide buttons (the narrow rows the fill-kernel study found weakest) and a strip
// of 16x16 icons.
void fill_grid(SkCanvas& canvas, const Frame& frame, std::uint8_t alpha) {
    SkPaint paint;
    paint.setColor(panel_colour(0, alpha));
    canvas.drawRect(SkRect::MakeWH(frame.width, frame.height), paint);
    for (int i = 0; i < 12; ++i) {
        const float x = frame.width * static_cast<float>(i % 4) / 4.0F + 2.0F;
        const float y = frame.height * static_cast<float>(i / 4) / 4.0F + 2.0F;
        paint.setColor(panel_colour(i + 1, alpha));
        canvas.drawRect(SkRect::MakeXYWH(x, y, frame.width / 4.0F - 4.0F, frame.height / 4.0F - 4.0F), paint);
    }
    for (int i = 0; i < 8; ++i) {
        paint.setColor(panel_colour(i + 20, alpha));
        canvas.drawRect(SkRect::MakeXYWH(frame.width - 41.0F, static_cast<float>(i) * frame.height / 8.0F,
            37.0F, frame.height / 8.0F - 3.0F), paint);
    }
    for (int i = 0; i < 24; ++i) {
        paint.setColor(panel_colour(i + 40, alpha));
        canvas.drawRect(SkRect::MakeXYWH(4.0F + static_cast<float>(i) * 19.0F, frame.height - 20.0F, 16, 16), paint);
    }
}

void draw_clear(SkCanvas& canvas, const Frame&, const Inputs&) {
    canvas.clear(0xFF2B3036U);
}

void draw_opaque_fills(SkCanvas& canvas, const Frame& frame, const Inputs&) {
    fill_grid(canvas, frame, 0xFF);
}

void draw_translucent_fills(SkCanvas& canvas, const Frame& frame, const Inputs&) {
    canvas.clear(0xFF2B3036U);
    fill_grid(canvas, frame, 0x80);
}

void draw_rounded_rects(SkCanvas& canvas, const Frame& frame, const Inputs&) {
    canvas.clear(0xFFF0F0F0U);
    SkPaint paint;
    paint.setAntiAlias(true);
    for (int i = 0; i < 24; ++i) {
        const float x = frame.width * static_cast<float>(i % 6) / 6.0F + 3.25F;
        const float y = frame.height * static_cast<float>(i / 6) / 4.0F + 2.5F;
        SkRRect rounded;
        rounded.setRectXY(SkRect::MakeXYWH(x, y, frame.width / 6.0F - 7.5F, frame.height / 4.0F - 6.0F),
            4.0F + static_cast<float>(i % 3), 4.0F + static_cast<float>(i % 3));
        paint.setColor(panel_colour(i, i % 2 == 0 ? 0xFF : 0xB0));
        canvas.drawRRect(rounded, paint);
    }
}

void draw_strokes(SkCanvas& canvas, const Frame& frame, const Inputs&) {
    canvas.clear(0xFFFFFFFFU);
    SkPaint paint;
    paint.setStyle(SkPaint::kStroke_Style);
    for (int i = 0; i < 24; ++i) {
        const float x = frame.width * static_cast<float>(i % 6) / 6.0F + 2.5F;
        const float y = frame.height * static_cast<float>(i / 6) / 4.0F + 2.5F;
        paint.setAntiAlias(i % 2 == 1);
        paint.setStrokeWidth(i % 2 == 0 ? 0.0F : 1.5F);
        paint.setColor(panel_colour(i + 7, 0xFF));
        canvas.drawRect(SkRect::MakeXYWH(x, y, frame.width / 6.0F - 6.0F, frame.height / 4.0F - 6.0F), paint);
    }
}

void draw_gradients(SkCanvas& canvas, const Frame& frame, const Inputs&) {
    const std::array<SkColor4f, 2> two = {{{0.93F, 0.94F, 0.96F, 1}, {0.78F, 0.80F, 0.85F, 1}}};
    const std::array<float, 2> two_stops = {0, 1};
    const std::array<SkColor4f, 3> three = {{{1, 0.2F, 0.1F, 0.9F}, {0.1F, 0.8F, 0.3F, 1}, {0.2F, 0.3F, 1, 0.6F}}};
    const std::array<float, 3> three_stops = {0, 0.45F, 1};
    const SkGradient::Colors two_colours(SkSpan<const SkColor4f>(two.data(), two.size()),
        SkSpan<const float>(two_stops.data(), two_stops.size()), SkTileMode::kClamp, SkColorSpace::MakeSRGB());
    const SkGradient::Colors three_colours(SkSpan<const SkColor4f>(three.data(), three.size()),
        SkSpan<const float>(three_stops.data(), three_stops.size()), SkTileMode::kClamp, SkColorSpace::MakeSRGB());
    SkGradient::Interpolation interpolation;
    interpolation.fInPremul = SkGradient::Interpolation::InPremul::kYes;
    const SkGradient two_stop(two_colours, interpolation);
    const SkGradient three_stop(three_colours, interpolation);
    canvas.clear(0xFF2B3036U);
    SkPaint paint;
    for (int i = 0; i < 16; ++i) {
        const float x = frame.width * static_cast<float>(i % 4) / 4.0F + 2.0F;
        const float y = frame.height * static_cast<float>(i / 4) / 4.0F + 2.0F;
        const float w = frame.width / 4.0F - 4.0F;
        const float h = frame.height / 4.0F - 4.0F;
        const SkPoint points[2] = {{x, y}, {i % 2 == 0 ? x : x + w, y + h}};
        paint.setShader(shader_of(SkShaders::LinearGradient(points, i % 3 == 2 ? three_stop : two_stop)));
        canvas.drawRect(SkRect::MakeXYWH(x, y, w, h), paint);
    }
}

void draw_image_copies(SkCanvas& canvas, const Frame& frame, const Inputs& inputs) {
    canvas.clear(0xFF2B3036U);
    SkPaint translucent;
    translucent.setAlphaf(0.6F);
    for (int i = 0; i * 200 < static_cast<int>(frame.width) + 200; ++i) {
        canvas.drawImage(inputs.opaque_image.get(), static_cast<float>(i * 200 - 40), -30.0F);
        canvas.drawImage(inputs.translucent_image.get(), static_cast<float>(i * 200 - 90), frame.height / 2.0F);
        canvas.drawImage(inputs.opaque_image.get(), static_cast<float>(i * 200), frame.height / 3.0F,
            SkSamplingOptions(), &translucent);
    }
}

void draw_scaled_linear(SkCanvas& canvas, const Frame& frame, const Inputs& inputs) {
    canvas.clear(0xFF2B3036U);
    const SkSamplingOptions linear(SkFilterMode::kLinear);
    canvas.drawImageRect(inputs.opaque_image.get(),
        SkRect::MakeXYWH(3.5F, 2.25F, frame.width * 0.55F, frame.height * 0.6F), linear, nullptr);
    canvas.drawImageRect(inputs.translucent_image.get(),
        SkRect::MakeXYWH(frame.width * 0.4F, frame.height * 0.3F, frame.width * 0.57F, frame.height * 0.66F),
        linear, nullptr);
}

void draw_scaled_nearest(SkCanvas& canvas, const Frame& frame, const Inputs& inputs) {
    canvas.clear(0xFF2B3036U);
    const SkSamplingOptions nearest(SkFilterMode::kNearest);
    canvas.drawImageRect(inputs.opaque_image.get(),
        SkRect::MakeXYWH(1.0F, 1.0F, frame.width * 0.62F, frame.height * 0.7F), nearest, nullptr);
    canvas.drawImageRect(inputs.translucent_image.get(),
        SkRect::MakeXYWH(frame.width * 0.5F, frame.height * 0.25F, frame.width * 0.45F, frame.height * 0.7F),
        nearest, nullptr);
}

void draw_glyphs(SkCanvas& canvas, const Frame& frame, const Inputs& inputs) {
    canvas.clear(0xFFFFFFFFU);
    SkPaint paint;
    const int columns = static_cast<int>(frame.width) / 9;
    const int rows = static_cast<int>(frame.height) / 18;
    for (int row = 0; row < rows; ++row) {
        paint.setColor(row % 3 == 0 ? 0xFF101820U : row % 3 == 1 ? 0xFF2050C0U : 0xC0A02010U);
        for (int column = 0; column < columns; ++column) {
            const int cell = (row * 7 + column * 3) % 16;
            const SkRect source = SkRect::MakeXYWH(static_cast<float>(cell * 12), 0, 12, 16);
            const SkRect destination = SkRect::MakeXYWH(static_cast<float>(column * 9), static_cast<float>(row * 18), 12, 16);
            canvas.drawImageRect(inputs.glyphs.get(), source, destination, SkSamplingOptions(), &paint,
                SkCanvas::kStrict_SrcRectConstraint);
        }
    }
}

void draw_clipped(SkCanvas& canvas, const Frame& frame, const Inputs& inputs) {
    canvas.clear(0xFF2B3036U);
    canvas.save();
    SkRRect clip;
    clip.setRectXY(SkRect::MakeXYWH(6.5F, 5.5F, frame.width - 13.0F, frame.height - 11.0F), 14.0F, 14.0F);
    canvas.clipRRect(clip, SkClipOp::kIntersect, true);
    fill_grid(canvas, frame, 0xFF);
    draw_gradients(canvas, frame, inputs);
    canvas.restore();
}

void draw_paths(SkCanvas& canvas, const Frame& frame, const Inputs&) {
    canvas.clear(0xFFF0F0F0U);
    SkPaint paint;
    paint.setAntiAlias(true);
    for (int i = 0; i < 12; ++i) {
        const float x = frame.width * static_cast<float>(i % 4) / 4.0F;
        const float y = frame.height * static_cast<float>(i / 4) / 3.0F;
        const float w = frame.width / 4.0F;
        const float h = frame.height / 3.0F;
        SkPathBuilder path;
        path.moveTo(x + w * 0.1F, y + h * 0.8F);
        path.cubicTo(x + w * 0.2F, y - h * 0.1F, x + w * 0.9F, y + h * 0.1F, x + w * 0.85F, y + h * 0.7F);
        path.quadTo(x + w * 0.5F, y + h * 1.05F, x + w * 0.1F, y + h * 0.8F);
        path.close();
        path.addOval(SkRect::MakeXYWH(x + w * 0.3F, y + h * 0.3F, w * 0.35F, h * 0.35F));
        paint.setColor(panel_colour(i + 3, i % 2 == 0 ? 0xFF : 0x99));
        canvas.drawPath(path.detach(), paint);
    }
}

using DrawCase = void (*)(SkCanvas&, const Frame&, const Inputs&);

struct Case {
    const char* name;
    DrawCase draw;
};

constexpr std::array<Case, 12> cases = {{
    {"clear", draw_clear},
    {"opaque_fills", draw_opaque_fills},
    {"translucent_fills", draw_translucent_fills},
    {"rounded_rects", draw_rounded_rects},
    {"strokes", draw_strokes},
    {"gradients", draw_gradients},
    {"image_copies", draw_image_copies},
    {"scaled_linear", draw_scaled_linear},
    {"scaled_nearest", draw_scaled_nearest},
    {"glyph_masks", draw_glyphs},
    {"clipped", draw_clipped},
    {"paths", draw_paths},
}};

// A BGRA premultiplied surface over caller storage whose rows carry padding, so a write past a
// row's last pixel is seen.
struct Target {
    int width = 0;
    int height = 0;
    std::size_t stride = 0;
    std::vector<std::uint8_t> pixels;
    sk_sp<SkSurface> surface;
};

bool prepare(Target& target, int width, int height) {
    target.width = width;
    target.height = height;
    target.stride = static_cast<std::size_t>(width) * 4U + row_padding;
    target.pixels.assign(target.stride * static_cast<std::size_t>(height), padding_value);
    const SkImageInfo info = SkImageInfo::Make(width, height, kBGRA_8888_SkColorType,
        kPremul_SkAlphaType, SkColorSpace::MakeSRGB());
    target.surface = SkSurfaces::WrapPixels(info, target.pixels.data(), target.stride);
    return static_cast<bool>(target.surface);
}

bool padding_intact(const Target& target) {
    for (int y = 0; y < target.height; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * target.stride;
        for (std::size_t x = static_cast<std::size_t>(target.width) * 4U; x < target.stride; ++x) {
            if (target.pixels[row + x] != padding_value) return false;
        }
    }
    return true;
}

bool write_case(FILE* output, const char* name, const Target& target) {
    std::array<char, name_bytes> padded_name{};
    std::strncpy(padded_name.data(), name, name_bytes - 1U);
    const std::array<std::uint32_t, 2> size = {
        static_cast<std::uint32_t>(target.width), static_cast<std::uint32_t>(target.height)};
    if (std::fwrite(padded_name.data(), 1, name_bytes, output) != name_bytes) return false;
    // Little-endian on every host this runs on (x86-64 and arm64).
    if (std::fwrite(size.data(), sizeof(std::uint32_t), size.size(), output) != size.size()) return false;
    const std::size_t row_bytes = static_cast<std::size_t>(target.width) * 4U;
    for (int y = 0; y < target.height; ++y) {
        const std::uint8_t* row = target.pixels.data() + static_cast<std::size_t>(y) * target.stride;
        if (std::fwrite(row, 1, row_bytes, output) != row_bytes) return false;
    }
    return true;
}

int record(const char* default_path, const char* highp_path, const Inputs& inputs) {
    constexpr int width = 320;
    constexpr int height = 200;
    const Frame frame{static_cast<float>(width), static_cast<float>(height)};
    const std::array<const char*, 2> paths = {default_path, highp_path};
    for (std::size_t precision = 0; precision < paths.size(); ++precision) {
        FILE* output = std::fopen(paths[precision], "wb");
        if (output == nullptr) return 2;
        gForceHighPrecisionRasterPipeline = precision == 1U;
        for (const Case& entry : cases) {
            Target target;
            if (!prepare(target, width, height)) {
                std::fclose(output);
                return 3;
            }
            SkCanvas& canvas = *(*target.surface).getCanvas();
            entry.draw(canvas, frame, inputs);
            if (canvas.getSaveCount() != 1 || !padding_intact(target) || !write_case(output, entry.name, target)) {
                std::fclose(output);
                return 4;
            }
        }
        gForceHighPrecisionRasterPipeline = false;
        if (std::fclose(output) != 0) return 5;
    }
    std::printf("cases=%zu precisions=2 PASS\n", cases.size());
    return 0;
}

int benchmark(const Inputs& inputs) {
    constexpr int width = 1280;
    constexpr int height = 800;
    constexpr int warm_up_frames = 2;
    constexpr std::size_t minimum_frames = 5;
    constexpr double minimum_seconds = 0.2;
    const Frame frame{static_cast<float>(width), static_cast<float>(height)};
    Target target;
    if (!prepare(target, width, height)) return 3;
    SkCanvas& canvas = *(*target.surface).getCanvas();
    std::printf("case,precision,median_ns,frames\n");
    for (int precision = 0; precision < 2; ++precision) {
        gForceHighPrecisionRasterPipeline = precision == 1;
        for (const Case& entry : cases) {
            for (int i = 0; i < warm_up_frames; ++i) entry.draw(canvas, frame, inputs);
            std::vector<double> frame_ns;
            double total_seconds = 0;
            while (frame_ns.size() < minimum_frames || total_seconds < minimum_seconds) {
                const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
                entry.draw(canvas, frame, inputs);
                const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
                const double seconds = std::chrono::duration<double>(end - start).count();
                total_seconds += seconds;
                frame_ns.push_back(seconds * 1e9);
            }
            const std::size_t middle = frame_ns.size() / 2U;
            std::nth_element(frame_ns.begin(), frame_ns.begin() + static_cast<std::ptrdiff_t>(middle), frame_ns.end());
            std::printf("%s,%s,%.0f,%zu\n", entry.name, precision == 0 ? "default" : "highp",
                frame_ns[middle], frame_ns.size());
        }
    }
    gForceHighPrecisionRasterPipeline = false;
    if (canvas.getSaveCount() != 1 || !padding_intact(target)) return 4;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Inputs inputs;
    inputs.opaque_image = make_image(0xFF);
    inputs.translucent_image = make_image(0x9C);
    inputs.glyphs = make_glyphs();
    if (!inputs.opaque_image || !inputs.translucent_image || !inputs.glyphs) return 6;
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) return benchmark(inputs);
    if (argc == 3) return record(argv[1], argv[2], inputs);
    std::fprintf(stderr, "usage: stx_skia_isa_workload_tests <default.raw> <highp.raw> | --benchmark\n");
    return 2;
}
