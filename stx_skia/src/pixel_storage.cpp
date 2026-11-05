#include "stx_skia/pixel_storage.hpp"
#include <new>
#include <utility>

namespace stx_skia {
namespace {
void release_pixel_vector(const void*, void* context) {
    std::vector<std::byte>* owner = static_cast<std::vector<std::byte>*>(context);
    delete owner;
}
}

sk_sp<SkData> take_pixel_data(std::vector<std::byte>& pixels) {
    if (pixels.empty()) {
        sk_sp<SkData> empty = SkData::MakeEmpty();
        return empty;
    }
    std::vector<std::byte>* owner = new (std::nothrow) std::vector<std::byte>();
    if (owner == nullptr) return nullptr;
    // SkData follows Skia's fail-fast allocation policy. No pixel copy occurs.
    sk_sp<SkData> data = SkData::MakeWithProc(
        pixels.data(), pixels.size(), release_pixel_vector, owner);
    *owner = std::move(pixels);
    pixels.clear();
    return data;
}
}
