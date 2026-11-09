/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/core/SkCachedData.h"

#include "include/private/SkMalloc.h"
#include "include/private/chromium/SkDiscardableMemory.h"

#include <climits>

SkCachedData::SkCachedData(void* data, size_t size)
    : fStorage{.fMalloc = data}
    , fData(data)
    , fSize(size)
    , fRefCnt(1)
    , fStorageType(StorageType::kMalloc)
    , fInCache(false)
    , fIsLocked(true)
{ }

SkCachedData::SkCachedData(size_t size, SkDiscardableMemory* dm)
    : fStorage{.fDM = dm}
    , fData((*dm).data())
    , fSize(size)
    , fRefCnt(1)
    , fStorageType(StorageType::kDiscardableMemory)
    , fInCache(false)
    , fIsLocked(true)
{ }

SkCachedData::~SkCachedData() {
    switch (fStorageType) {
        case StorageType::kMalloc:
            sk_free(fStorage.fMalloc);
            break;
        case StorageType::kDiscardableMemory:
            delete fStorage.fDM;
            break;
        default:
            SK_ABORT("Invalid cached storage type.");
    }
}

void SkCachedData::internalRef(bool fromCache) const {
    SkAutoMutexExclusive lock(fMutex);
    validate();
    inMutexRef(fromCache);
    validate();
}

void SkCachedData::internalUnref(bool fromCache) const {
    bool destroy = false;
    {
        SkAutoMutexExclusive lock(fMutex);
        validate();
        destroy = inMutexUnref(fromCache);
        validate();
    }
    // Releasing the final reference cannot destroy the mutex while it is held.
    if (destroy) delete this;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void SkCachedData::inMutexRef(bool fromCache) const {
    fMutex.assertHeld();
    SkASSERT_RELEASE(fRefCnt > 0 && fRefCnt < INT_MAX);
    SkASSERT_RELEASE(!fromCache || !fInCache);
    if ((1 == fRefCnt) && fInCache) {
        inMutexLock();
    }

    fRefCnt += 1;
    if (fromCache) {
        SkASSERT(!fInCache);
        fInCache = true;
    }
}

bool SkCachedData::inMutexUnref(bool fromCache) const {
    fMutex.assertHeld();
    SkASSERT_RELEASE(fRefCnt > 0);
    SkASSERT_RELEASE(!fromCache || fInCache);
    SkASSERT_RELEASE(fromCache || !fInCache || fRefCnt > 1);
    --fRefCnt;
    switch (fRefCnt) {
        case 0:
            // we're going to be deleted, so we need to be unlocked (for DiscardableMemory)
            if (fIsLocked) {
                inMutexUnlock();
            }
            break;
        case 1:
            if (fInCache && !fromCache) {
                // If we're down to 1 owner, and that owner is the cache, this it is safe
                // to unlock (and mutate fData) even if the cache is in a different thread,
                // as the cache is NOT allowed to inspect or use fData.
                inMutexUnlock();
            }
            break;
        default:
            break;
    }

    if (fromCache) {
        SkASSERT(fInCache);
        fInCache = false;
    }

    // return true when we need to be deleted
    const bool destroy = fRefCnt == 0;
    return destroy;
}

void SkCachedData::inMutexLock() const {
    fMutex.assertHeld();

    SkASSERT(!fIsLocked);
    fIsLocked = true;

    switch (fStorageType) {
        case StorageType::kMalloc:
            setData(fStorage.fMalloc);
            break;
        case StorageType::kDiscardableMemory:
            if ((*fStorage.fDM).lock()) {
                void* ptr = (*fStorage.fDM).data();
                SkASSERT(ptr);
                setData(ptr);
            } else {
                setData(nullptr);   // signal failure to lock, contents are gone
            }
            break;
        default:
            SK_ABORT("Invalid cached storage type.");
    }
}

void SkCachedData::inMutexUnlock() const {
    fMutex.assertHeld();

    SkASSERT(fIsLocked);
    fIsLocked = false;

    switch (fStorageType) {
        case StorageType::kMalloc:
            // nothing to do/check
            break;
        case StorageType::kDiscardableMemory:
            if (fData) {    // did the previous lock succeed?
                (*fStorage.fDM).unlock();
            }
            break;
        default:
            SK_ABORT("Invalid cached storage type.");
    }
    setData(nullptr);   // signal that we're in an unlocked state
}

///////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef SK_DEBUG
void SkCachedData::validate() const {
    if (fIsLocked) {
        SkASSERT((fInCache && fRefCnt > 1) || !fInCache);
        switch (fStorageType) {
            case StorageType::kMalloc:
                SkASSERT(fData == fStorage.fMalloc);
                break;
            case StorageType::kDiscardableMemory:
                // fData can be null or the actual value, depending if DM's lock succeeded
                break;
            default:
                SK_ABORT("Invalid cached storage type.");
        }
    } else {
        SkASSERT((fInCache && 1 == fRefCnt) || (0 == fRefCnt));
        SkASSERT(nullptr == fData);
    }
}
#endif
