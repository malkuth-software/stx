#include "include/core/SkBitmap.h"
#include "include/core/SkCPUContext.h"
#include "include/core/SkCPURecorder.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixelRef.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRecorder.h"
#include "include/core/SkSurface.h"
#include "src/image/SkSurface_Raster.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

template <typename T>
concept HasGpuSurfaceContext = requires(T& surface) { surface.recordingContext(); };
template <typename T>
concept HasGpuSurfaceRecorder = requires(T& surface) { surface.recorder(); };
template <typename T>
concept HasGpuSurfaceWait = requires(T& surface) { surface.wait(0, nullptr); };
template <typename T>
concept HasGpuSurfaceCharacterization = requires(T& surface) { surface.characterize(nullptr); };
template <typename T>
concept HasYuvSurfaceRead = requires { &T::asyncRescaleAndReadPixelsYUV420; };
#ifdef STX_SKIA_REFERENCE
static_assert(HasGpuSurfaceContext<SkSurface>);
static_assert(HasGpuSurfaceRecorder<SkSurface>);
static_assert(HasGpuSurfaceWait<SkSurface>);
static_assert(HasGpuSurfaceCharacterization<SkSurface>);
static_assert(HasYuvSurfaceRead<SkSurface>);
#else
static_assert(!HasGpuSurfaceContext<SkSurface>);
static_assert(!HasGpuSurfaceRecorder<SkSurface>);
static_assert(!HasGpuSurfaceWait<SkSurface>);
static_assert(!HasGpuSurfaceCharacterization<SkSurface>);
static_assert(!HasYuvSurfaceRead<SkSurface>);
#endif

// The library downcasts SkRecorder by type(), skcpu::Recorder to its implementation, and
// skcpu::Context to its implementation. Classes derived outside the library would make those
// casts undefined, so the candidate does not let them be constructed; the reference does.
struct ForeignRecorder : SkRecorder {
    Type type() const override { return Type::kCPU; }
    skcpu::Recorder* cpuRecorder() override { return nullptr; }
};
struct ForeignCpuRecorder : skcpu::Recorder {};
struct ForeignContext : skcpu::Context {};
#ifdef STX_SKIA_REFERENCE
// The intake SkRecorder has further pure virtual functions, so ForeignRecorder is abstract there.
static_assert(std::is_default_constructible_v<ForeignCpuRecorder>);
static_assert(std::is_default_constructible_v<ForeignContext>);
#else
// ForeignRecorder is concrete, so only the sealed constructor keeps it from being constructed.
static_assert(!std::is_abstract_v<ForeignRecorder>);
static_assert(!std::is_default_constructible_v<ForeignRecorder>);
static_assert(!std::is_default_constructible_v<ForeignCpuRecorder>);
static_assert(!std::is_default_constructible_v<ForeignContext>);
#endif

namespace {
constexpr int width = 9;
constexpr int height = 7;
constexpr std::size_t stride = 52;

struct ReleaseState {
    int calls = 0;
};

void record_release(void*, void* context) {
    ReleaseState& state = *static_cast<ReleaseState*>(context);
    ++state.calls;
}

// Storage and release state belong to each test scope and outlive the pixel ref.
// This tracks its lifetime without an allocator hook or a dangling observation.
class TrackedPixels final : public SkPixelRef {
public:
    TrackedPixels(void* pixels, ReleaseState& state)
        : SkPixelRef(9, 7, pixels, stride), fState(state) {}
    ~TrackedPixels() override { ++fState.calls; }
private:
    ReleaseState& fState;
};

SkImageInfo image_info() {
    const SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType,
                                              kPremul_SkAlphaType);
    return info;
}

bool notify_change(SkSurface& surface, SkSurface::ContentChangeMode mode) {
#ifdef STX_SKIA_REFERENCE
    surface.notifyContentWillChange(mode);
    return true;
#else
    const bool prepared = surface.notifyContentWillChange(mode);
    return prepared;
#endif
}

bool verify_image(const SkImage& image, bool greenCenter) {
    SkPixmap pixels;
    if (!image.peekPixels(&pixels)) return false;
    if (pixels.width() != width || pixels.height() != height) return false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool green = greenCenter && x >= 2 && x < 6 && y >= 1 && y < 5;
            const SkColor expected = green ? SK_ColorGREEN : SK_ColorRED;
            if (pixels.getColor(x, y) != expected) return false;
        }
    }
    return true;
}

bool snapshots(bool wrapped, bool discard) {
    ReleaseState release;
    std::array<uint8_t, stride * height> storage{};
    const SkImageInfo info = image_info();
    sk_sp<SkSurface> surface;
    if (wrapped) {
        surface = SkSurfaces::WrapPixels(info, storage.data(), stride, record_release, &release);
    } else {
        surface = SkSurfaces::Raster(info, stride, nullptr);
    }
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    if ((*surface).getCanvas() != &canvas) return false;
    canvas.clear(SK_ColorRED);
    const uint32_t generation = (*surface).generationID();
    if (generation != (*surface).generationID()) return false;
    sk_sp<SkImage> oldImage = (*surface).makeImageSnapshot();
    if (!oldImage || !verify_image(*oldImage, false)) return false;
    sk_sp<SkImage> repeated = (*surface).makeImageSnapshot();
    if (repeated != oldImage) return false;
    repeated.reset();
    sk_sp<SkImage> subset = (*surface).makeImageSnapshot(SkIRect::MakeXYWH(-2, -2, 5, 4));
    if (!subset || (*subset).width() != 3 || (*subset).height() != 2) return false;
    if ((*surface).makeImageSnapshot(SkIRect::MakeXYWH(20, 20, 4, 4))) return false;

    if (discard) {
        if (!notify_change(*surface, SkSurface::kDiscard_ContentChangeMode)) {
            return false;
        }
        canvas.clear(SK_ColorRED);
    }
    SkPaint paint;
    paint.setColor(SK_ColorGREEN);
    canvas.drawRect(SkRect::MakeXYWH(2, 1, 4, 4), paint);
    if ((*surface).generationID() == generation) return false;
    sk_sp<SkImage> current = (*surface).makeImageSnapshot();
    if (!current || current == oldImage || !verify_image(*current, true)) return false;
    if (!verify_image(*oldImage, false)) return false;
    if (release.calls != 0) return false;
    surface.reset();
    if (release.calls != (wrapped ? 1 : 0)) return false;
    if (!verify_image(*oldImage, false) || !verify_image(*current, true)) return false;
    SkPixmap subsetPixels;
    if (!(*subset).peekPixels(&subsetPixels)) return false;
    return subsetPixels.getColor(0, 0) == SK_ColorRED;
}

bool external_write_preserves_snapshot() {
    sk_sp<SkSurface> surface = SkSurfaces::Raster(image_info());
    if (!surface) return false;
    SkCanvas* canvas = (*surface).getCanvas();
    if (canvas == nullptr) return false;
    (*canvas).clear(SK_ColorRED);
    sk_sp<SkImage> snapshot = (*surface).makeImageSnapshot();
    if (!snapshot) return false;
    if (!notify_change(*surface, SkSurface::kRetain_ContentChangeMode)) {
        return false;
    }
    SkPixmap writable;
    if (!(*surface).peekPixels(&writable)) return false;
    *writable.writable_addr32(2, 2) = SK_ColorGREEN;
    SkPixmap oldPixels;
    if (!(*snapshot).peekPixels(&oldPixels)) return false;
    return oldPixels.getColor(2, 2) == SK_ColorRED &&
           writable.getColor(2, 2) == SK_ColorGREEN;
}

bool old_allocation_release() {
    ReleaseState release;
    std::array<uint8_t, stride * height> storage{};
    const SkImageInfo info = image_info();
    sk_sp<SkPixelRef> pixels = sk_make_sp<TrackedPixels>(storage.data(), release);
    sk_sp<SkSurface> surface = sk_make_sp<SkSurface_Raster>(info, std::move(pixels), nullptr);
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.clear(SK_ColorRED);
    sk_sp<SkImage> snapshot = (*surface).makeImageSnapshot();
    if (!snapshot) return false;
    SkPaint paint;
    paint.setColor(SK_ColorGREEN);
    canvas.drawRect(SkRect::MakeXYWH(2, 1, 4, 4), paint);
    if (release.calls != 0 || !verify_image(*snapshot, false)) return false;
    snapshot.reset();
#ifdef STX_SKIA_REFERENCE
    // Original capture bookkeeping retains the initial pixel ref until destruction.
    if (release.calls != 0) return false;
#else
    if (release.calls != 1) return false;
#endif
    sk_sp<SkImage> current = (*surface).makeImageSnapshot();
    if (!current || !verify_image(*current, true)) return false;
    surface.reset();
    return release.calls == 1;
}

bool restore_mutability() {
    const SkImageInfo info = image_info();
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info, stride, nullptr);
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.clear(SK_ColorRED);
    SkPixmap before;
    if (!(*surface).peekPixels(&before)) return false;
    sk_sp<SkImage> temporary = (*surface).makeImageSnapshot();
    if (!temporary) return false;
    temporary.reset();
    canvas.clear(SK_ColorGREEN);
    SkPixmap after;
    if (!(*surface).peekPixels(&after)) return false;
    if (before.addr() != after.addr()) return false;
    return after.getColor(4, 3) == SK_ColorGREEN;
}

struct ReadState {
    int calls = 0;
    std::unique_ptr<const SkSurface::AsyncReadResult> result = nullptr;
};

void record_read(void* context, std::unique_ptr<const SkSurface::AsyncReadResult> result) {
    ReadState& state = *static_cast<ReadState*>(context);
    ++state.calls;
    state.result = std::move(result);
}

bool callback_reads() {
    ReadState state;
    const SkImageInfo info = image_info();
    const SkImageInfo small = info.makeWH(3, 2);
    const SkIRect bounds = SkIRect::MakeWH(width, height);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.clear(SK_ColorGREEN);
    (*surface).asyncRescaleAndReadPixels(small, bounds, SkSurface::RescaleGamma::kSrc,
        SkSurface::RescaleMode::kNearest, record_read, &state);
    if (state.calls != 1 || !state.result || (*state.result).count() != 1) return false;
    {
        const SkPixmap pixels(small, (*state.result).data(0), (*state.result).rowBytes(0));
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 3; ++x) {
                if (pixels.getColor(x, y) != SK_ColorGREEN) return false;
            }
        }
    }
    (*surface).asyncRescaleAndReadPixels(small, SkIRect::MakeWH(width + 1, height),
        SkSurface::RescaleGamma::kSrc, SkSurface::RescaleMode::kNearest, record_read, &state);
    if (state.calls != 2 || state.result) return false;
#ifndef STX_SKIA_REFERENCE
    // The reference aborts when allocating an unknown-format fallback bitmap.
    sk_sp<SkSurface> empty = SkSurfaces::Null(width, height);
    (*empty).asyncRescaleAndReadPixels(small, bounds, SkSurface::RescaleGamma::kSrc,
        SkSurface::RescaleMode::kNearest, record_read, &state);
    return state.calls == 3 && !state.result;
#else
    return true;
#endif
}

bool invalid_and_null() {
    const SkImageInfo info = image_info();
    std::array<uint8_t, stride * height> storage{};
    ReleaseState release;
    if (SkSurfaces::WrapPixels(info, nullptr, stride, record_release, &release)) return false;
    if (SkSurfaces::WrapPixels(info, storage.data(), 1, record_release, &release)) return false;
#ifndef STX_SKIA_REFERENCE
    const std::size_t ignoredStride = std::numeric_limits<std::size_t>::max();
    if (SkSurfaces::WrapPixels(info, storage.data(), ignoredStride,
                              record_release, &release)) return false;
    const std::size_t excessiveStride = ignoredStride - 3;
    if (SkSurfaces::WrapPixels(info, storage.data(), excessiveStride,
                              record_release, &release)) return false;
    const SkImageInfo singleRow = info.makeWH(width, 1);
    const std::size_t unrepresentableStride = static_cast<std::size_t>(INT32_MAX) + 1;
    if (SkSurfaces::Raster(singleRow, unrepresentableStride, nullptr)) return false;
    if (SkSurfaces::Raster(singleRow, ignoredStride, nullptr)) return false;
    sk_sp<SkSurface> recorderSurface = SkSurfaces::Raster(singleRow);
    if (!recorderSurface) return false;
    skcpu::Recorder* recorder = skcpu::AsRecorder((*recorderSurface).baseRecorder());
    if (recorder == nullptr) return false;
    if ((*recorder).makeBitmapSurface(singleRow, unrepresentableStride, nullptr)) return false;
    if ((*recorder).makeBitmapSurface(singleRow, ignoredStride, nullptr)) return false;
#endif
    if (SkSurfaces::Raster(info, 1, nullptr)) return false;
    if (release.calls != 0) return false;
    if (SkSurfaces::Null(0, height)) return false;
    sk_sp<SkSurface> surface = SkSurfaces::Null(width, height);
    if (!surface || (*surface).makeImageSnapshot()) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.clear(SK_ColorRED);
    SkPixmap pixels;
    if ((*surface).peekPixels(&pixels)) return false;
    return (*surface).getCanvas() == &canvas;
}

// Exercise facade copies with disjoint, padded buffers. The expected image is
// specified by coordinates rather than reconstructed through another copy API.
bool pixel_copies(bool wrapped) {
    std::array<uint8_t, stride * height> backing{};
    backing.fill(0xa5);
    const SkImageInfo info = image_info();
    sk_sp<SkSurface> surface;
    if (wrapped) {
        surface = SkSurfaces::WrapPixels(info, backing.data(), stride);
    } else {
        surface = SkSurfaces::Raster(info, stride, nullptr);
    }
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    canvas.clear(SK_ColorRED);
    sk_sp<SkImage> oldImage = (*surface).makeImageSnapshot();
    if (!oldImage) return false;

    constexpr std::size_t sourceStride = 28;
    std::array<uint8_t, sourceStride * 4> source{};
    source.fill(0xb6);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 5; ++x) {
            const std::size_t offset = static_cast<std::size_t>(y) * sourceStride + x * 4U;
            source[offset] = static_cast<uint8_t>(x * 30);
            source[offset + 1] = static_cast<uint8_t>(y * 40);
            source[offset + 2] = 90;
            source[offset + 3] = 255;
        }
    }
    const SkPixmap input(info.makeWH(5, 4), source.data(), sourceStride);
    const uint32_t before = (*surface).generationID();
    (*surface).writePixels(input, width + 1, height + 1);
    if ((*surface).generationID() != before) return false;
    // Pixel writes ignore drawing transforms and clips.
    canvas.save();
    canvas.translate(20.0f, 30.0f);
    canvas.clipRect(SkRect::MakeEmpty());
    (*surface).writePixels(input, -2, 2);
    canvas.restore();
    if ((*surface).generationID() == before) return false;
    if (!verify_image(*oldImage, false)) return false;

    constexpr std::size_t outputStride = 60;
    const SkImageInfo outputInfo = info.makeWH(width + 2, height + 2);
    std::array<uint8_t, outputStride * (height + 2)> output{};
    output.fill(0xc7);
    if (!(*surface).readPixels(outputInfo, output.data(), outputStride, -1, -1)) return false;
    for (int y = 0; y < height + 2; ++y) {
        for (std::size_t byte = 0; byte < outputStride; ++byte) {
            uint8_t expected = 0xc7;
            const int x = static_cast<int>(byte / 4);
            const int component = static_cast<int>(byte % 4);
            if (y >= 1 && y <= height && x >= 1 && x <= width) {
                const int sx = x - 1;
                const int sy = y - 1;
                expected = (component == 0 || component == 3) ? 255 : 0;
                if (sx < 3 && sy >= 2 && sy < 6) {
                    const std::size_t sourceOffset =
                        static_cast<std::size_t>(sy - 2) * sourceStride + (sx + 2) * 4U;
                    expected = source[sourceOffset + component];
                }
            }
            const std::size_t offset = static_cast<std::size_t>(y) * outputStride + byte;
            if (output[offset] != expected) return false;
        }
    }
    const std::array<uint8_t, outputStride * (height + 2)> saved = output;
    const SkPixmap destination(outputInfo, output.data(), outputStride);
    if ((*surface).readPixels(destination, width, height)) return false;
    if (output != saved) return false;
    if (wrapped) {
        for (int y = 0; y < height; ++y) {
            for (std::size_t byte = width * 4; byte < stride; ++byte) {
                if (backing[static_cast<std::size_t>(y) * stride + byte] != 0xa5) return false;
            }
        }
    }

    // Full bitmap writes take the discard path, retaining the old snapshot.
    sk_sp<SkImage> partialImage = (*surface).makeImageSnapshot();
    if (!partialImage) return false;
    SkBitmap replacement;
    if (!replacement.tryAllocPixels(info)) return false;
    replacement.eraseColor(SK_ColorBLUE);
    (*surface).writePixels(replacement, 0, 0);
    SkBitmap readback;
    if (!readback.tryAllocPixels(info)) return false;
    if (!(*surface).readPixels(readback, 0, 0)) return false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (readback.getColor(x, y) != SK_ColorBLUE) return false;
        }
    }
    SkPixmap partial;
    if (!(*partialImage).peekPixels(&partial)) return false;
    if (partial.getColor(width - 1, height - 1) != SK_ColorRED) return false;
    if (partial.getColor(0, 2) != SkColorSetARGB(255, 60, 0, 90)) return false;
    return true;
}

bool related_surfaces() {
    const SkImageInfo info = image_info();
    sk_sp<SkSurface> source = SkSurfaces::Raster(info);
    if (!source || !(*source).baseRecorder()) return false;
    SkCanvas& sourceCanvas = *(*source).getCanvas();
    sourceCanvas.clear(SK_ColorGREEN);
    sk_sp<SkSurface> target = (*source).makeSurface(width + 2, height + 2);
    if (!target || (*target).width() != width + 2 || (*target).height() != height + 2) return false;
    if ((*source).makeSurface(0, height)) return false;
    SkCanvas& targetCanvas = *(*target).getCanvas();
    targetCanvas.clear(SK_ColorRED);
    (*source).draw(&targetCanvas, 1.0f, 1.0f);
    SkPixmap targetPixels;
    if (!(*target).peekPixels(&targetPixels)) return false;
    for (int y = 0; y < height + 2; ++y) {
        for (int x = 0; x < width + 2; ++x) {
            const bool inside = x >= 1 && x <= width && y >= 1 && y <= height;
            const SkColor expected = inside ? SK_ColorGREEN : SK_ColorRED;
            if (targetPixels.getColor(x, y) != expected) return false;
        }
    }
    return true;
}
}

int main() {
    if (!external_write_preserves_snapshot()) return 10;
    for (int wrapped = 0; wrapped < 2; ++wrapped) {
        for (int discard = 0; discard < 2; ++discard) {
            if (!snapshots(wrapped != 0, discard != 0)) return 1;
        }
    }
    if (!old_allocation_release()) return 2;
    if (!restore_mutability()) return 3;
    if (!invalid_and_null()) return 4;
    if (!callback_reads()) return 5;
    if (!pixel_copies(false) || !pixel_copies(true)) return 6;
    if (!related_surfaces()) return 7;
    std::printf("surface ownership, snapshots, clipped copies, padded rows, callbacks and null behavior; base %zu bytes; PASS\n",
                sizeof(SkSurface_Base));
    return 0;
}
