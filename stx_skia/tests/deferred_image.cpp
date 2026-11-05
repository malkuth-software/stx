#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageGenerator.h"
#include "include/core/SkPixmap.h"
#include "src/image/SkImage_Lazy.h"
#include "peek_pixels.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <limits>
#include <optional>
#include <thread>
#include <utility>

template <typename T>
concept HasGeneratorPlanes = requires { &T::getYUVAPlanes; };
template <typename T>
concept HasTextureGenerator = requires(T& generator) { generator.isTextureGenerator(); };
#ifdef STX_SKIA_REFERENCE
static_assert(HasGeneratorPlanes<SkImageGenerator>);
static_assert(HasTextureGenerator<SkImageGenerator>);
#else
static_assert(!HasGeneratorPlanes<SkImageGenerator>);
static_assert(!HasTextureGenerator<SkImageGenerator>);
#endif

namespace {
constexpr int width = 8;
constexpr int height = 5;
constexpr std::size_t stride = 40;

SkImageInfo image_info() {
    const SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType,
                                              kPremul_SkAlphaType);
    return info;
}

struct GeneratorState {
    std::atomic<int> calls{0};
    std::atomic<int> active{0};
    std::atomic<int> overlaps{0};
    std::atomic<int> destroyed{0};
    std::atomic<bool> failNext{false};
};

// The test owns state for longer than every image and worker. Only the generator
// mutates pixel destinations; each worker supplies its own separate buffer.
class CountedGenerator final : public SkImageGenerator {
public:
    CountedGenerator(GeneratorState& state, const SkImageInfo& info)
        : SkImageGenerator(info), fState(state) {}
    ~CountedGenerator() override { ++fState.destroyed; }
protected:
    bool onGetPixels(const SkImageInfo& info, void* pixels, size_t rowBytes,
                     const Options&) override {
        const int concurrent = fState.active.fetch_add(1);
        if (concurrent != 0) ++fState.overlaps;
        ++fState.calls;
        for (int i = 0; i < 8; ++i) std::this_thread::yield();
        const bool fail = fState.failNext.exchange(false);
        const SkPixmap destination(info, pixels, rowBytes);
        const bool filled = destination.erase(SK_ColorGREEN);
        --fState.active;
        const bool succeeded = filled && !fail;
        return succeeded;
    }
    sk_sp<const SkData> onRefEncodedData() override {
        constexpr std::array<uint8_t, 4> marker{4, 3, 2, 1};
        sk_sp<const SkData> data = SkData::MakeWithCopy(marker.data(), marker.size());
        return data;
    }
private:
    GeneratorState& fState;
};

sk_sp<SkImage> make_image(GeneratorState& state) {
    const SkImageInfo info = image_info();
    std::unique_ptr<SkImageGenerator> generator = std::make_unique<CountedGenerator>(state, info);
    sk_sp<SkImage> image = SkImages::DeferredFromGenerator(std::move(generator));
    return image;
}

bool read_image(const SkImage& image, SkImage::CachingHint hint) {
    std::array<uint8_t, stride * height> storage{};
    storage.fill(0xbc);
    const SkPixmap destination(image_info(), storage.data(), stride);
    if (!image.readPixels(nullptr, destination, 0, 0, hint)) return false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (destination.getColor(x, y) != SK_ColorGREEN) return false;
        }
        for (std::size_t byte = width * 4; byte < stride; ++byte) {
            if (storage[static_cast<std::size_t>(y) * stride + byte] != 0xbc) return false;
        }
    }
    return true;
}

bool cache_policy() {
    GeneratorState state;
    sk_sp<SkImage> image = make_image(state);
    if (!image || !(*image).isLazyGenerated()) return false;
    if (state.calls != 0) return false;
    if (!read_image(*image, SkImage::kDisallow_CachingHint)) return false;
    if (!read_image(*image, SkImage::kDisallow_CachingHint)) return false;
    if (state.calls != 2) return false;
    if (!read_image(*image, SkImage::kAllow_CachingHint)) return false;
    if (state.calls != 3) return false;
    if (!read_image(*image, SkImage::kAllow_CachingHint)) return false;
    // Disallow prevents adding a new entry; it still permits an existing hit.
    if (!read_image(*image, SkImage::kDisallow_CachingHint)) return false;
    if (state.calls != 3) return false;
    image.reset();
    const bool destroyed = state.destroyed == 1;
    return destroyed;
}

bool failed_generation() {
    GeneratorState state;
    state.failNext = true;
    sk_sp<SkImage> image = make_image(state);
    if (!image) return false;
    std::array<uint8_t, stride * height> storage{};
    storage.fill(0xca);
    const std::array<uint8_t, stride * height> unchanged = storage;
    const SkPixmap destination(image_info(), storage.data(), stride);
    if ((*image).readPixels(nullptr, destination, 0, 0, SkImage::kAllow_CachingHint)) return false;
    if (storage != unchanged || state.calls != 1) return false;
    if (!read_image(*image, SkImage::kAllow_CachingHint) || state.calls != 2) return false;
    if (!read_image(*image, SkImage::kAllow_CachingHint) || state.calls != 2) return false;
    return true;
}

bool conversions_and_lifetime() {
    GeneratorState state;
    sk_sp<SkImage> image = make_image(state);
    if (!image) return false;
    sk_sp<const SkData> encoded = (*image).refEncodedData();
    if (!encoded || (*encoded).size() != 4) return false;
    sk_sp<SkImage> converted = (*image).makeColorTypeAndColorSpace(
        nullptr, kBGRA_8888_SkColorType, nullptr, {});
    sk_sp<SkImage> repeated = (*image).makeColorTypeAndColorSpace(
        nullptr, kBGRA_8888_SkColorType, nullptr, {});
    if (!converted || converted != repeated || state.calls != 0) return false;
    if ((*converted).uniqueID() == (*image).uniqueID()) return false;
    if ((*converted).refEncodedData()) return false;
    repeated.reset();
    sk_sp<SkColorSpace> linear = SkColorSpace::MakeSRGBLinear();
    sk_sp<SkImage> reinterpreted = (*image).reinterpretColorSpace(linear);
    if (!reinterpreted || (*reinterpreted).isLazyGenerated()) return false;
    if (!SkColorSpace::Equals((*reinterpreted).colorSpace(), linear.get())) return false;
    const SkIRect bounds = SkIRect::MakeXYWH(1, 1, 4, 3);
    sk_sp<SkImage> subset = (*image).makeSubset(nullptr, bounds, {});
    if (!subset || (*subset).width() != 4 || (*subset).height() != 3) return false;
    const std::optional<SkPixmap> subsetPixelsView = peek_pixels(*subset);
    if (!subsetPixelsView) return false;
    const SkPixmap& subsetPixels = *subsetPixelsView;
    if (subsetPixels.getColor(3, 2) != SK_ColorGREEN) return false;
    image.reset();
    if (state.destroyed != 0) return false;
    if (!read_image(*converted, SkImage::kDisallow_CachingHint)) return false;
    converted.reset();
    if (state.destroyed != 1) return false;
    // Materialized images remain independent of the provider's lifetime.
    if (!read_image(*reinterpreted, SkImage::kDisallow_CachingHint)) return false;
    return true;
}

struct WorkerResult { bool passed = false; };
void read_worker(const SkImage* image, std::barrier<>& start, WorkerResult& result) {
    start.arrive_and_wait();
    for (int i = 0; i < 25; ++i) {
        if (!read_image(*image, SkImage::kDisallow_CachingHint)) return;
    }
    result.passed = true;
}

bool concurrent_generation() {
    GeneratorState state;
    sk_sp<SkImage> image = make_image(state);
    if (!image) return false;
    sk_sp<SkImage> converted = (*image).makeColorTypeAndColorSpace(
        nullptr, kBGRA_8888_SkColorType, nullptr, {});
    if (!converted) return false;
    std::barrier<> start(4);
    std::array<WorkerResult, 4> results{};
    std::array<std::thread, 4> workers{};
    for (std::size_t i = 0; i < workers.size(); ++i) {
        const SkImage* selected = i % 2 == 0 ? image.get() : converted.get();
        workers[i] = std::thread(read_worker, selected, std::ref(start), std::ref(results[i]));
    }
    for (std::thread& worker : workers) worker.join();
    for (const WorkerResult& result : results) {
        if (!result.passed) return false;
    }
    const bool serialized = state.calls == 100 && state.active == 0 && state.overlaps == 0;
    return serialized;
}

bool request_validation() {
    GeneratorState state;
    const SkImageInfo info = image_info();
    CountedGenerator generator(state, info);
    std::array<uint8_t, stride * height> pixels{};
    if (generator.getPixels(info, nullptr, stride)) return false;
    if (generator.getPixels(info, pixels.data(), 1)) return false;
#ifndef STX_SKIA_REFERENCE
    // These rejected requests must never reach a provider or touch its buffer.
    if (generator.getPixels(info.makeWH(0, height), pixels.data(), stride)) return false;
    if (generator.getPixels(info.makeWH(-1, height), pixels.data(), stride)) return false;
    const SkImageInfo excessive = info.makeWH(std::numeric_limits<int>::max(), 1);
    if (generator.getPixels(excessive, pixels.data(), stride)) return false;
    if (generator.getPixels(info, pixels.data(), stride + 1)) return false;
    const std::size_t overflowingStride = std::numeric_limits<std::size_t>::max() - 3;
    if (generator.getPixels(info, pixels.data(), overflowingStride)) return false;
#endif
    if (state.calls != 0) return false;
    if (!generator.getPixels(info, pixels.data(), stride)) return false;
    const bool once = state.calls == 1;
    return once;
}

bool invalid_factory() {
    if (SkImages::DeferredFromGenerator(nullptr)) return false;
    GeneratorState state;
    const SkImageInfo info = image_info();
    const SkImageInfo invalid = info.makeWH(0, height);
    std::unique_ptr<SkImageGenerator> generator = std::make_unique<CountedGenerator>(state, invalid);
    if (SkImages::DeferredFromGenerator(std::move(generator))) return false;
    const bool released = state.calls == 0 && state.destroyed == 1;
    return released;
}
}

int main() {
    if (!cache_policy()) return 1;
    if (!failed_generation()) return 2;
    if (!conversions_and_lifetime()) return 3;
    if (!concurrent_generation()) return 4;
    if (!invalid_factory()) return 5;
    if (!request_validation()) return 6;
    std::printf("deferred images: cache policy, failure, conversion, lifetime, serialized generation; lazy %zu bytes; PASS\n",
                sizeof(SkImage_Lazy));
    return 0;
}
