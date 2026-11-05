/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkResourceCache_DEFINED
#define SkResourceCache_DEFINED

#include "include/private/SkDebug.h"
#include "src/core/SkResourceCacheInvalidation.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

class SkCachedData;
class SkDiscardableMemory;
class SkTraceMemoryDump;

// Owns heap records in a least-recently-used list; the hash borrows those records.
// Local caches require external serialization. Static entry points serialize
// access to a process-lifetime global cache. Destruction requires quiescence and
// destroys all records, including pinned records. Borrows of record storage end
// with the cache; independently acquired data ownership can outlive it.
class SkResourceCache {
public:
    struct Key {
        // Initialize after the subclass payload. Payload begins immediately after
        // Key, has no uninitialized padding, and its byte size is a multiple of 4.
        // Namespace identifies the key type; zero sharedID disables group purging.
        // The complete extent must fit the signed 32-bit word count.
        void init(void* nameSpace, uint64_t sharedID, size_t dataSize);
        size_t size() const {
            const size_t bytes = static_cast<size_t>(fCount32) * sizeof(uint32_t);
            return bytes;
        }
        void* getNamespace() const { return fNamespace; }
        uint64_t getSharedID() const {
            const uint64_t sharedID = (static_cast<uint64_t>(fSharedID_hi) << 32) | fSharedID_lo;
            return sharedID;
        }
        uint32_t hash() const { return fHash; }
        bool operator==(const Key& other) const {
            if (fCount32 != other.fCount32 || fHash != other.fHash) {
                return false;
            }
            const size_t bytes = size();
            const bool equal = std::memcmp(this, &other, bytes) == 0;
            return equal;
        }

    private:
        int32_t fCount32 = 0;  // Complete key extent in 32-bit words.
        uint32_t fHash = 0;
        // Split the shared ID to retain the original layout on 32-bit hosts.
        uint32_t fSharedID_lo = 0;
        uint32_t fSharedID_hi = 0;
        void* fNamespace = nullptr;
    };

    struct Rec {
        using Key = SkResourceCache::Key;
        Rec() = default;
        Rec(const Rec&) = delete;
        Rec& operator=(const Rec&) = delete;
        virtual ~Rec() = default;
        uint32_t getHash() const {
            const Key& key = getKey();
            const uint32_t hash = key.hash();
            return hash;
        }
        virtual const Key& getKey() const = 0;
        // Key and byte charge remain stable from admission until destruction.
        virtual size_t bytesUsed() const = 0;
        virtual bool canBePurged() { return true; }

        // Synchronous under the cache's access authority. A pinned duplicate
        // receives installation instead of the newly supplied record. Installation
        // may pin its own storage but may not reenter or mutate this cache.
        virtual void postAddInstall(void*) {}
        virtual const char* getCategory() const = 0;
        virtual SkDiscardableMemory* diagnostic_only_getDiscardable() const { return nullptr; }
    private:
        Rec* fNext = nullptr;  // Observers, linked only while cache owns this record.
        Rec* fPrev = nullptr;
        friend class SkResourceCache;
    };

    using ID = const Rec*;
    // Visitors run synchronously under cache access authority. They may copy or
    // acquire record-owned data, but must not retain an unpinned record borrow,
    // reenter this cache, mutate its list, or throw. False declares a found record
    // stale and purgeable; it is removed without a separate canBePurged decision.
    using FindVisitor = bool (*)(const Rec&, void* context);
    using Visitor = void (*)(const Rec&, void* context);
    // Returns owned, locked discardable storage or null on failure.
    using DiscardableFactory = SkDiscardableMemory* (*)(size_t bytes);

    static bool Find(const Key& key, FindVisitor visitor, void* context);
    static void Add(Rec* record, void* payload = nullptr);
    static void VisitAll(Visitor visitor, void* context);
    static size_t GetTotalBytesUsed();
    static size_t GetTotalByteLimit();
    static size_t SetTotalByteLimit(size_t newLimit);
    static size_t SetSingleAllocationByteLimit(size_t newLimit);
    static size_t GetSingleAllocationByteLimit();
    static size_t GetEffectiveSingleAllocationByteLimit();
    static void PurgeAll();
    static void CheckMessages();
    static void TestDumpMemoryStatistics();
    static void DumpMemoryStatistics(SkTraceMemoryDump* dump);
    static DiscardableFactory GetDiscardableFactory();
    static SkCachedData* NewCachedData(size_t bytes);
    static void PostPurgeSharedID(uint64_t sharedID);
    static void Dump();

    // Discardable mode uses the historical count/UINT32_MAX thresholds rather
    // than the stored byte limit. Accounting still reports record byte charges.
    explicit SkResourceCache(DiscardableFactory factory);
    explicit SkResourceCache(size_t byteLimit);
    SkResourceCache(const SkResourceCache&) = delete;
    SkResourceCache& operator=(const SkResourceCache&) = delete;
    virtual ~SkResourceCache();

    // A successful find promotes the record to most recently used. A miss leaves
    // context unchanged. Pending shared-ID messages are processed before lookup.
    virtual bool find(const Key& key, FindVisitor visitor, void* context);
    // Takes sole ownership even on rejection. Null is ignored; charge/count
    // overflow rejects and destroys the record without invoking installation.
    // Pinned duplicates win; unpinned duplicates are replaced. Installation
    // occurs after admission and before budget eviction, so it may pin the entry.
    virtual void add(Rec* record, void* payload = nullptr);
    // Visits least to most recently used without promoting or polling messages.
    virtual void visitAll(Visitor visitor, void* context);
    virtual size_t getTotalBytesUsed() const { return fTotalBytesUsed; }
    virtual size_t getTotalByteLimit() const { return fTotalByteLimit; }
    virtual size_t setSingleAllocationByteLimit(size_t maximumAllocationSize);
    virtual size_t getSingleAllocationByteLimit() const;
    // Zero single-allocation limit means default: total budget in malloc mode,
    // unlimited in discardable mode. This is guidance for callers, not admission.
    virtual size_t getEffectiveSingleAllocationByteLimit() const;
    // Returns previous limit; lowering triggers eviction. The historical policy
    // evicts while charge >= limit, skipping pinned entries even if over budget.
    virtual size_t setTotalByteLimit(size_t newLimit);
    virtual void purgeSharedID(uint64_t sharedID);
    virtual void purgeAll() { purgeAsNeeded(true); }
    virtual DiscardableFactory discardableFactory() const { return fDiscardableFactory; }
    // Returns a client reference, not an admitted cache record. Caller unrefs it.
    virtual SkCachedData* newCachedData(size_t bytes);
    virtual void dump() const;

protected:
    // Internal calls already have access authority. The synchronized subclass
    // supplies a locked external entry point with the same name, not an override.
    void checkMessages();

private:
    // Intrusive list owns records; removal unlinks both indexes before deleting.
    Rec* fHead = nullptr;
    Rec* fTail = nullptr;
    class Hash;
    std::unique_ptr<Hash> fHash;
    const DiscardableFactory fDiscardableFactory;
    size_t fTotalBytesUsed = 0;
    size_t fTotalByteLimit;
    size_t fSingleAllocationByteLimit = 0;
    int fCount = 0;
    // Both drain and inbox buffers retain capacity between checks. The cache's
    // access authority serializes use of this drain buffer and record mutation.
    std::vector<uint64_t> fPurgeSharedIDs{};
    SkResourceCacheInvalidationInbox fPurgeSharedIDInbox{};

    void purgeAsNeeded(bool forcePurge = false);
    void moveToHead(Rec* record);
    void addToHead(Rec* record);
    void unlink(Rec* record);
    void remove(Rec* record);
#ifdef SK_DEBUG
    void validate() const;
#else
    void validate() const {}
#endif
};
#endif
