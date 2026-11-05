/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkSynchronizedResourceCache.h"

SkSynchronizedResourceCache::SkSynchronizedResourceCache(DiscardableFactory fact)
 : SkResourceCache(fact) {}

SkSynchronizedResourceCache::SkSynchronizedResourceCache(size_t byteLimit)
 : SkResourceCache(byteLimit) {}

SkSynchronizedResourceCache::~SkSynchronizedResourceCache() = default;

size_t SkSynchronizedResourceCache::getTotalBytesUsed() const {
    SkAutoMutexExclusive lock(fMutex);
    const size_t result = SkResourceCache::getTotalBytesUsed();
    return result;
}

size_t SkSynchronizedResourceCache::getTotalByteLimit() const {
    SkAutoMutexExclusive lock(fMutex);
    const size_t result = SkResourceCache::getTotalByteLimit();
    return result;
}

size_t SkSynchronizedResourceCache::setTotalByteLimit(size_t newLimit) {
    SkAutoMutexExclusive lock(fMutex);
    const size_t result = SkResourceCache::setTotalByteLimit(newLimit);
    return result;
}

SkResourceCache::DiscardableFactory SkSynchronizedResourceCache::discardableFactory() const {
    SkAutoMutexExclusive lock(fMutex);
    const DiscardableFactory result = SkResourceCache::discardableFactory();
    return result;
}

SkCachedData* SkSynchronizedResourceCache::newCachedData(size_t bytes) {
    SkAutoMutexExclusive lock(fMutex);
    SkCachedData* const result = SkResourceCache::newCachedData(bytes);
    return result;
}

void SkSynchronizedResourceCache::dump() const {
    SkAutoMutexExclusive lock(fMutex);
    SkResourceCache::dump();
}

size_t SkSynchronizedResourceCache::setSingleAllocationByteLimit(size_t size) {
    SkAutoMutexExclusive lock(fMutex);
    const size_t result = SkResourceCache::setSingleAllocationByteLimit(size);
    return result;
}

size_t SkSynchronizedResourceCache::getSingleAllocationByteLimit() const {
    SkAutoMutexExclusive lock(fMutex);
    const size_t result = SkResourceCache::getSingleAllocationByteLimit();
    return result;
}

size_t SkSynchronizedResourceCache::getEffectiveSingleAllocationByteLimit() const {
    SkAutoMutexExclusive lock(fMutex);
    const size_t result = SkResourceCache::getEffectiveSingleAllocationByteLimit();
    return result;
}

void SkSynchronizedResourceCache::purgeAll() {
    SkAutoMutexExclusive lock(fMutex);
    SkResourceCache::purgeAll();
}

bool SkSynchronizedResourceCache::find(const Key& key, FindVisitor visitor, void* context) {
    SkAutoMutexExclusive lock(fMutex);
    const bool result = SkResourceCache::find(key, visitor, context);
    return result;
}

void SkSynchronizedResourceCache::add(Rec* rec, void* payload) {
    SkAutoMutexExclusive lock(fMutex);
    SkResourceCache::add(rec, payload);
}

void SkSynchronizedResourceCache::visitAll(Visitor visitor, void* context) {
    SkAutoMutexExclusive lock(fMutex);
    SkResourceCache::visitAll(visitor, context);
}

void SkSynchronizedResourceCache::purgeSharedID(uint64_t sharedID) {
    SkAutoMutexExclusive lock(fMutex);
    SkResourceCache::purgeSharedID(sharedID);
}

void SkSynchronizedResourceCache::checkMessages() {
    SkAutoMutexExclusive lock(fMutex);
    SkResourceCache::checkMessages();
}
