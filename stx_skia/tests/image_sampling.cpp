#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkShader.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTileMode.h"
#include "src/core/SkConvertPixels.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
constexpr int outputWidth = 17;
constexpr int outputHeight = 13;
using OutputPixels = std::array<uint32_t, outputWidth * outputHeight>;

struct Recording {
    FILE* file = nullptr;
    FILE* correctedFile = nullptr;
    size_t cases = 0;
    size_t correctedCases = 0;
    uint64_t hash = 1469598103934665603ULL;
    bool append(const OutputPixels& pixels, bool corrected) {
        FILE* destination = corrected ? correctedFile : file;
        if (destination != nullptr) {
            const size_t written = std::fwrite(pixels.data(), sizeof(uint32_t), pixels.size(), destination);
            if (written != pixels.size()) return false;
        }
        for (const uint32_t pixel : pixels) {
            hash ^= pixel;
            hash *= 1099511628211ULL;
        }
        ++cases;
        if (corrected) ++correctedCases;
        return true;
    }
};

sk_sp<SkImage> image_for(int width, int height, SkColorType type, SkAlphaType requestedAlpha) {
    SkAlphaType alpha = kUnknown_SkAlphaType;
    if (!SkColorTypeValidateAlphaType(type, requestedAlpha, &alpha)) return nullptr;
    const sk_sp<SkColorSpace> srgb = SkColorSpace::MakeSRGB();
    const SkImageInfo inputInfo = SkImageInfo::Make(width, height, kRGBA_F32_SkColorType,
                                                   kUnpremul_SkAlphaType, srgb);
    std::vector<float> input(static_cast<size_t>(width * height * 4), 0.0f);
    for (size_t pixel = 0; pixel < input.size() / 4; ++pixel) {
        input[pixel * 4] = static_cast<float>((pixel * 31 + 7) % 257) / 256.0f;
        input[pixel * 4 + 1] = static_cast<float>((pixel * 71 + 3) % 257) / 256.0f;
        input[pixel * 4 + 2] = static_cast<float>((pixel * 131 + 5) % 257) / 256.0f;
        input[pixel * 4 + 3] = static_cast<float>(pixel % 9) / 8.0f;
    }
    const SkImageInfo info = SkImageInfo::Make(width, height, type, alpha, srgb);
    const size_t rowBytes = info.minRowBytes() + static_cast<size_t>(info.bytesPerPixel()) * 3;
    const size_t extent = info.computeByteSize(rowBytes);
    std::vector<uint64_t> converted((extent + 7) / 8, 0);
    if (!SkConvertPixels(info, converted.data(), rowBytes, inputInfo, input.data(),
                         inputInfo.minRowBytes())) return nullptr;
    const SkPixmap pixels(info, converted.data(), rowBytes);
    sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(pixels);
    return image;
}

bool draw(const sk_sp<SkShader>& shader, size_t variant, OutputPixels& pixels) {
    pixels.fill(0x00000000U);
    const sk_sp<SkColorSpace> space = (variant & 1) != 0 ? SkColorSpace::MakeSRGBLinear()
                                                        : SkColorSpace::MakeSRGB();
    const SkImageInfo info = SkImageInfo::Make(outputWidth, outputHeight, kRGBA_8888_SkColorType,
                                              kPremul_SkAlphaType, space);
    const sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, pixels.data(), outputWidth * 4);
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    SkPaint paint{};
    paint.setColor(0x9f397bd2U);
    paint.setBlendMode(SkBlendMode::kSrc);
    paint.setShader(shader);
    canvas.drawPaint(paint);
    return true;
}

bool corpus(Recording& output) {
    struct Dimensions { int width; int height; SkAlphaType alpha; };
    const std::array<Dimensions, 3> shapes{{{9, 7, kPremul_SkAlphaType},
                                          {1, 7, kUnpremul_SkAlphaType},
                                          {9, 1, kOpaque_SkAlphaType}}};
    const std::array<SkTileMode, 4> modes{SkTileMode::kClamp, SkTileMode::kRepeat,
                                         SkTileMode::kMirror, SkTileMode::kDecal};
    const std::array<SkSamplingOptions, 7> options{
        SkSamplingOptions(SkFilterMode::kNearest),
        SkSamplingOptions(SkFilterMode::kLinear),
        SkSamplingOptions(SkFilterMode::kNearest, SkMipmapMode::kNearest),
        SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear),
        SkSamplingOptions(SkCubicResampler::Mitchell()),
        SkSamplingOptions(SkCubicResampler::CatmullRom()),
        SkSamplingOptions::Aniso(4)};
    for (int format = 1; format <= kLastEnum_SkColorType; ++format) {
        for (const Dimensions& shape : shapes) {
            const SkColorType type = static_cast<SkColorType>(format);
            sk_sp<SkImage> image = image_for(shape.width, shape.height, type, shape.alpha);
            if (!image) { std::printf("image creation failed %d\n", format); return false; }
            for (const SkTileMode modeX : modes) {
                for (const SkTileMode modeY : modes) {
                    for (size_t sample = 0; sample < options.size(); ++sample) {
                        const SkSamplingOptions& sampling = options[sample];
                        // The original mip generator has documented RGB10A2 alpha and RG16
                        // channel defects. Retain these renders separately from equality
                        // evidence; mipmap_contract checks their corrected weighted sums.
                        const bool correctedFormat = type == kRGBA_1010102_SkColorType ||
                                type == kBGRA_1010102_SkColorType || type == kR16G16_unorm_SkColorType;
                        const bool corrected = correctedFormat &&
                                (sampling.mipmap != SkMipmapMode::kNone || sampling.isAniso());
                        SkMatrix local{};
                        // Alternate half-pixel snapping, minification, reflected sampling,
                        // and perspective. The one-pixel axes exercise tile simplification.
                        switch (sample % 4) {
                            case 0: local.setTranslate(0.5f, -0.5f); break;
                            case 1: local.setScaleTranslate(-1.25f, 0.75f, 7.5f, -1.25f); break;
                            case 2: local.setScaleTranslate(0.375f, 0.5f, 3.25f, 1.75f); break;
                            default:
                                local.setSkew(0.25f, -0.125f);
                                local.setPerspX(0.015625f);
                                break;
                        }
                        const sk_sp<SkShader> shader = (*image).makeShader(modeX, modeY, sampling, local);
                        if (!shader) return false;
                        OutputPixels pixels{};
                        if (!draw(shader, sample, pixels) || !output.append(pixels, corrected)) return false;
                        const sk_sp<SkShader> raw = (*image).makeRawShader(modeX, modeY, sampling, &local);
                        if (sampling.useCubic) {
                            if (raw) return false;
                        } else {
                            if (!raw || !draw(raw, sample, pixels) || !output.append(pixels, corrected)) return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}
}

int main(int argc, char** argv) {
    if (argc != 1 && argc != 3) return 2;
    FILE* file = argc == 3 ? std::fopen(argv[1], "wb") : nullptr;
    if (argc == 3 && file == nullptr) return 2;
    FILE* corrected = argc == 3 ? std::fopen(argv[2], "wb") : nullptr;
    if (argc == 3 && corrected == nullptr) {
        std::fclose(file);
        return 2;
    }
    Recording output{.file = file, .correctedFile = corrected};
    const bool passed = corpus(output);
    const int closed = file == nullptr ? 0 : std::fclose(file);
    const int correctedClosed = corrected == nullptr ? 0 : std::fclose(corrected);
    if (!passed || closed != 0 || correctedClosed != 0) return 1;
    if (output.cases != 16128 || output.correctedCases != 864) return 3;
    std::printf("image sampling: %zu scenes (%zu corrected), %zu bytes, hash=%llu PASS\n", output.cases,
                output.correctedCases,
                output.cases * sizeof(OutputPixels), static_cast<unsigned long long>(output.hash));
    return 0;
}
