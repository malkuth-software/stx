/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkPixelRef.h"

#include "include/private/SkAssert.h"
#include "include/private/SkDebug.h"
#include "include/private/SkPixelStorage.h"
#include "src/core/SkBitmapCache.h"
#include "src/core/SkNextID.h"
#include "src/core/SkPixelRefPriv.h"

#include <atomic>
#include <utility>

uint32_t SkNextID::ImageID() {
    // Reserve the low bit for SkPixelRef's unique-generation tag.
    static std::atomic<uint32_t> nextID{2};

    uint32_t id = 0;
    do {
        id = nextID.fetch_add(2, std::memory_order_relaxed);
    } while (id == 0);
    return id;
}

SkPixelRef::SkPixelRef(int width, int height, void* pixels, size_t rowBytes)
    : SkPixelStorage(SkPixelStorage::Type::kPixelRef)
    , fWidth(width)
    , fHeight(height)
    , fPixels(pixels)
    , fRowBytes(rowBytes)
    , fTaggedGenID(0)
    , fGenIDChangeListeners()
    , fAddedToCache(false)
    , fMutability(kMutable) {}

SkPixelRef::~SkPixelRef() {
    callGenIDChangeListeners();
}

// This is undefined if there are clients in-flight trying to use us
void SkPixelRef::android_only_reset(int width, int height, size_t rowBytes) {
    fWidth = width;
    fHeight = height;
    fRowBytes = rowBytes;
    // note: we do not change fPixels

    // conservative, since its possible the "new" settings are the same as the old.
    notifyPixelsChanged();
}

void SkPixelRef::needsNewGenID() {
    fTaggedGenID.store(0);
    SkASSERT(!genIDIsUnique()); // This method isn't threadsafe, so the assert should be fine.
}

uint32_t SkPixelRef::getGenerationID() const {
    uint32_t id = fTaggedGenID.load();
    if (id == 0) {
        const uint32_t identity = SkNextID::ImageID();
        const uint32_t next = identity | 1U;
        if (fTaggedGenID.compare_exchange_strong(id, next)) {
            id = next;
        }
        // On a lost race, compare_exchange_strong has filled id with the winner.
        // The winning generation may be shared, so its unique tag is not asserted.
    }
    // The unique tag is private; callers receive only the identity.
    const uint32_t generation = id & ~1U;
    return generation;
}

void SkPixelRef::addGenIDChangeListener(sk_sp<SkIDChangeListener> listener) {
    if (!listener || !genIDIsUnique()) {
        // No point in tracking this if we're not going to call it.
        return;
    }
    SkASSERT(!(*listener).shouldDeregister());
    fGenIDChangeListeners.add(std::move(listener));
}

// Notify while the old generation is still available to cache invalidation.
void SkPixelRef::callGenIDChangeListeners() {
    // We don't invalidate ourselves if we think another SkPixelRef is sharing our genID.
    if (genIDIsUnique()) {
        fGenIDChangeListeners.changed();
        if (fAddedToCache.exchange(false)) {
            const uint32_t generation = getGenerationID();
            SkNotifyBitmapGenIDIsStale(generation);
        }
    } else {
        // A registration has one opportunity; shared generations discard it.
        fGenIDChangeListeners.reset();
    }
}

void SkPixelRef::notifyPixelsChanged() {
#ifdef SK_DEBUG
    if (isImmutable()) {
        SkDebugf("========== notifyPixelsChanged called on immutable pixelref");
    }
#endif
    callGenIDChangeListeners();
    needsNewGenID();
}

void SkPixelRef::setImmutable() {
    fMutability = kImmutable;
}

void SkPixelRef::setImmutableWithID(uint32_t genID) {
    /*
     *  We are forcing the genID to match an external value. The caller must ensure that this
     *  value does not conflict with other content.
     *
     *  One use is to force this pixelref's id to match an SkImage's id
     */
    fMutability = kImmutable;
    fTaggedGenID.store(genID);
}

void SkPixelRef::setTemporarilyImmutable() {
    SkASSERT(fMutability != kImmutable);
    fMutability = kTemporarilyImmutable;
}

void SkPixelRef::restoreMutability() {
    SkASSERT(fMutability != kImmutable);
    fMutability = kMutable;
}

namespace {
// The callback owns the pixel allocation. It runs before base-class cache
// invalidation, so change listeners must never inspect the released pixels.
class CallbackPixelRef final : public SkPixelRef {
public:
    CallbackPixelRef(int width, int height, void* pixels, size_t row_bytes,
                     void (*release)(void*, void*), void* context)
        : SkPixelRef(width, height, pixels, row_bytes),
          fRelease(release), fContext(context) {}

    ~CallbackPixelRef() override {
        fRelease(pixels(), fContext);
    }

private:
    void (*fRelease)(void*, void*);
    void* fContext;
};
}

sk_sp<SkPixelRef> SkMakePixelRefWithProc(int width, int height, size_t row_bytes,
                                      void* pixels, void (*release)(void*, void*),
                                      void* context) {
    SkASSERT(width >= 0 && height >= 0);
    if (release == nullptr) {
        sk_sp<SkPixelRef> result = sk_make_sp<SkPixelRef>(width, height, pixels, row_bytes);
        return result;
    }
    sk_sp<SkPixelRef> result(new CallbackPixelRef(width, height, pixels, row_bytes,
                                               release, context));
    return result;
}
