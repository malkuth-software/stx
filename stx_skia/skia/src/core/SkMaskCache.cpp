/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkMaskCache.h"

#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkSize.h"
#include "include/private/SkAssert.h"
#include "src/core/SkCachedData.h"
#include "src/core/SkMask.h"
#include "src/core/SkResourceCache.h"
#include "src/core/SkTLazy.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

class SkDiscardableMemory;
enum SkBlurStyle : int;

namespace {

// The cached descriptor (without pixels) and the record's pixel storage. The record holds
// one cache reference to fData from admission until its destruction.
struct MaskValue {
    SkMask          fMask;
    SkCachedData*   fData;
};

// A null local cache selects the process-wide cache.
bool find_in_cache(SkResourceCache* localCache, const SkResourceCache::Key& key,
                   SkResourceCache::FindVisitor visitor, void* context) {
    if (localCache != nullptr) {
        const bool found = (*localCache).find(key, visitor, context);
        return found;
    }
    const bool found = SkResourceCache::Find(key, visitor, context);
    return found;
}

// The cache takes sole ownership of the record, even when it rejects it.
void add_to_cache(SkResourceCache* localCache, std::unique_ptr<SkResourceCache::Rec> record) {
    if (localCache != nullptr) {
        (*localCache).add(record.release());
        return;
    }
    SkResourceCache::Add(record.release());
}

unsigned gRRectBlurKeyNamespaceLabel = 0;

struct RRectBlurKey : public SkResourceCache::Key {
public:
    RRectBlurKey(SkScalar sigma, const SkRRect& rrect, SkBlurStyle style)
        : fSigma(sigma)
        , fStyle(static_cast<int32_t>(style))
        , fRRect(rrect)
    {
        init(&gRRectBlurKeyNamespaceLabel, 0,
             sizeof(fSigma) + sizeof(fStyle) + sizeof(fRRect));
    }

    SkScalar   fSigma;
    int32_t    fStyle;
    SkRRect    fRRect;
};

struct RRectBlurRec : public SkResourceCache::Rec {
    RRectBlurRec(RRectBlurKey key, const SkMask& mask, SkCachedData* data)
        : fKey(key), fValue({{nullptr, mask.fBounds, mask.fRowBytes, mask.fFormat}, data})
    {
        (*fValue.fData).attachToCacheAndRef();
    }
    ~RRectBlurRec() override {
        (*fValue.fData).detachFromCacheAndUnref();
    }

    RRectBlurKey   fKey;
    MaskValue      fValue;

    const Key& getKey() const override { return fKey; }
    size_t bytesUsed() const override {
        const size_t bytes = sizeof(*this) + (*fValue.fData).size();
        return bytes;
    }
    const char* getCategory() const override { return "rrect-blur"; }
    SkDiscardableMemory* diagnostic_only_getDiscardable() const override {
        SkDiscardableMemory* discardable = (*fValue.fData).diagnostic_only_getDiscardable();
        return discardable;
    }

    // Acquires a client reference for the caller. Discarded pixels make the record stale.
    static bool Visitor(const SkResourceCache::Rec& baseRec, void* contextData) {
        const RRectBlurRec& rec = static_cast<const RRectBlurRec&>(baseRec);
        SkTLazy<MaskValue>& result = *static_cast<SkTLazy<MaskValue>*>(contextData);

        SkCachedData& tmpData = *rec.fValue.fData;
        tmpData.ref();
        if (nullptr == tmpData.data()) {
            tmpData.unref();
            return false;
        }
        result.init(rec.fValue);
        return true;
    }
};
} // namespace

SkCachedData* SkMaskCache::FindAndRef(SkScalar sigma, SkBlurStyle style,
                                      const SkRRect& rrect, SkTLazy<SkMask>* mask,
                                      SkResourceCache* localCache) {
    SkTLazy<MaskValue> result;
    const RRectBlurKey key(sigma, rrect, style);
    if (!find_in_cache(localCache, key, RRectBlurRec::Visitor, &result)) {
        return nullptr;
    }

    const MaskValue& found = *result;
    const uint8_t* pixels = static_cast<const uint8_t*>((*found.fData).data());
    (*mask).init(pixels, found.fMask.fBounds, found.fMask.fRowBytes, found.fMask.fFormat);
    return found.fData;
}

void SkMaskCache::Add(SkScalar sigma, SkBlurStyle style,
                      const SkRRect& rrect, const SkMask& mask, SkCachedData* data,
                      SkResourceCache* localCache) {
    const RRectBlurKey key(sigma, rrect, style);
    std::unique_ptr<SkResourceCache::Rec> record(new RRectBlurRec(key, mask, data));
    add_to_cache(localCache, std::move(record));
}

//////////////////////////////////////////////////////////////////////////////////////////

namespace {
unsigned gRectsBlurKeyNamespaceLabel = 0;

struct RectsBlurKey : public SkResourceCache::Key {
public:
    // fSizes holds the first rectangle's size, the second rectangle's size and the offset
    // between them (zero for one rectangle), and the first rectangle's fractional phase
    // within its rounded-out integer bounds.
    RectsBlurKey(SkScalar sigma, SkBlurStyle style, SkSpan<const SkRect> rects)
            : fSigma(sigma), fStyle(static_cast<int32_t>(style)) {
        SkASSERT_RELEASE(rects.size() == 1 || rects.size() == 2);
        const SkIRect ir = rects[0].roundOut();
        fSizes[0] = SkSize{rects[0].width(), rects[0].height()};
        if (rects.size() == 2) {
            fSizes[1] = SkSize{rects[1].width(), rects[1].height()};
            fSizes[2] = SkSize{rects[0].x() - rects[1].x(), rects[0].y() - rects[1].y()};
        } else {
            fSizes[1] = SkSize{0, 0};
            fSizes[2] = SkSize{0, 0};
        }
        fSizes[3] = SkSize{rects[0].x() - static_cast<SkScalar>(ir.x()),
                           rects[0].y() - static_cast<SkScalar>(ir.y())};

        init(&gRectsBlurKeyNamespaceLabel, 0,
             sizeof(fSigma) + sizeof(fStyle) + sizeof(fSizes));
    }

    SkScalar    fSigma;
    int32_t     fStyle;
    SkSize      fSizes[4] = {};
};

struct RectsBlurRec : public SkResourceCache::Rec {
    RectsBlurRec(RectsBlurKey key, const SkMask& mask, SkCachedData* data)
        : fKey(key), fValue({{nullptr, mask.fBounds, mask.fRowBytes, mask.fFormat}, data})
    {
        (*fValue.fData).attachToCacheAndRef();
    }
    ~RectsBlurRec() override {
        (*fValue.fData).detachFromCacheAndUnref();
    }

    RectsBlurKey   fKey;
    MaskValue      fValue;

    const Key& getKey() const override { return fKey; }
    size_t bytesUsed() const override {
        const size_t bytes = sizeof(*this) + (*fValue.fData).size();
        return bytes;
    }
    const char* getCategory() const override { return "rects-blur"; }
    SkDiscardableMemory* diagnostic_only_getDiscardable() const override {
        SkDiscardableMemory* discardable = (*fValue.fData).diagnostic_only_getDiscardable();
        return discardable;
    }

    // Acquires a client reference for the caller. Discarded pixels make the record stale.
    static bool Visitor(const SkResourceCache::Rec& baseRec, void* contextData) {
        const RectsBlurRec& rec = static_cast<const RectsBlurRec&>(baseRec);
        SkTLazy<MaskValue>& result = *static_cast<SkTLazy<MaskValue>*>(contextData);

        SkCachedData& tmpData = *rec.fValue.fData;
        tmpData.ref();
        if (nullptr == tmpData.data()) {
            tmpData.unref();
            return false;
        }
        result.init(rec.fValue);
        return true;
    }
};
} // namespace

SkCachedData* SkMaskCache::FindAndRef(SkScalar sigma,
                                      SkBlurStyle style,
                                      SkSpan<const SkRect> rects,
                                      SkTLazy<SkMask>* mask,
                                      SkResourceCache* localCache) {
    SkTLazy<MaskValue> result;
    const RectsBlurKey key(sigma, style, rects);
    if (!find_in_cache(localCache, key, RectsBlurRec::Visitor, &result)) {
        return nullptr;
    }

    const MaskValue& found = *result;
    const uint8_t* pixels = static_cast<const uint8_t*>((*found.fData).data());
    (*mask).init(pixels, found.fMask.fBounds, found.fMask.fRowBytes, found.fMask.fFormat);
    return found.fData;
}

void SkMaskCache::Add(SkScalar sigma,
                      SkBlurStyle style,
                      SkSpan<const SkRect> rects,
                      const SkMask& mask,
                      SkCachedData* data,
                      SkResourceCache* localCache) {
    const RectsBlurKey key(sigma, style, rects);
    std::unique_ptr<SkResourceCache::Rec> record(new RectsBlurRec(key, mask, data));
    add_to_cache(localCache, std::move(record));
}
