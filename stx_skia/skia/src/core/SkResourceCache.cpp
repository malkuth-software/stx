/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkResourceCache.h"

#include "include/core/SkString.h"
#include "include/core/SkTraceMemoryDump.h"
#include "include/core/SkTypes.h"
#include "include/private/SkDebug.h"
#include "include/private/SkMalloc.h"
#include "src/core/SkCachedData.h"
#include "src/core/SkChecksum.h"
#include "src/core/SkSynchronizedResourceCache.h"
#include "src/core/SkTHash.h"

#if defined(SK_USE_DISCARDABLE_SCALEDIMAGECACHE)
#include "include/private/chromium/SkDiscardableMemory.h"
#endif

#include <algorithm>
#include <limits>
#include <memory>

using namespace skia_private;

// This can be defined by the caller's build system
//#define SK_USE_DISCARDABLE_SCALEDIMAGECACHE

#ifndef SK_DISCARDABLEMEMORY_SCALEDIMAGECACHE_COUNT_LIMIT
#   define SK_DISCARDABLEMEMORY_SCALEDIMAGECACHE_COUNT_LIMIT   1024
#endif

#ifndef SK_DEFAULT_IMAGE_CACHE_LIMIT
    #define SK_DEFAULT_IMAGE_CACHE_LIMIT     (32 * 1024 * 1024)
#endif

void SkResourceCache::Key::init(void* nameSpace, uint64_t sharedID, size_t dataSize) {
    constexpr size_t unhashedBytes = 2 * sizeof(uint32_t);
    constexpr size_t localWords = 4 + sizeof(void*) / sizeof(uint32_t);
    static_assert(sizeof(Key) == localWords * sizeof(uint32_t));
    static_assert(sizeof(Key) == offsetof(Key, fNamespace) + sizeof(fNamespace));
    constexpr size_t maximumWords = static_cast<size_t>(std::numeric_limits<int32_t>::max());
    if (dataSize % sizeof(uint32_t) != 0 ||
        dataSize / sizeof(uint32_t) > maximumWords - localWords) {
        SK_ABORT("Resource cache key extent is invalid");
    }
    const size_t words = localWords + dataSize / sizeof(uint32_t);
    if (words > std::numeric_limits<size_t>::max() / sizeof(uint32_t)) {
        SK_ABORT("Resource cache key byte extent overflows");
    }
    fCount32 = static_cast<int32_t>(words);
    fSharedID_lo = static_cast<uint32_t>(sharedID);
    fSharedID_hi = static_cast<uint32_t>(sharedID >> 32);
    fNamespace = nameSpace;
    // Byte views inspect the complete initialized object representation, including
    // the subclass payload, without reading pointer fields as uint32_t objects.
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(this);
    const size_t hashedBytes = words * sizeof(uint32_t) - unhashedBytes;
    fHash = SkChecksum::Hash32(bytes + unhashedBytes, hashedBytes);
}

namespace {
    struct HashTraits {
        static uint32_t Hash(const SkResourceCache::Key& key) {
            const uint32_t hash = key.hash();
            return hash;
        }
        static const SkResourceCache::Key& GetKey(const SkResourceCache::Rec* rec) {
            const SkResourceCache::Key& key = (*rec).getKey();
            return key;
        }
    };
}  // namespace

class SkResourceCache::Hash :
    public THashTable<SkResourceCache::Rec*, SkResourceCache::Key, HashTraits> {};

///////////////////////////////////////////////////////////////////////////////

SkResourceCache::SkResourceCache(DiscardableFactory factory)
        : fHash(std::make_unique<Hash>())
        , fDiscardableFactory(factory)
        , fTotalByteLimit(0) {}

SkResourceCache::SkResourceCache(size_t byteLimit)
        : fHash(std::make_unique<Hash>())
        , fDiscardableFactory(nullptr)
        , fTotalByteLimit(byteLimit) {}

SkResourceCache::~SkResourceCache() {
    Rec* rec = fHead;
    while (rec) {
        Rec* next = (*rec).fNext;
        delete rec;
        rec = next;
    }
}

////////////////////////////////////////////////////////////////////////////////

bool SkResourceCache::find(const Key& key, FindVisitor visitor, void* context) {
    checkMessages();

    Rec** found = (*fHash).find(key);
    if (found == nullptr) {
        return false;
    }
    Rec* rec = *found;
    const bool valid = visitor(*rec, context);
    if (valid) {
        moveToHead(rec);
    } else {
        remove(rec);
    }
    return valid;
}

void SkResourceCache::add(Rec* rec, void* payload) {
    // The raw entry point transfers ownership immediately; the list takes it
    // only after admission. Rejection and pinned duplicates destroy this owner.
    std::unique_ptr<Rec> pending(rec);
    checkMessages();
    if (!pending) {
        return;
    }
    const Key& key = (*rec).getKey();
    Rec** found = (*fHash).find(key);
    Rec* previous = found == nullptr ? nullptr : *found;
    if (previous != nullptr && !(*previous).canBePurged()) {
        (*previous).postAddInstall(payload);
        return;
    }
    const size_t replacedBytes = previous == nullptr ? 0 : (*previous).bytesUsed();
    const size_t retainedBytes = fTotalBytesUsed - replacedBytes;
    const size_t addedBytes = (*rec).bytesUsed();
    if (addedBytes > std::numeric_limits<size_t>::max() - retainedBytes ||
        (previous == nullptr && fCount == std::numeric_limits<int>::max())) {
        return;
    }
    if (previous != nullptr) {
        remove(previous);
    }
    addToHead(rec);
    (*fHash).set(rec);
    pending.release();  // Intrusive list now owns the record.
    (*rec).postAddInstall(payload);
    purgeAsNeeded();
}

void SkResourceCache::remove(Rec* rec) {
    SkASSERT((*rec).canBePurged());
    const size_t used = (*rec).bytesUsed();
    SkASSERT(used <= fTotalBytesUsed);

    unlink(rec);
    const Key& key = (*rec).getKey();
    (*fHash).remove(key);

    fTotalBytesUsed -= used;
    fCount -= 1;

    delete rec;
}

void SkResourceCache::purgeAsNeeded(bool forcePurge) {
    size_t byteLimit = fTotalByteLimit;
    int countLimit = std::numeric_limits<int>::max();

    if (fDiscardableFactory) {
        countLimit = SK_DISCARDABLEMEMORY_SCALEDIMAGECACHE_COUNT_LIMIT;
        byteLimit = UINT32_MAX;  // Historical discardable-mode threshold.
    }

    Rec* rec = fTail;
    while (rec) {
        if (!forcePurge && fTotalBytesUsed < byteLimit && fCount < countLimit) {
            break;
        }

        Rec* prev = (*rec).fPrev;
        if ((*rec).canBePurged()) {
            remove(rec);
        }
        rec = prev;
    }
}

void SkResourceCache::purgeSharedID(uint64_t sharedID) {
    if (0 == sharedID) {
        return;
    }

    // go backwards, just like purgeAsNeeded, just to make the code similar.
    // could iterate either direction and still be correct.
    Rec* rec = fTail;
    while (rec) {
        Rec* prev = (*rec).fPrev;
        const Key& key = (*rec).getKey();
        if (key.getSharedID() == sharedID) {
            // even though the "src" is now dead, caches could still be in-flight, so
            // we have to check if it can be removed.
            if ((*rec).canBePurged()) {
                remove(rec);
            }
        }
        rec = prev;
    }
}

void SkResourceCache::visitAll(Visitor visitor, void* context) {
    // go backwards, just like purgeAsNeeded, just to make the code similar.
    // could iterate either direction and still be correct.
    Rec* rec = fTail;
    while (rec) {
        visitor(*rec, context);
        rec = (*rec).fPrev;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////

size_t SkResourceCache::setTotalByteLimit(size_t newLimit) {
    const size_t prevLimit = fTotalByteLimit;
    fTotalByteLimit = newLimit;
    if (newLimit < prevLimit) {
        purgeAsNeeded();
    }
    return prevLimit;
}

SkCachedData* SkResourceCache::newCachedData(size_t bytes) {
    checkMessages();

    if (fDiscardableFactory != nullptr) {
        SkDiscardableMemory* memory = fDiscardableFactory(bytes);
        if (memory == nullptr) {
            return nullptr;
        }
        SkCachedData* data = new SkCachedData(bytes, memory);
        return data;
    }
    void* storage = sk_malloc_throw(bytes);
    SkCachedData* data = new SkCachedData(storage, bytes);
    return data;
}

///////////////////////////////////////////////////////////////////////////////

void SkResourceCache::unlink(Rec* rec) {
    Rec* prev = (*rec).fPrev;
    Rec* next = (*rec).fNext;

    if (!prev) {
        SkASSERT(fHead == rec);
        fHead = next;
    } else {
        (*prev).fNext = next;
    }

    if (!next) {
        fTail = prev;
    } else {
        (*next).fPrev = prev;
    }

    (*rec).fNext = nullptr;
    (*rec).fPrev = nullptr;
}

void SkResourceCache::moveToHead(Rec* rec) {
    if (fHead == rec) {
        return;
    }

    SkASSERT(fHead);
    SkASSERT(fTail);

    validate();

    unlink(rec);

    (*fHead).fPrev = rec;
    (*rec).fNext = fHead;
    fHead = rec;

    validate();
}

void SkResourceCache::addToHead(Rec* rec) {
    validate();

    (*rec).fPrev = nullptr;
    (*rec).fNext = fHead;
    if (fHead) {
        (*fHead).fPrev = rec;
    }
    fHead = rec;
    if (!fTail) {
        fTail = rec;
    }
    fTotalBytesUsed += (*rec).bytesUsed();
    fCount += 1;

    validate();
}

///////////////////////////////////////////////////////////////////////////////

#ifdef SK_DEBUG
void SkResourceCache::validate() const {
    if (nullptr == fHead) {
        SkASSERT(nullptr == fTail);
        SkASSERT(0 == fTotalBytesUsed);
        SkASSERT(0 == fCount);
        return;
    }

    if (fHead == fTail) {
        SkASSERT(nullptr == (*fHead).fPrev);
        SkASSERT(nullptr == (*fHead).fNext);
        SkASSERT((*fHead).bytesUsed() == fTotalBytesUsed);
        SkASSERT(1 == fCount);
        return;
    }

    SkASSERT(nullptr == (*fHead).fPrev);
    SkASSERT((*fHead).fNext);
    SkASSERT(nullptr == (*fTail).fNext);
    SkASSERT((*fTail).fPrev);

    size_t used = 0;
    int count = 0;
    const Rec* rec = fHead;
    while (rec) {
        count += 1;
        used += (*rec).bytesUsed();
        SkASSERT(used <= fTotalBytesUsed);
        rec = (*rec).fNext;
    }
    SkASSERT(fCount == count);

    rec = fTail;
    while (rec) {
        SkASSERT(count > 0);
        count -= 1;
        SkASSERT(used >= (*rec).bytesUsed());
        used -= (*rec).bytesUsed();
        rec = (*rec).fPrev;
    }

    SkASSERT(0 == count);
    SkASSERT(0 == used);
}
#endif

void SkResourceCache::dump() const {
    validate();

    SkDebugf("SkResourceCache: count=%d bytes=%zu %s\n",
             fCount, fTotalBytesUsed, fDiscardableFactory ? "discardable" : "malloc");
}

size_t SkResourceCache::setSingleAllocationByteLimit(size_t newLimit) {
    const size_t oldLimit = fSingleAllocationByteLimit;
    fSingleAllocationByteLimit = newLimit;
    return oldLimit;
}

size_t SkResourceCache::getSingleAllocationByteLimit() const {
    return fSingleAllocationByteLimit;
}

size_t SkResourceCache::getEffectiveSingleAllocationByteLimit() const {
    // fSingleAllocationByteLimit == 0 means the caller is asking for our default
    size_t limit = fSingleAllocationByteLimit;

    // if we're not discardable (i.e. we are fixed-budget) then cap the single-limit
    // to our budget.
    if (nullptr == fDiscardableFactory) {
        if (0 == limit) {
            limit = fTotalByteLimit;
        } else {
            limit = std::min(limit, fTotalByteLimit);
        }
    }
    return limit;
}

void SkResourceCache::checkMessages() {
    fPurgeSharedIDInbox.poll(fPurgeSharedIDs);
    for (const uint64_t sharedID : fPurgeSharedIDs) {
        SkResourceCache::purgeSharedID(sharedID);
    }
    fPurgeSharedIDs.clear();
}

///////////////////////////////////////////////////////////////////////////////

static SkSynchronizedResourceCache& get_cache() {
#if defined(SK_USE_DISCARDABLE_SCALEDIMAGECACHE)
    static SkSynchronizedResourceCache* gResourceCache =
            new SkSynchronizedResourceCache(SkDiscardableMemory::Create);
#else
    static SkSynchronizedResourceCache* gResourceCache =
            new SkSynchronizedResourceCache(SK_DEFAULT_IMAGE_CACHE_LIMIT);
#endif
    // Deliberately process-lifetime: pixel/ref destructors may post during shutdown.
    return *gResourceCache;
}

size_t SkResourceCache::GetTotalBytesUsed() {
    const size_t result = get_cache().getTotalBytesUsed();
    return result;
}

size_t SkResourceCache::GetTotalByteLimit() {
    const size_t result = get_cache().getTotalByteLimit();
    return result;
}

size_t SkResourceCache::SetTotalByteLimit(size_t newLimit) {
    const size_t result = get_cache().setTotalByteLimit(newLimit);
    return result;
}

SkResourceCache::DiscardableFactory SkResourceCache::GetDiscardableFactory() {
    const DiscardableFactory result = get_cache().discardableFactory();
    return result;
}

SkCachedData* SkResourceCache::NewCachedData(size_t bytes) {
    SkCachedData* result = get_cache().newCachedData(bytes);
    return result;
}

void SkResourceCache::Dump() {
    get_cache().dump();
}

size_t SkResourceCache::SetSingleAllocationByteLimit(size_t size) {
    const size_t result = get_cache().setSingleAllocationByteLimit(size);
    return result;
}

size_t SkResourceCache::GetSingleAllocationByteLimit() {
    const size_t result = get_cache().getSingleAllocationByteLimit();
    return result;
}

size_t SkResourceCache::GetEffectiveSingleAllocationByteLimit() {
    const size_t result = get_cache().getEffectiveSingleAllocationByteLimit();
    return result;
}

void SkResourceCache::PurgeAll() {
    get_cache().purgeAll();
}

void SkResourceCache::CheckMessages() {
    get_cache().checkMessages();
}

bool SkResourceCache::Find(const Key& key, FindVisitor visitor, void* context) {
    const bool result = get_cache().find(key, visitor, context);
    return result;
}

void SkResourceCache::Add(Rec* rec, void* payload) {
    get_cache().add(rec, payload);
}

void SkResourceCache::VisitAll(Visitor visitor, void* context) {
    get_cache().visitAll(visitor, context);
}

void SkResourceCache::PostPurgeSharedID(uint64_t sharedID) {
    if (sharedID) {
        SkResourceCacheInvalidationInbox::Post(sharedID);
    }
}

///////////////////////////////////////////////////////////////////////////////

static void dump_visitor(const SkResourceCache::Rec& rec, void*) {
    SkDebugf("RC: %12s bytes %9zu  discardable %p\n",
             rec.getCategory(), rec.bytesUsed(), rec.diagnostic_only_getDiscardable());
}

void SkResourceCache::TestDumpMemoryStatistics() {
    VisitAll(dump_visitor, nullptr);
}

static void sk_trace_dump_visitor(const SkResourceCache::Rec& rec, void* context) {
    SkTraceMemoryDump* dump = static_cast<SkTraceMemoryDump*>(context);
    SkString dumpName = SkStringPrintf("skia/sk_resource_cache/%s_%p", rec.getCategory(), &rec);
    SkDiscardableMemory* discardable = rec.diagnostic_only_getDiscardable();
    if (discardable) {
        (*dump).setDiscardableMemoryBacking(dumpName.c_str(), *discardable);

        // The discardable memory size will be calculated by dumper, but we also dump what we think
        // the size of object in memory is irrespective of whether object is live or dead.
        (*dump).dumpNumericValue(dumpName.c_str(), "discardable_size", "bytes", rec.bytesUsed());
    } else {
        (*dump).dumpNumericValue(dumpName.c_str(), "size", "bytes", rec.bytesUsed());
        (*dump).setMemoryBacking(dumpName.c_str(), "malloc", nullptr);
    }
}

void SkResourceCache::DumpMemoryStatistics(SkTraceMemoryDump* dump) {
    // Since resource could be backed by malloc or discardable, the cache always dumps detailed
    // stats to be accurate.
    VisitAll(sk_trace_dump_visitor, dump);
}
