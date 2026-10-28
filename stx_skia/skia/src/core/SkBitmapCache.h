/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkBitmapCache_DEFINED
#define SkBitmapCache_DEFINED

#include "include/core/SkRect.h"
#include "include/private/SkAssert.h"

#include <cstdint>
#include <memory>

class SkBitmap;
class SkImage;
class SkImage_Base;
class SkMipmap;
class SkPixmap;
class SkResourceCache;
struct SkImageInfo;

uint64_t SkMakeResourceCacheSharedIDForBitmap(uint32_t bitmapGenID);

void SkNotifyBitmapGenIDIsStale(uint32_t bitmapGenID);

struct SkBitmapCacheDesc {
    // Make supplies a nonzero identity and positive bounds; default is invalid.
    uint32_t fImageID = 0;
    SkIRect fSubset{};

    void validate() const {
        SkASSERT(fImageID);
        SkASSERT(fSubset.fLeft >= 0 && fSubset.fTop >= 0);
        SkASSERT(fSubset.width() > 0 && fSubset.height() > 0);
    }

    static SkBitmapCacheDesc Make(const SkImage*);
    static SkBitmapCacheDesc Make(uint32_t genID, const SkIRect& subset);
};

class SkBitmapCache {
public:
    // The global cache serializes lookup/publication. A successful lookup pins
    // pixels through the returned bitmap's pixel ref. Failure leaves it unchanged.
    // A populated result may be replaced, including one already using this entry.
    static bool Find(const SkBitmapCacheDesc&, SkBitmap* result);

    class Rec;
    struct RecDeleter { void operator()(Rec* r) { PrivateDeleteRec(r); } };
    using RecPtr = std::unique_ptr<Rec, RecDeleter>;

    // Allocate writable pixels owned by the pending record. Failure leaves the
    // pixmap unchanged. Fill pixels before Add; do not mutate after publication.
    // Destroying an unpublished record releases its block.
    static RecPtr Alloc(const SkBitmapCacheDesc&, const SkImageInfo&, SkPixmap*);
    // Consumes a nonnull record. A pinned duplicate key reuses the existing entry
    // and discards the new pixels. The returned bitmap owns a pin in either case.
    static void Add(RecPtr, SkBitmap*);

private:
    static void PrivateDeleteRec(Rec*);
};

class SkMipmapCache {
public:
    // Return one owned client reference, or null. Release through unref/sk_sp.
    // Local caches require caller serialization; null selects the global cache.
    // Source pixels are borrowed synchronously; the mipmap stores its own pixels.
    static const SkMipmap* FindAndRef(const SkBitmapCacheDesc&,
                                      SkResourceCache* localCache = nullptr);
    static const SkMipmap* AddAndRef(const SkImage_Base*,
                                     SkResourceCache* localCache = nullptr);
};

#endif
