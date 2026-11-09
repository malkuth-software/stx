#include "include/core/SkBitmap.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImage.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "include/private/chromium/SkDiscardableMemory.h"
#include "include/private/SkMalloc.h"
#include "src/core/SkBitmapCache.h"
#include "src/core/SkMipmap.h"
#include "src/core/SkNextID.h"
#include "src/core/SkResourceCache.h"
#include "src/image/SkImage_Base.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <memory>

namespace {
struct ClearCache {
    ClearCache() { SkResourceCache::PurgeAll(); }
    ~ClearCache() { SkResourceCache::PurgeAll(); }
};

SkImageInfo info() {
    const SkImageInfo result = SkImageInfo::Make(8, 8, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    return result;
}

bool bitmap_entries() {
    ClearCache cleanup;
    const SkImageInfo imageInfo = info();
    const SkBitmapCacheDesc description = SkBitmapCacheDesc::Make(SkNextID::ImageID(),
                                                                 imageInfo.bounds());
    SkPixmap pixels{};
    SkBitmapCache::RecPtr record = SkBitmapCache::Alloc(description, imageInfo, &pixels);
    if (!record || !pixels.erase(SK_ColorGREEN)) return false;
    SkBitmap first{};
    SkBitmapCache::Add(std::move(record), &first);
    if (record || !first.isImmutable() || first.getColor(0, 0) != SK_ColorGREEN) return false;
    const uint32_t generation = first.getGenerationID();
    SkBitmap second{};
    if (!SkBitmapCache::Find(description, &second)) return false;
    if (second.getGenerationID() != generation || second.getPixels() != first.getPixels()) return false;
#ifndef STX_SKIA_REFERENCE
    // Replacing a pin to the same entry must release the old pixel ref outside
    // the record mutex. The original implementation recursively takes that mutex.
    for (int repeat = 0; repeat < 10; ++repeat) {
        if (!SkBitmapCache::Find(description, &second)) return false;
        if (second.getGenerationID() != generation) return false;
    }
#endif
    SkPixmap duplicatePixels{};
    SkBitmapCache::RecPtr duplicate = SkBitmapCache::Alloc(description, imageInfo, &duplicatePixels);
    if (!duplicate || !duplicatePixels.erase(SK_ColorBLUE)) return false;
    SkBitmap duplicateBitmap{};
    SkBitmapCache::Add(std::move(duplicate), &duplicateBitmap);
    if (duplicateBitmap.getColor(0, 0) != SK_ColorGREEN) return false;
    if (duplicateBitmap.getGenerationID() != generation) return false;
    SkResourceCache::PurgeAll();
    SkBitmap stillPinned{};
    if (!SkBitmapCache::Find(description, &stillPinned)) return false;
    const SkBitmapCacheDesc absent = SkBitmapCacheDesc::Make(SkNextID::ImageID(), imageInfo.bounds());
    if (SkBitmapCache::Find(absent, &stillPinned)) return false;
    if (stillPinned.getGenerationID() != generation) return false;
    first.reset();
    second.reset();
    duplicateBitmap.reset();
    stillPinned.reset();
    SkResourceCache::PurgeAll();
    if (SkBitmapCache::Find(description, &first)) return false;

    // An unpinned entry with the same key may instead be replaced by new pixels.
    SkPixmap replacementPixels{};
    SkBitmapCache::RecPtr replacement = SkBitmapCache::Alloc(description, imageInfo, &replacementPixels);
    if (!replacement || !replacementPixels.erase(SK_ColorBLUE)) return false;
    SkBitmapCache::Add(std::move(replacement), &first);
    if (first.getColor(0, 0) != SK_ColorBLUE || first.getGenerationID() == generation) return false;
    const uint32_t blueGeneration = first.getGenerationID();
    first.reset();
    SkPixmap finalPixels{};
    SkBitmapCache::RecPtr finalRecord = SkBitmapCache::Alloc(description, imageInfo, &finalPixels);
    if (!finalRecord || !finalPixels.erase(SK_ColorRED)) return false;
    SkBitmapCache::Add(std::move(finalRecord), &first);
    if (first.getColor(0, 0) != SK_ColorRED || first.getGenerationID() == blueGeneration) return false;
    first.reset();
    SkNotifyBitmapGenIDIsStale(description.fImageID);
    SkResourceCache::CheckMessages();
    if (SkBitmapCache::Find(description, &first)) return false;
    return true;
}

bool mipmap_entries(bool local) {
    ClearCache cleanup;
    SkResourceCache localCache(static_cast<size_t>(1024 * 1024));
    SkResourceCache* cache = local ? &localCache : nullptr;
    const SkImageInfo imageInfo = info();
    alignas(8) std::array<uint8_t, 8 * 8 * 4> storage{};
    const SkPixmap source(imageInfo, storage.data(), 32);
    if (!source.erase(SK_ColorRED)) return false;
    sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(source);
    if (!image) return false;
    const SkBitmapCacheDesc description = SkBitmapCacheDesc::Make(image.get());
    sk_sp<const SkMipmap> missing(SkMipmapCache::FindAndRef(description, cache));
    if (missing) return false;
    sk_sp<const SkMipmap> created(SkMipmapCache::AddAndRef(as_IB(image), cache));
    if (!created) return false;
    sk_sp<const SkMipmap> found(SkMipmapCache::FindAndRef(description, cache));
    if (!found || found.get() != created.get()) return false;
    if (cache != nullptr) {
        (*cache).purgeAll();
    } else {
        SkResourceCache::PurgeAll();
    }
    // Removing the cache's ownership must leave both client references readable.
    SkMipmap::Level selected{};
    if (!(*found).getLevel(0, &selected)) return false;
    if (selected.fPixmap.getColor(0, 0) != SK_ColorRED) return false;
    missing.reset(SkMipmapCache::FindAndRef(description, cache));
    if (missing) return false;
    return true;
}

// The local factory interface has no context. This single-threaded fixture's
// counters outlive the local cache and every returned client allocation.
int discardedBlocks = 0;
bool discardTransitionsValid = true;

struct FreeMemory {
    void operator()(void* memory) const { sk_free(memory); }
};

class LostMemory final : public SkDiscardableMemory {
public:
    explicit LostMemory(size_t bytes) : fStorage(sk_malloc_throw(bytes)) {}
    ~LostMemory() override {
        discardTransitionsValid = discardTransitionsValid && !fLocked;
        ++discardedBlocks;
    }
    bool lock() override {
        discardTransitionsValid = discardTransitionsValid && !fLocked;
        return false; // Every unlocked block is discarded in this fixture.
    }
    void* data() override {
        discardTransitionsValid = discardTransitionsValid && fLocked;
        void* pixels = fStorage.get();
        return pixels;
    }
    void unlock() override {
        discardTransitionsValid = discardTransitionsValid && fLocked;
        fLocked = false;
        fStorage.reset();
    }
private:
    std::unique_ptr<void, FreeMemory> fStorage;
    bool fLocked = true;
};

SkDiscardableMemory* allocate_discardable(size_t bytes) {
    SkDiscardableMemory* memory = new LostMemory(bytes);
    return memory;
}

bool lost_mipmap() {
    ClearCache cleanup;
    discardedBlocks = 0;
    discardTransitionsValid = true;
    SkResourceCache cache(allocate_discardable);
    const SkImageInfo imageInfo = info();
    alignas(8) std::array<uint8_t, 256> storage{};
    const SkPixmap source(imageInfo, storage.data(), 32);
    if (!source.erase(SK_ColorGREEN)) return false;
    sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(source);
    if (!image) return false;
    const SkBitmapCacheDesc description = SkBitmapCacheDesc::Make(image.get());
    sk_sp<const SkMipmap> client(SkMipmapCache::AddAndRef(as_IB(image), &cache));
    if (!client) return false;
    client.reset();
    client.reset(SkMipmapCache::FindAndRef(description, &cache));
    if (client || discardedBlocks != 1 || !discardTransitionsValid) return false;
    client.reset(SkMipmapCache::FindAndRef(description, &cache));
    if (client) return false;
    return true;
}

bool invalid_allocation() {
#ifndef STX_SKIA_REFERENCE
    const SkImageInfo imageInfo = info();
    const SkBitmapCacheDesc description = SkBitmapCacheDesc::Make(SkNextID::ImageID(),
                                                                 imageInfo.bounds());
    alignas(8) std::array<uint8_t, 256> storage{};
    SkPixmap output(imageInfo, storage.data(), 32);
    const std::array<SkImageInfo, 3> invalid{
        imageInfo.makeWH(7, 8), imageInfo.makeColorType(kUnknown_SkColorType),
        imageInfo.makeWH(-1, 8)};
    for (const SkImageInfo& invalidInfo : invalid) {
        SkBitmapCache::RecPtr record = SkBitmapCache::Alloc(description, invalidInfo, &output);
        if (record || output.addr() != storage.data() || output.width() != 8) return false;
    }
#endif
    return true;
}
}

int main() {
    if (!bitmap_entries()) return 1;
    if (!mipmap_entries(false) || !mipmap_entries(true)) return 2;
    if (!invalid_allocation()) return 3;
    if (!lost_mipmap()) return 4;
    std::printf("bitmap cache: ownership, replacement, duplicate admission, stale keys and mipmap clients PASS\n");
    return 0;
}
