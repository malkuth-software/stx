// SkBorrowedData lends caller-owned pixels to an image for one scope. The contract draws
// through a borrow and ends it cleanly; the stop mode keeps an image past the borrow, which
// the destructor's Release check must stop.

#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kSide = 4;
using Pixels = std::array<uint32_t, kSide * kSide>;

// Fills target with source's pixels drawn through a borrow, and returns the image made from
// the borrow when keep is true (the stop mode), else null after the borrow has ended.
sk_sp<SkImage> draw_borrowed(const Pixels& source, Pixels& target, bool keep) {
    const SkImageInfo info = SkImageInfo::MakeN32Premul(kSide, kSide);
    const size_t rowBytes = kSide * sizeof(uint32_t);
    sk_sp<SkImage> kept{};
    {
        const SkBorrowedData borrowed(source.data(), source.size() * sizeof(uint32_t));
        sk_sp<SkImage> image = SkImages::RasterFromData(info, borrowed.data(), rowBytes);
        if (!image) return nullptr;
        const sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, target.data(), rowBytes);
        if (!surface) return nullptr;
        SkPaint paint{};
        paint.setBlendMode(SkBlendMode::kSrc);
        (*(*surface).getCanvas()).drawImage(image.get(), 0, 0, SkSamplingOptions(), &paint);
        if (keep) kept = image;
    }
    return kept;
}

bool contract() {
    Pixels source{};
    for (size_t index = 0; index < source.size(); ++index) {
        source[index] = 0xff000000U | static_cast<uint32_t>(index * 0x010203U);
    }
    Pixels target{};
    const sk_sp<SkImage> kept = draw_borrowed(source, target, false);
    if (kept) return false;
    const bool same = std::memcmp(source.data(), target.data(), sizeof(Pixels)) == 0;
    return same;
}

int stop_mode(const char* mode) {
    if (std::strcmp(mode, "--outlive") != 0) return 2;
    Pixels source{};
    Pixels target{};
    // Ends the borrow while an image still holds the data: the destructor stops here.
    const sk_sp<SkImage> kept = draw_borrowed(source, target, true);
    return kept ? 0 : 3;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2) return stop_mode(argv[1]);
    if (argc != 1) return 2;
    if (!contract()) {
        std::printf("borrowed data: FAIL\n");
        return 1;
    }
    std::printf("borrowed data: PASS\n");
    return 0;
}
