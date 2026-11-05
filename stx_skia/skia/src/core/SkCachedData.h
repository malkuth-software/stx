/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkCachedData_DEFINED
#define SkCachedData_DEFINED

#include "include/core/SkTypes.h"
#include "include/private/SkMutex.h"
#include "include/private/SkNoncopyable.h"

#include <cstddef>

class SkDiscardableMemory;

class SkCachedData : ::SkNoncopyable {
public:
    // Adopt a sk_malloc-compatible allocation, or an already locked discardable
    // allocation. Initial state has one client reference and no cache reference.
    // Only heap instances may use ref/unref; the final release deletes this object.
    SkCachedData(void* mallocData, size_t size);
    SkCachedData(size_t size, SkDiscardableMemory*);
    virtual ~SkCachedData();

    // A client reference keeps the data address stable (possibly null after a
    // failed discardable lock). Cache ownership alone grants no pixel access.
    // Writable access additionally requires externally exclusive pixel ownership.
    size_t size() const { return fSize; }
    const void* data() const { return fData; }

    void* writable_data() { return fData; }

    void ref() const { internalRef(false); }
    void unref() const { internalUnref(false); }

    // Diagnostic access requires externally quiescent reference transitions.
    int testing_only_getRefCnt() const { return fRefCnt; }
    bool testing_only_isLocked() const { return fIsLocked; }
    bool testing_only_isInCache() const { return fInCache; }

    SkDiscardableMemory* diagnostic_only_getDiscardable() const {
        SkDiscardableMemory* memory = fStorageType == StorageType::kDiscardableMemory ? fStorage.fDM : nullptr;
        return memory;
    }

protected:
    // Runs under fMutex before publishing newData, which may be null. It must
    // not reenter ref/unref/cache methods or throw; it updates borrowed views only.
    virtual void onDataChange(void* oldData, void* newData) const {}

private:
    mutable SkMutex fMutex{};

    enum class StorageType {
        kDiscardableMemory,
        kMalloc
    };

    union Storage {
        SkDiscardableMemory* fDM;
        void* fMalloc;
    };
    const Storage fStorage;
    mutable void* fData;
    const size_t fSize;
    mutable int fRefCnt;  // Includes one cache reference when fInCache is true.
    const StorageType fStorageType;
    mutable bool fInCache;
    mutable bool fIsLocked;

    void internalRef(bool fromCache) const;
    void internalUnref(bool fromCache) const;

    void inMutexRef(bool fromCache) const;
    bool inMutexUnref(bool fromCache) const;  // returns true if we should delete "this"
    void inMutexLock() const;
    void inMutexUnlock() const;

    // called whenever our fData might change (lock or unlock)
    void setData(void* newData) const {
        if (newData != fData) {
            // notify our subclasses of the change
            onDataChange(fData, newData);
            fData = newData;
        }
    }


public:
#ifdef SK_DEBUG
    void validate() const;
#else
    void validate() const {}
#endif

   /*
     *  Attaching data to a SkResourceCache (only one at a time) enables the data to be
     *  unlocked when the cache is the only owner, thus freeing it to be purged (assuming the
     *  data is backed by a SkDiscardableMemory).
     *
     *  When attached, it also automatically attempts to "lock" the data when the first client
     *  ref's the data (typically from a find(key, visitor) call).
     *
     *  Thus the data will always be "locked" when a non-cache has a ref on it (whether or not
     *  the lock succeeded to recover the memory -- check data() to see if it is nullptr).
     */

    /*
     *  Call when adding this instance to a SkResourceCache::Rec subclass
     *  (typically in the Rec's constructor).
     */
    void attachToCacheAndRef() const { internalRef(true); }

    /*
     *  Call when removing this instance from a SkResourceCache::Rec subclass
     *  (typically in the Rec's destructor).
     */
    void detachFromCacheAndUnref() const { internalUnref(true); }
};

#endif
