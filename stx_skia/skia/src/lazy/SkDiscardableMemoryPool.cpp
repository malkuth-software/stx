/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/SkMalloc.h"
#include "include/private/SkMutex.h"
#include "include/private/SkTemplates.h"
#include "include/private/chromium/SkDiscardableMemory.h"
#include "src/core/SkTInternalLList.h"
#include "src/lazy/SkDiscardableMemoryPool.h"
#include <limits>
#include <memory>
#include <new>
#include <utility>

using namespace skia_private;

// Each block owns its allocation and retains the pool. The pool observes blocks
// through intrusive links; it never owns or deletes their interface objects.
// The pool mutex protects membership, accounting, budget and purge decisions.
// A block has one caller at a time; its data borrow lasts only while locked.

namespace {

class PoolDiscardableMemory;

/**
 *  This non-global pool can be used for unit tests to verify that the
 *  pool works.
 */
class DiscardableMemoryPool : public SkDiscardableMemoryPool {
public:
    explicit DiscardableMemoryPool(size_t budget);
    ~DiscardableMemoryPool() override;

    std::unique_ptr<SkDiscardableMemory> make(size_t bytes);
    SkDiscardableMemory* create(size_t bytes) override {
        std::unique_ptr<SkDiscardableMemory> block = make(bytes);
        SkDiscardableMemory* result = block.release();
        return result;  // Existing interface transfers ownership to its caller.
    }

    size_t getRAMUsed() override;
    void setRAMBudget(size_t budget) override;
    size_t getRAMBudget() override {
        SkAutoMutexExclusive guard(fMutex);
        return fBudget;
    }

    /** purges all unlocked DMs */
    void dumpPool() override;

    #if SK_LAZY_CACHE_STATS  // Defined in SkDiscardableMemoryPool.h
    int getCacheHits() override {
        SkAutoMutexExclusive guard(fMutex);
        return fCacheHits;
    }
    int getCacheMisses() override {
        SkAutoMutexExclusive guard(fMutex);
        return fCacheMisses;
    }
    void resetCacheHitsAndMisses() override {
        SkAutoMutexExclusive guard(fMutex);
        fCacheHits = 0;
        fCacheMisses = 0;
    }
    #endif  // SK_LAZY_CACHE_STATS

private:
    SkMutex fMutex{};
    size_t fBudget;
    size_t fUsed = 0;
    SkTInternalLList<PoolDiscardableMemory> fList{};
#if SK_LAZY_CACHE_STATS
    int fCacheHits = 0;
    int fCacheMisses = 0;
#endif

    // Requires fMutex. Evict oldest unlocked allocations until within budget;
    // locked blocks remain live even when their total exceeds the budget.
    void dumpDownTo(size_t budget);
    void removeFromPool(PoolDiscardableMemory& block);
    bool lock(PoolDiscardableMemory& block);
    void unlock(PoolDiscardableMemory& block);

    friend class PoolDiscardableMemory;

};

/**
 *  A PoolDiscardableMemory is a SkDiscardableMemory that relies on
 *  a DiscardableMemoryPool object to manage the memory.
 */
class PoolDiscardableMemory : public SkDiscardableMemory {
public:
    PoolDiscardableMemory(sk_sp<DiscardableMemoryPool> pool, size_t bytes);
    ~PoolDiscardableMemory() override;
    bool lock() override;
    void* data() override;
    void unlock() override;
    friend class DiscardableMemoryPool;
private:
    SK_DECLARE_INTERNAL_LLIST_INTERFACE(PoolDiscardableMemory);
    sk_sp<DiscardableMemoryPool> fPool;
    bool fLocked = false;
    UniqueVoidPtr fPointer{};
    const size_t fBytes;
};

PoolDiscardableMemory::PoolDiscardableMemory(sk_sp<DiscardableMemoryPool> pool,
                                             size_t bytes)
        : fPool(std::move(pool)), fBytes(bytes) {
    SkASSERT(fPool != nullptr);
    SkASSERT(fBytes > 0);
}

PoolDiscardableMemory::~PoolDiscardableMemory() {
    SkASSERT(!fLocked); // contract for SkDiscardableMemory
    (*fPool).removeFromPool(*this);
}

bool PoolDiscardableMemory::lock() {
    SkASSERT(!fLocked); // contract for SkDiscardableMemory
    const bool acquired = (*fPool).lock(*this);
    return acquired;
}

void* PoolDiscardableMemory::data() {
    SkASSERT(fLocked); // contract for SkDiscardableMemory
    void* data = fPointer.get();
    return data;
}

void PoolDiscardableMemory::unlock() {
    SkASSERT(fLocked); // contract for SkDiscardableMemory
    (*fPool).unlock(*this);
}

////////////////////////////////////////////////////////////////////////////////

DiscardableMemoryPool::DiscardableMemoryPool(size_t budget) : fBudget(budget) {}

DiscardableMemoryPool::~DiscardableMemoryPool() {
    // PoolDiscardableMemory objects that belong to this pool are
    // always deleted before deleting this pool since each one has a
    // ref to the pool.
    SkASSERT(fList.isEmpty());
}

void DiscardableMemoryPool::dumpDownTo(size_t budget) {
    fMutex.assertHeld();
    if (fUsed <= budget) {
        return;
    }
    using Iter = SkTInternalLList<PoolDiscardableMemory>::Iter;
    Iter iterator{};
    PoolDiscardableMemory* current = iterator.init(fList, Iter::kTail_IterStart);
    while (fUsed > budget && current != nullptr) {
        PoolDiscardableMemory& block = *current;
        // Save the next traversal position before removal clears these links.
        current = iterator.prev();
        if (block.fLocked) {
            continue;
        }
        SkASSERT(block.fPointer != nullptr);
        SkASSERT(fUsed >= block.fBytes);
        block.fPointer.reset();
        fUsed -= block.fBytes;
        fList.remove(&block);
        // The caller still owns the block object. Future lock attempts fail.
    }
}

std::unique_ptr<SkDiscardableMemory> DiscardableMemoryPool::make(size_t bytes) {
    if (bytes == 0) {
        return nullptr;
    }
    // Allocate the substantial pixel storage outside the pool lock.
    UniqueVoidPtr allocation(sk_malloc_canfail(bytes));
    if (allocation == nullptr) {
        return nullptr;
    }
    // Prepare an empty, unlocked record outside the mutex too. Until publication
    // it has no allocation or membership, just like a discarded block. Both owners
    // precede the guard, so any rejection releases them only after unlocking.
    std::unique_ptr<PoolDiscardableMemory> block(new (std::nothrow)
        PoolDiscardableMemory(sk_ref_sp(this), bytes));
    if (block == nullptr) {
        return nullptr;
    }
    {
        SkAutoMutexExclusive guard(fMutex);
        if (bytes > std::numeric_limits<size_t>::max() - fUsed) {
            return nullptr;
        }
        (*block).fPointer = std::move(allocation);
        (*block).fLocked = true;
        fList.addToHead(block.get());
        fUsed += bytes;
        dumpDownTo(fBudget);
    }
    std::unique_ptr<SkDiscardableMemory> result = std::move(block);
    return result;
}

void DiscardableMemoryPool::removeFromPool(PoolDiscardableMemory& block) {
    SkAutoMutexExclusive guard(fMutex);
    if (block.fPointer != nullptr) {
        SkASSERT(fUsed >= block.fBytes);
        fUsed -= block.fBytes;
        fList.remove(&block);
    } else {
        SkASSERT(!fList.isInList(&block));
    }
}

bool DiscardableMemoryPool::lock(PoolDiscardableMemory& block) {
    SkAutoMutexExclusive guard(fMutex);
    if (block.fPointer == nullptr) {
#if SK_LAZY_CACHE_STATS
        if (fCacheMisses < std::numeric_limits<int>::max()) {
            ++fCacheMisses;
        }
#endif
        return false;
    }
    block.fLocked = true;
    if (fList.head() != &block) {
        fList.remove(&block);
        fList.addToHead(&block);
    }
#if SK_LAZY_CACHE_STATS
    if (fCacheHits < std::numeric_limits<int>::max()) {
        ++fCacheHits;
    }
#endif
    return true;
}

void DiscardableMemoryPool::unlock(PoolDiscardableMemory& block) {
    SkAutoMutexExclusive guard(fMutex);
    block.fLocked = false;
    dumpDownTo(fBudget);
}

size_t DiscardableMemoryPool::getRAMUsed() {
    SkAutoMutexExclusive guard(fMutex);
    return fUsed;
}
void DiscardableMemoryPool::setRAMBudget(size_t budget) {
    SkAutoMutexExclusive autoMutexAcquire(fMutex);
    fBudget = budget;
    dumpDownTo(fBudget);
}
void DiscardableMemoryPool::dumpPool() {
    SkAutoMutexExclusive autoMutexAcquire(fMutex);
    dumpDownTo(0);
}

}  // namespace

sk_sp<SkDiscardableMemoryPool> SkDiscardableMemoryPool::Make(size_t size) {
    sk_sp<SkDiscardableMemoryPool> pool = sk_make_sp<DiscardableMemoryPool>(size);
    return pool;
}

SkDiscardableMemoryPool* SkGetGlobalDiscardableMemoryPool() {
    // Process-lifetime owner: blocks may release from other static destructors.
    static SkDiscardableMemoryPool* global =
            new DiscardableMemoryPool(SK_DEFAULT_GLOBAL_DISCARDABLE_MEMORY_POOL_SIZE);
    return global;
}
