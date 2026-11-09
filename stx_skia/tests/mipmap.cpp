#include "include/core/SkBitmap.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImage.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "include/private/SkMalloc.h"
#include "include/private/chromium/SkDiscardableMemory.h"
#include "src/core/SkMipmap.h"
#include "src/core/SkMipmapBuilder.h"
#include "src/image/SkImage_Base.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
struct Output {
    std::FILE* file = nullptr;
    uint64_t hash = 1469598103934665603ULL;
    size_t bytes = 0;
    int chains = 0;
};

bool record_pixels(const SkPixmap& pixels, Output& output) {
    const size_t rowBytes = pixels.info().minRowBytes();
    for (int y = 0; y < pixels.height(); ++y) {
        const uint8_t* row = static_cast<const uint8_t*>(pixels.addr(0, y));
        if (output.file != nullptr && std::fwrite(row, 1, rowBytes, output.file) != rowBytes) {
            return false;
        }
        for (size_t index = 0; index < rowBytes; ++index) {
            output.hash ^= row[index];
            output.hash *= 1099511628211ULL;
        }
        output.bytes += rowBytes;
    }
    return true;
}

bool chain(int width, int height, SkColorType format, Output& output) {
    sk_sp<SkColorSpace> colorSpace = SkColorSpace::MakeSRGB();
    const SkImageInfo info = SkImageInfo::Make(width, height, format, kPremul_SkAlphaType,
                                              colorSpace);
    const size_t pixelBytes = static_cast<size_t>(SkColorTypeBytesPerPixel(format));
    const size_t stride = static_cast<size_t>(width + 2) * pixelBytes;
    std::vector<uint64_t> storage((stride * height + 7) / 8, 0);
    const SkPixmap source(info, storage.data(), stride);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const SkColor color = SkColorSetARGB(255, (x * 19 + y * 7) & 255,
                                                  (y * 23 + x * 3) & 255, (x * y * 11) & 255);
            const SkIRect pixel = SkIRect::MakeXYWH(x, y, 1, 1);
            if (!source.erase(color, pixel)) return false;
        }
    }
    const std::vector<uint64_t> original = storage;
    sk_sp<SkMipmap> mipmap(SkMipmap::Build(source, nullptr));
    const int count = SkMipmap::ComputeLevelCount(width, height);
    if (count == 0) return !mipmap;
    if (!mipmap || (*mipmap).countLevels() != count) return false;
    if (!(*mipmap).validForRootLevel(info)) return false;
    int expectedWidth = width;
    int expectedHeight = height;
    for (int index = 0; index < count; ++index) {
        expectedWidth = expectedWidth > 1 ? expectedWidth / 2 : 1;
        expectedHeight = expectedHeight > 1 ? expectedHeight / 2 : 1;
        const SkISize dimensions = SkMipmap::ComputeLevelSize(width, height, index);
        if (dimensions.width() != expectedWidth || dimensions.height() != expectedHeight) return false;
        SkMipmap::Level level{};
        if (!(*mipmap).getLevel(index, &level) || !(*mipmap).getLevel(index, nullptr)) return false;
        if (level.fPixmap.dimensions() != dimensions) return false;
        if (level.fPixmap.rowBytes() != static_cast<size_t>(expectedWidth) * pixelBytes) return false;
        if (!SkColorSpace::Equals(level.fPixmap.colorSpace(), colorSpace.get())) return false;
        if (level.fScale.width() != static_cast<float>(expectedWidth) / width) return false;
        if (level.fScale.height() != static_cast<float>(expectedHeight) / height) return false;
        if (!record_pixels(level.fPixmap, output)) return false;
    }
    SkMipmap::Level unchanged{.fPixmap = source, .fScale = SkSize::Make(1.0f, 1.0f)};
    if ((*mipmap).getLevel(-1, &unchanged) || (*mipmap).getLevel(count, &unchanged)) return false;
    if ((*mipmap).extractLevel(SkSize::Make(1.0f, 1.0f), &unchanged)) return false;
    if (unchanged.fPixmap.addr() != source.addr()) return false;
    if (!(*mipmap).extractLevel(SkSize::Make(0.00001f, 0.00001f), &unchanged)) return false;
    if (unchanged.fPixmap.width() != 1 || unchanged.fPixmap.height() != 1) return false;
    if (storage != original) return false;
    ++output.chains;
    return true;
}

// Fill packed source representations directly so less common two-channel and
// integer formats do not depend on the separate SkPixmap erase conversion path.
void fill_raw(const SkPixmap& pixels, bool halfFloat) {
    const size_t rowBytes = pixels.info().minRowBytes();
    for (int y = 0; y < pixels.height(); ++y) {
        uint8_t* row = static_cast<uint8_t*>(pixels.writable_addr(0, y));
        if (halfFloat) {
            for (size_t word = 0; word < rowBytes / 2; ++word) {
                const uint16_t value = static_cast<uint16_t>(0x2000 + ((word * 131 + y * 313) % 0x1c00));
                std::memcpy(row + word * 2, &value, sizeof(value));
            }
        } else {
            for (size_t byte = 0; byte < rowBytes; ++byte) {
                row[byte] = static_cast<uint8_t>((byte * 107 + y * 197 + 43) & 255);
            }
        }
    }
}

bool packed_chains(Output& output) {
    struct Format {
        SkColorType colorType;
        bool halfFloat;
    };
    // RG16 and RGB10A2 have independent oracles for their corrected channels.
    constexpr std::array<Format, 16> formats{{
        {kRGBA_8888_SkColorType, false}, {kBGRA_8888_SkColorType, false},
        {kRGB_565_SkColorType, false}, {kARGB_4444_SkColorType, false},
        {kAlpha_8_SkColorType, false}, {kGray_8_SkColorType, false},
        {kR8_unorm_SkColorType, false}, {kRGBA_F16Norm_SkColorType, true},
        {kRGBA_F16_SkColorType, true}, {kR8G8_unorm_SkColorType, false},
        {kA16_unorm_SkColorType, false}, {kR16_unorm_SkColorType, false},
        {kA16_float_SkColorType, true}, {kR16_float_SkColorType, true},
        {kR16G16_float_SkColorType, true}, {kR16G16B16A16_unorm_SkColorType, false},
    }};
    for (const Format& format : formats) {
        for (int height = 1; height <= 9; ++height) {
            for (int width = 1; width <= 9; ++width) {
                if (width == 1 && height == 1) continue;
                const SkImageInfo info = SkImageInfo::Make(width, height, format.colorType,
                                                           kPremul_SkAlphaType);
                const size_t stride = info.minRowBytes() + 16;
                const size_t extent = stride * (height - 1) + info.minRowBytes();
                std::vector<uint64_t> storage((extent + 7) / 8, 0);
                const SkPixmap source(info, storage.data(), stride);
                fill_raw(source, format.halfFloat);
                const std::vector<uint64_t> original = storage;
                sk_sp<SkMipmap> mips(SkMipmap::Build(source, nullptr));
                if (!mips) return false;
                const int count = (*mips).countLevels();
                for (int index = 0; index < count; ++index) {
                    SkMipmap::Level level{};
                    if (!(*mips).getLevel(index, &level)) return false;
                    if (!record_pixels(level.fPixmap, output)) return false;
                }
                if (storage != original) return false;
                ++output.chains;
            }
        }
    }
    return true;
}

bool rg16_average() {
    for (int height = 1; height <= 17; ++height) {
        for (int width = 1; width <= 17; ++width) {
            if (width == 1 && height == 1) continue;
            const int outputWidth = width > 1 ? width / 2 : 1;
            const int outputHeight = height > 1 ? height / 2 : 1;
            const size_t sourceStride = static_cast<size_t>(width + 2);
            const size_t destinationStride = static_cast<size_t>(outputWidth + 2);
            std::vector<uint32_t> input(sourceStride * (height - 1) + width, 0xbaadbaadU);
            std::vector<uint32_t> output(destinationStride * (outputHeight - 1) + outputWidth,
                                         0xbaadbaadU);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const uint32_t red = static_cast<uint32_t>((x * 7919 + y * 3571) & 65535);
                    const uint32_t green = static_cast<uint32_t>((x * 2131 + y * 4421 + 12345) & 65535);
                    input[static_cast<size_t>(y) * sourceStride + x] = red | (green << 16);
                }
            }
            const SkImageInfo info = SkImageInfo::Make(width, height, kR16G16_unorm_SkColorType,
                                                       kOpaque_SkAlphaType);
            const SkPixmap source(info, input.data(), sourceStride * sizeof(uint32_t));
            const SkImageInfo destinationInfo = info.makeWH(outputWidth, outputHeight);
            const SkPixmap destination(destinationInfo, output.data(),
                                        destinationStride * sizeof(uint32_t));
            std::unique_ptr<SkMipmapDownSampler> sampler = SkMipmap::MakeDownSampler(source);
            if (!sampler) return false;
            (*sampler).buildLevel(destination, source);
            const int columns = width == 1 ? 1 : 2 + (width & 1);
            const int rows = height == 1 ? 1 : 2 + (height & 1);
            const unsigned divisor = (columns == 3 ? 4U : columns) * (rows == 3 ? 4U : rows);
            for (int y = 0; y < outputHeight; ++y) {
                for (int x = 0; x < outputWidth; ++x) {
                    uint32_t redSum = 0;
                    uint32_t greenSum = 0;
                    for (int row = 0; row < rows; ++row) {
                        for (int column = 0; column < columns; ++column) {
                            const unsigned wx = columns == 3 && column == 1 ? 2U : 1U;
                            const unsigned wy = rows == 3 && row == 1 ? 2U : 1U;
                            const size_t index = static_cast<size_t>(2 * y + row) * sourceStride +
                                                 2 * x + column;
                            const uint32_t pixel = input[index];
                            redSum += (pixel & 65535U) * wx * wy;
                            greenSum += (pixel >> 16) * wx * wy;
                        }
                    }
                    const uint32_t red = redSum / divisor;
#ifdef STX_SKIA_REFERENCE
                    // The pinned implementation narrows away the green channel.
                    const uint32_t green = 0;
#else
                    const uint32_t green = greenSum / divisor;
#endif
                    const uint32_t expected = red | (green << 16);
                    if (output[static_cast<size_t>(y) * destinationStride + x] != expected) return false;
                }
                if (y + 1 < outputHeight) {
                    for (size_t x = static_cast<size_t>(outputWidth); x < destinationStride; ++x) {
                        if (output[static_cast<size_t>(y) * destinationStride + x] != 0xbaadbaadU) {
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

bool rgba10_average() {
    for (const SkColorType format : {kRGBA_1010102_SkColorType, kBGRA_1010102_SkColorType}) {
        for (int height = 1; height <= 17; ++height) {
            for (int width = 1; width <= 17; ++width) {
                if (width == 1 && height == 1) continue;
                const int outputWidth = width > 1 ? width / 2 : 1;
                const int outputHeight = height > 1 ? height / 2 : 1;
                std::vector<uint32_t> input(static_cast<size_t>(width) * height, 0);
                std::vector<uint32_t> output(static_cast<size_t>(outputWidth) * outputHeight, 0);
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        const uint32_t c0 = static_cast<uint32_t>((x * 193 + y * 101) & 1023);
                        const uint32_t c1 = static_cast<uint32_t>((x * 317 + y * 293) & 1023);
                        const uint32_t c2 = static_cast<uint32_t>((x * 727 + y * 163) & 1023);
                        const uint32_t alpha = static_cast<uint32_t>((x + 2 * y + 3) & 3);
                        input[static_cast<size_t>(y) * width + x] = c0 | (c1 << 10) |
                                                                  (c2 << 20) | (alpha << 30);
                    }
                }
                const SkImageInfo info = SkImageInfo::Make(width, height, format, kPremul_SkAlphaType);
                const SkPixmap source(info, input.data(), static_cast<size_t>(width) * 4);
                const SkImageInfo destinationInfo = info.makeWH(outputWidth, outputHeight);
                const SkPixmap destination(destinationInfo, output.data(),
                                            static_cast<size_t>(outputWidth) * 4);
                std::unique_ptr<SkMipmapDownSampler> sampler = SkMipmap::MakeDownSampler(source);
                if (!sampler) return false;
                (*sampler).buildLevel(destination, source);
                const int columns = width == 1 ? 1 : 2 + (width & 1);
                const int rows = height == 1 ? 1 : 2 + (height & 1);
                const unsigned divisor = (columns == 3 ? 4U : columns) * (rows == 3 ? 4U : rows);
                for (int y = 0; y < outputHeight; ++y) {
                    for (int x = 0; x < outputWidth; ++x) {
                        uint32_t expected = 0;
                        for (int channel = 0; channel < 4; ++channel) {
                            const unsigned shift = static_cast<unsigned>(channel) * 10;
                            const uint32_t mask = channel == 3 ? 3U : 1023U;
                            uint32_t sum = 0;
                            for (int row = 0; row < rows; ++row) {
                                for (int column = 0; column < columns; ++column) {
                                    const unsigned wx = columns == 3 && column == 1 ? 2U : 1U;
                                    const unsigned wy = rows == 3 && row == 1 ? 2U : 1U;
                                    const size_t index = static_cast<size_t>(2 * y + row) * width +
                                                         2 * x + column;
                                    sum += ((input[index] >> shift) & mask) * wx * wy;
                                }
                            }
#ifdef STX_SKIA_REFERENCE
                            // The pinned filter reserves only four bits for the
                            // weighted alpha sum. Large triangle sums wrap there.
                            if (channel == 3) sum &= 15U;
#endif
                            expected |= (sum / divisor) << shift;
                        }
                        if (output[static_cast<size_t>(y) * outputWidth + x] != expected) return false;
                    }
                }
            }
        }
    }
    return true;
}

bool independent_average() {
    alignas(8) std::array<uint8_t, 64> storage{};
    const SkImageInfo info = SkImageInfo::Make(4, 4, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    const SkPixmap source(info, storage.data(), 16);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const size_t offset = static_cast<size_t>(y * 16 + x * 4);
            storage[offset] = static_cast<uint8_t>(x * 40);
            storage[offset + 1] = static_cast<uint8_t>(y * 40);
            storage[offset + 2] = 80;
            storage[offset + 3] = 255;
        }
    }
    sk_sp<SkMipmap> mips(SkMipmap::Build(source, nullptr));
    if (!mips) return false;
    SkMipmap::Level level{};
    if (!(*mips).getLevel(0, &level)) return false;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const SkColor expected = SkColorSetARGB(255, 20 + x * 80, 20 + y * 80, 80);
            if (level.fPixmap.getColor(x, y) != expected) return false;
        }
    }
    if (!(*mips).getLevel(1, &level)) return false;
    if (level.fPixmap.getColor(0, 0) != SkColorSetARGB(255, 60, 60, 80)) return false;
    SkBitmap bitmap;
    if (!bitmap.installPixels(source)) return false;
    sk_sp<SkMipmap> fromBitmap(SkMipmap::Build(bitmap, nullptr));
    if (!fromBitmap || (*fromBitmap).countLevels() != 2) return false;
    return true;
}

bool builder() {
    const SkImageInfo info = SkImageInfo::Make(9, 7, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    alignas(8) std::array<uint8_t, 9 * 7 * 4> storage{};
    const SkPixmap source(info, storage.data(), 9 * 4);
    if (!source.erase(SK_ColorGREEN)) return false;
    sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(source);
    if (!image) return false;
    sk_sp<SkImage> attached{};
    {
        SkMipmapBuilder levels(info);
        if (levels.countLevels() != 3) return false;
        if (levels.level(-1).addr() != nullptr || levels.level(3).addr() != nullptr) return false;
        for (int index = 0; index < 3; ++index) {
            const SkPixmap level = levels.level(index);
            const SkColor color = SkColorSetARGB(255, 20 + index * 30, 70, 90);
            if (!level.erase(color)) return false;
        }
        attached = levels.attachTo(image);
        if (!attached || !(*attached).hasMipmaps()) return false;
#ifndef STX_SKIA_REFERENCE
        if (levels.attachTo(nullptr)) return false;
#endif
    }
    image.reset();
    const SkImage_Base& base = *as_IB(attached);
    sk_sp<SkMipmap> mips = base.refMips();
    if (!mips) return false;
    for (int index = 0; index < 3; ++index) {
        SkMipmap::Level level{};
        if (!(*mips).getLevel(index, &level)) return false;
        const SkColor expected = SkColorSetARGB(255, 20 + index * 30, 70, 90);
        if (level.fPixmap.getColor(0, 0) != expected) return false;
    }
    const SkImageInfo tiny = info.makeWH(1, 1);
    SkMipmapBuilder empty(tiny);
    if (empty.countLevels() != 0 || empty.level(0).addr() != nullptr) return false;
    return true;
}

struct AllocationEvents {
    int calls = 0;
    int locks = 0;
    int unlocks = 0;
    int destroyed = 0;
    bool valid = true;
    bool failAllocation = false;
    bool discardOnUnlock = false;
};
// Factory API has no context argument. This fixture is single-threaded; global
// state outlives every returned block and is reset only after all owners release.
AllocationEvents events{};

class Discardable final : public SkDiscardableMemory {
public:
    explicit Discardable(size_t bytes) : fStorage(sk_malloc_throw(bytes)) {}
    ~Discardable() override {
        events.valid = events.valid && !fLocked;
        ++events.destroyed;
        sk_free(fStorage);
    }
    bool lock() override {
        events.valid = events.valid && !fLocked;
        ++events.locks;
        if (fStorage == nullptr) return false;
        fLocked = true;
        return true;
    }
    void* data() override {
        events.valid = events.valid && fLocked;
        return fStorage;
    }
    void unlock() override {
        events.valid = events.valid && fLocked;
        fLocked = false;
        ++events.unlocks;
        if (events.discardOnUnlock) {
            sk_free(fStorage);
            fStorage = nullptr;
        }
    }
private:
    void* fStorage = nullptr;
    bool fLocked = true;
};

SkDiscardableMemory* allocate(size_t bytes) {
    ++events.calls;
    if (events.failAllocation) return nullptr;
    SkDiscardableMemory* memory = new Discardable(bytes);
    return memory;
}

bool discardable(bool discard) {
    events = AllocationEvents{};
    events.discardOnUnlock = discard;
    alignas(8) std::array<uint8_t, 64> storage{};
    const SkImageInfo info = SkImageInfo::Make(4, 4, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    const SkPixmap source(info, storage.data(), 16);
    if (!source.erase(SK_ColorGREEN)) return false;
    sk_sp<SkMipmap> client(SkMipmap::Build(source, allocate));
    if (!client) return false;
    SkMipmap* cache = client.get();
    (*cache).attachToCacheAndRef();
    client.reset();
    // Cache identity remains live but no pixels may be inspected without a
    // client reference. The next ref either restores the block or reports loss.
    client = sk_ref_sp(cache);
    SkMipmap::Level selected{};
    const bool found = (*client).getLevel(0, &selected);
    const bool extracted = (*client).extractLevel(SkSize::Make(0.25f, 0.25f), nullptr);
    const bool valid = (*client).validForRootLevel(info);
    bool correct = found == !discard && extracted == !discard && valid == !discard;
    if (found && selected.fPixmap.getColor(0, 0) != SK_ColorGREEN) correct = false;
    (*cache).detachFromCacheAndUnref();
    client.reset();
    correct = correct && events.calls == 1 && events.locks == 1 &&
              events.unlocks == (discard ? 1 : 2) && events.destroyed == 1 && events.valid;
    return correct;
}

int benchmark() {
    struct Case { int width; int height; int iterations; };
    constexpr std::array<Case, 5> cases{{
        {8, 8, 100000}, {127, 129, 500}, {128, 128, 500},
        {1023, 1025, 20}, {1024, 1024, 20},
    }};
    constexpr std::array<SkColorType, 2> formats{kRGBA_8888_SkColorType, kRGBA_F16_SkColorType};
    for (const SkColorType format : formats) {
        for (const Case& sample : cases) {
            const SkImageInfo info = SkImageInfo::Make(sample.width, sample.height, format,
                                                       kPremul_SkAlphaType);
            const int outputWidth = sample.width / 2;
            const int outputHeight = sample.height / 2;
            const SkImageInfo reducedInfo = info.makeWH(outputWidth, outputHeight);
            const size_t sourceStride = info.minRowBytes() + 16;
            const size_t destinationStride = reducedInfo.minRowBytes() + 16;
            std::vector<uint64_t> input((sourceStride * sample.height + 7) / 8, 0);
            std::vector<uint64_t> output((destinationStride * outputHeight + 7) / 8, 0);
            const SkPixmap source(info, input.data(), sourceStride);
            const SkPixmap destination(reducedInfo, output.data(), destinationStride);
            fill_raw(source, format == kRGBA_F16_SkColorType);
            std::unique_ptr<SkMipmapDownSampler> sampler = SkMipmap::MakeDownSampler(source);
            if (!sampler) return 9;
            (*sampler).buildLevel(destination, source);
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            for (int iteration = 0; iteration < sample.iterations; ++iteration) {
                (*sampler).buildLevel(destination, source);
            }
            const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
            const std::chrono::duration<double> elapsed = end - start;
            Output result{};
            if (!record_pixels(destination, result)) return 10;
            std::printf("%d,%d,%d,%d,%.9f,%llu\n", static_cast<int>(format), sample.width,
                        sample.height, sample.iterations, elapsed.count(),
                        static_cast<unsigned long long>(result.hash));
        }
    }
    return 0;
}

bool invalid_inputs() {
    events = AllocationEvents{};
    events.failAllocation = true;
    const SkImageInfo info = SkImageInfo::Make(4, 4, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    const SkPixmap metadata(info, nullptr, 0);
    sk_sp<SkMipmap> failed(SkMipmap::Build(metadata, allocate, false));
    if (failed || events.calls != 1) return false;
#ifndef STX_SKIA_REFERENCE
    events.calls = 0;
    alignas(8) std::array<uint8_t, 64> storage{};
    const std::array<SkPixmap, 6> invalid{{
        SkPixmap(info, nullptr, 16),
        SkPixmap(info, storage.data(), 4),
        SkPixmap(info, storage.data(), std::numeric_limits<size_t>::max()),
        SkPixmap(info.makeWH(-1, 4), storage.data(), 16),
        SkPixmap(info.makeColorType(kUnknown_SkColorType), storage.data(), 16),
        SkPixmap(info.makeWH(400000000, 400000000), nullptr, 0),
    }};
    for (size_t index = 0; index < invalid.size(); ++index) {
        const bool compute = index != invalid.size() - 1;
        sk_sp<SkMipmap> result(SkMipmap::Build(invalid[index], allocate, compute));
        if (result) return false;
    }
    if (events.calls != 0) return false;
#endif
    if (SkMipmap::ComputeLevelCount(-1, 4) != 0) return false;
    if (!SkMipmap::ComputeLevelSize(4, 4, -1).isEmpty()) return false;
    if (!SkMipmap::ComputeLevelSize(4, 4, 2).isEmpty()) return false;
    return true;
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) {
        const int result = benchmark();
        return result;
    }
    if (!rg16_average()) return 7;
    if (!rgba10_average()) return 8;
    if (!independent_average()) return 1;
    if (!builder()) return 2;
    if (!discardable(false) || !discardable(true)) return 3;
    if (!invalid_inputs()) return 4;
    Output output{};
    if (argc == 2) {
        output.file = std::fopen(argv[1], "wb");
        if (output.file == nullptr) return 5;
    }
    constexpr std::array<SkColorType, 4> formats{
        kAlpha_8_SkColorType, kRGB_565_SkColorType, kRGBA_8888_SkColorType, kRGBA_F16_SkColorType};
    bool passed = true;
    for (const SkColorType format : formats) {
        for (int height = 1; height <= 17; ++height) {
            for (int width = 1; width <= 17; ++width) {
                if (!chain(width, height, format, output)) passed = false;
            }
        }
    }
    if (!packed_chains(output)) passed = false;
    if (output.file != nullptr && std::fclose(output.file) != 0) passed = false;
    if (!passed) return 6;
    std::printf("mipmap: %d chains, %zu bytes, hash=%llu PASS\n", output.chains,
                output.bytes, static_cast<unsigned long long>(output.hash));
    return 0;
}
