/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkBitmapCache.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkFourByteTag.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixelRef.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkMutex.h"
#include "include/private/chromium/SkDiscardableMemory.h"
#include "src/core/SkImageInfoPriv.h"
#include "src/core/SkMipmap.h"
#include "src/core/SkNextID.h"
#include "src/core/SkResourceCache.h"
#include "src/image/SkImage_Base.h"

#include <cstddef>
#include <utility>

uint64_t SkMakeResourceCacheSharedIDForBitmap(uint32_t generationID) {
    const uint64_t tag = SkSetFourByteTag('b', 'm', 'a', 'p');
    const uint64_t sharedID = (tag << 32) | generationID;
    return sharedID;
}

void SkNotifyBitmapGenIDIsStale(uint32_t generationID) {
    const uint64_t sharedID = SkMakeResourceCacheSharedIDForBitmap(generationID);
    SkResourceCache::PostPurgeSharedID(sharedID);
}

SkBitmapCacheDesc SkBitmapCacheDesc::Make(uint32_t imageID, const SkIRect& subset) {
    SkASSERT(imageID != 0);
    SkASSERT(subset.width() > 0 && subset.height() > 0);
    const SkBitmapCacheDesc description{.fImageID = imageID, .fSubset = subset};
    return description;
}

SkBitmapCacheDesc SkBitmapCacheDesc::Make(const SkImage* image) {
    const SkIRect bounds = SkIRect::MakeWH((*image).width(), (*image).height());
    const SkBitmapCacheDesc description = Make((*image).uniqueID(), bounds);
    return description;
}

namespace {
unsigned gBitmapKeyNamespaceLabel = 0;
unsigned gMipMapKeyNamespaceLabel = 0;

struct BitmapKey : public SkResourceCache::Key {
    explicit BitmapKey(const SkBitmapCacheDesc& description) : fDesc(description) {
        const uint64_t sharedID = SkMakeResourceCacheSharedIDForBitmap(fDesc.fImageID);
        init(&gBitmapKeyNamespaceLabel, sharedID, sizeof(fDesc));
    }
    const SkBitmapCacheDesc fDesc;
};

struct FreeBlock {
    void operator()(void* block) const { sk_free(block); }
};
using PixelBlock = std::unique_ptr<void, FreeBlock>;
}

// This named friend is the cache's authority to assign immutable pixel identity.
void SkBitmapCache_setImmutableWithID(SkPixelRef* pixels, uint32_t id) {
    (*pixels).setImmutableWithID(id);
}

class SkBitmapCache::Rec : public SkResourceCache::Rec {
public:
    Rec(const SkBitmapCacheDesc& description, const SkImageInfo& info, size_t rowBytes,
        std::unique_ptr<SkDiscardableMemory> memory, PixelBlock block)
        : fKey(description)
        , fDM(std::move(memory))
        , fMalloc(std::move(block))
        , fInfo(info)
        , fRowBytes(rowBytes)
        , fPrUniqueID(SkNextID::ImageID())
        , fDiscardableIsLocked(fDM != nullptr) {
        SkASSERT(static_cast<bool>(fDM) != static_cast<bool>(fMalloc));
    }

    ~Rec() override {
        SkASSERT(fExternalCounter == 0);
        if (fDM && fDiscardableIsLocked) {
            (*fDM).unlock();
        }
    }

    const Key& getKey() const override { return fKey; }
    size_t bytesUsed() const override {
        // The cached pixels were allocated or validated at this extent, so it fits.
        const std::optional<size_t> pixelBytes = fInfo.checkedByteSize(fRowBytes);
        SkASSERT_RELEASE(pixelBytes.has_value());
        const size_t bytes = sizeof(fKey) + *pixelBytes;
        return bytes;
    }
    bool canBePurged() override {
        SkAutoMutexExclusive lock(fMutex);
        const bool unpinned = fExternalCounter == 0;
        return unpinned;
    }
    void postAddInstall(void* payload) override {
        const bool installed = install(static_cast<SkBitmap*>(payload));
        SkASSERT(installed);
        (void)installed;
    }
    const char* getCategory() const override { return "bitmap"; }
    SkDiscardableMemory* diagnostic_only_getDiscardable() const override {
        SkDiscardableMemory* memory = fDM.get();
        return memory;
    }

    static void ReleaseProc(void*, void* context) {
        const Rec& record = *static_cast<const Rec*>(context);
        SkAutoMutexExclusive lock(record.fMutex);
        SkASSERT(record.fExternalCounter > 0);
        --record.fExternalCounter;
        if (record.fDM && record.fExternalCounter == 0) {
            (*record.fDM).unlock();
            record.fDiscardableIsLocked = false;
        }
    }

    bool install(SkBitmap* bitmap) const {
        void* pixels = nullptr;
        {
            SkAutoMutexExclusive lock(fMutex);
            if (!fDM && !fMalloc) {
                return false;
            }
            if (fDM) {
                if (!fDiscardableIsLocked) {
                    SkASSERT(fExternalCounter == 0);
                    if (!(*fDM).lock()) {
                        fDM.reset();
                        return false;
                    }
                    fDiscardableIsLocked = true;
                }
                pixels = (*fDM).data();
            } else {
                pixels = fMalloc.get();
            }
            SkASSERT(pixels != nullptr);
            // This pin keeps the record and pixel address live during installation.
            // installPixels invokes ReleaseProc itself if installation fails.
            ++fExternalCounter;
        }
        SkBitmap replacement{};
        // The callback is synchronous on failure, and on the final pixel-ref
        // release after success. Neither can run with fMutex already held here.
        const bool installed = replacement.installPixels(fInfo, pixels, fRowBytes,
                                                          ReleaseProc, const_cast<Rec*>(this));
        if (!installed) {
            return false;
        }
        SkBitmapCache_setImmutableWithID(replacement.pixelRef(), fPrUniqueID);
        (*bitmap).swap(replacement);
        // Releasing the previous destination may reenter this record's callback.
        return true;
    }

    static bool Finder(const SkResourceCache::Rec& base, void* context) {
        const Rec& record = static_cast<const Rec&>(base);
        SkBitmap* result = static_cast<SkBitmap*>(context);
        const bool installed = record.install(result);
        return installed;
    }

private:
    const BitmapKey fKey;
    mutable SkMutex fMutex{};
    mutable std::unique_ptr<SkDiscardableMemory> fDM;
    const PixelBlock fMalloc;
    const SkImageInfo fInfo;
    const size_t fRowBytes;
    // Lazy images can use the same cache key ID with distinct format entries.
    const uint32_t fPrUniqueID;
    // Includes a reserved pin while a replacement bitmap is being installed.
    mutable int fExternalCounter = 0;
    mutable bool fDiscardableIsLocked;
};

void SkBitmapCache::PrivateDeleteRec(Rec* record) { delete record; }

SkBitmapCache::RecPtr SkBitmapCache::Alloc(const SkBitmapCacheDesc& description,
                                         const SkImageInfo& info, SkPixmap* destination) {
    if (description.fImageID == 0 || description.fSubset.fLeft < 0 ||
        description.fSubset.fTop < 0 || info.width() != description.fSubset.width() ||
        info.height() != description.fSubset.height() ||
        !SkColorTypeIsValid(static_cast<unsigned>(info.colorType())) ||
        !SkAlphaTypeIsValid(static_cast<unsigned>(info.alphaType())) ||
        !SkImageInfoIsValid(info)) {
        return nullptr;
    }
    const size_t rowBytes = info.minRowBytes();
    const std::optional<size_t> pixelBytes = info.checkedByteSize(rowBytes);
    if (!pixelBytes) {
        return nullptr;
    }
    // Validate the metadata once before handing it to the release-callback path.
    SkBitmap metadata{};
    if (!metadata.setInfo(info, rowBytes)) {
        return nullptr;
    }
    std::unique_ptr<SkDiscardableMemory> memory{};
    PixelBlock block{};
    const SkResourceCache::DiscardableFactory factory = SkResourceCache::GetDiscardableFactory();
    void* pixels = nullptr;
    if (factory != nullptr) {
        memory.reset(factory(*pixelBytes));
        if (memory) pixels = (*memory).data();
    } else {
        block.reset(sk_malloc_canfail(*pixelBytes));
        pixels = block.get();
    }
    if (!memory && !block) {
        return nullptr;
    }
    RecPtr record(new Rec(description, info, rowBytes, std::move(memory), std::move(block)));
    (*destination).reset(info, pixels, rowBytes);
    return record;
}

void SkBitmapCache::Add(RecPtr record, SkBitmap* bitmap) {
    Rec* transferred = record.release();
    SkResourceCache::Add(transferred, bitmap);
}

bool SkBitmapCache::Find(const SkBitmapCacheDesc& description, SkBitmap* result) {
    description.validate();
    const BitmapKey key(description);
    const bool found = SkResourceCache::Find(key, Rec::Finder, result);
    return found;
}

namespace {
struct MipMapKey : public SkResourceCache::Key {
    explicit MipMapKey(const SkBitmapCacheDesc& description) : fDesc(description) {
        const uint64_t sharedID = SkMakeResourceCacheSharedIDForBitmap(fDesc.fImageID);
        init(&gMipMapKeyNamespaceLabel, sharedID, sizeof(fDesc));
    }
    const SkBitmapCacheDesc fDesc;
};

// A cache reference differs from a client reference: only a client pins pixels.
class MipMapRec final : public SkResourceCache::Rec {
public:
    MipMapRec(const SkBitmapCacheDesc& description, const SkMipmap& mipmap)
        : fKey(description), fMipMap(mipmap) {
        fMipMap.attachToCacheAndRef();
    }
    MipMapRec(const MipMapRec&) = delete;
    MipMapRec& operator=(const MipMapRec&) = delete;
    ~MipMapRec() override { fMipMap.detachFromCacheAndUnref(); }
    const Key& getKey() const override { return fKey; }
    size_t bytesUsed() const override {
        const size_t bytes = sizeof(fKey) + fMipMap.size();
        return bytes;
    }
    const char* getCategory() const override { return "mipmap"; }
    SkDiscardableMemory* diagnostic_only_getDiscardable() const override {
        SkDiscardableMemory* memory = fMipMap.diagnostic_only_getDiscardable();
        return memory;
    }
    static bool Finder(const SkResourceCache::Rec& base, void* context) {
        const MipMapRec& record = static_cast<const MipMapRec&>(base);
        sk_sp<const SkMipmap> client = sk_ref_sp(&record.fMipMap);
        // Client acquisition attempts to lock discardable data; failed locks
        // report null. The local owner balances the ref on that failure path.
        if ((*client).data() == nullptr) return false;
        const SkMipmap** output = static_cast<const SkMipmap**>(context);
        *output = client.release();
        return true;
    }
private:
    const MipMapKey fKey;
    const SkMipmap& fMipMap;
};
}

const SkMipmap* SkMipmapCache::FindAndRef(const SkBitmapCacheDesc& description,
                                       SkResourceCache* localCache) {
    const MipMapKey key(description);
    const SkMipmap* result = nullptr;
    bool found = false;
    if (localCache != nullptr) {
        found = (*localCache).find(key, MipMapRec::Finder, &result);
    } else {
        found = SkResourceCache::Find(key, MipMapRec::Finder, &result);
    }
    if (!found) return nullptr;
    return result;
}

const SkMipmap* SkMipmapCache::AddAndRef(const SkImage_Base* image, SkResourceCache* localCache) {
    SkBitmap source{};
    if (!(*image).getROPixels(&source)) return nullptr;
    SkResourceCache::DiscardableFactory factory = nullptr;
    if (localCache != nullptr) {
        factory = (*localCache).discardableFactory();
    } else {
        factory = SkResourceCache::GetDiscardableFactory();
    }
    sk_sp<SkMipmap> client(SkMipmap::Build(source, factory));
    if (!client) return nullptr;
    const SkBitmapCacheDesc description = SkBitmapCacheDesc::Make(image);
    MipMapRec* record = new MipMapRec(description, *client);
    if (localCache != nullptr) {
        (*localCache).add(record);
    } else {
        SkResourceCache::Add(record);
    }
    (*image).notifyAddedToRasterCache();
    const SkMipmap* result = client.release();
    return result;
}
