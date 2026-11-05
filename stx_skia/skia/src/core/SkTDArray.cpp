/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/SkTDArray.h"
#include "include/private/SkMalloc.h"

#include <climits>
#include <cstdint>
#include <cstring>

namespace {
constexpr int expanded_capacity(int requested, int elementBytes) {
    int expanded = INT_MAX;
    if (INT_MAX - requested > 4) {
        const int growth = 4 + ((requested + 4) >> 2);
        if (INT_MAX - requested > growth) expanded = requested + growth;
    }
    if (elementBytes == 1 && expanded <= INT_MAX - 15) {
        expanded = (expanded + 15) & ~15;
    }
    return expanded;
}
static_assert(expanded_capacity(1, 1) == 16);
static_assert(expanded_capacity(1, 4) == 6);
static_assert(expanded_capacity(INT_MAX - 16, 1) == INT_MAX);
static_assert(expanded_capacity(INT_MAX, 1) == INT_MAX);
}

SkTDStorage::SkTDStorage(int sizeOfT) : fSizeOfT{sizeOfT} {
    SkASSERT_RELEASE(sizeOfT > 0);
}

SkTDStorage::SkTDStorage(const void* src, int size, int sizeOfT)
        : SkTDStorage(sizeOfT) {
    SkASSERT_RELEASE(size >= 0);
    if (size == 0) return;
    SkASSERT_RELEASE(src != nullptr);
    const size_t storageSize = safe_bytes(size);
    fStorage = static_cast<std::byte*>(sk_malloc_throw(storageSize));
    memcpy(fStorage, src, storageSize);
    fCapacity = size;
    fSize = size;
}

SkTDStorage::SkTDStorage(const SkTDStorage& that)
        : SkTDStorage{that.fStorage, that.fSize, that.fSizeOfT} {}

SkTDStorage& SkTDStorage::operator=(const SkTDStorage& that) {
    SkASSERT_RELEASE(fSizeOfT == that.fSizeOfT);
    if (this == &that) return *this;
    if (that.fSize > fCapacity) {
        SkTDStorage replacement(that);
        swap(replacement);
        return *this;
    }
    if (that.fSize > 0) {
        const size_t byteCount = that.size_bytes();
        memcpy(fStorage, that.fStorage, byteCount);
    }
    fSize = that.fSize;
    return *this;
}

SkTDStorage::SkTDStorage(SkTDStorage&& that)
        : fSizeOfT{that.fSizeOfT}
        , fStorage{std::exchange(that.fStorage, nullptr)}
        , fCapacity{std::exchange(that.fCapacity, 0)}
        , fSize{std::exchange(that.fSize, 0)} {}

SkTDStorage& SkTDStorage::operator=(SkTDStorage&& that) {
    SkASSERT_RELEASE(fSizeOfT == that.fSizeOfT);
    if (this == &that) return *this;
    reset();
    fStorage = std::exchange(that.fStorage, nullptr);
    fCapacity = std::exchange(that.fCapacity, 0);
    fSize = std::exchange(that.fSize, 0);
    return *this;
}

SkTDStorage::~SkTDStorage() { sk_free(fStorage); }

void SkTDStorage::reset() {
    sk_free(fStorage);
    fStorage = nullptr;
    fCapacity = 0;
    fSize = 0;
}

void SkTDStorage::swap(SkTDStorage& that) {
    SkASSERT_RELEASE(fSizeOfT == that.fSizeOfT);
    std::swap(fStorage, that.fStorage);
    std::swap(fCapacity, that.fCapacity);
    std::swap(fSize, that.fSize);
}

void SkTDStorage::resize(int newSize) {
    SkASSERT_RELEASE(newSize >= 0);
    if (newSize > fCapacity) reserve(newSize);
    fSize = newSize;
}

void SkTDStorage::reserve(int newCapacity) {
    SkASSERT_RELEASE(newCapacity >= 0);
    if (newCapacity <= fCapacity) return;
    const int expanded = expanded_capacity(newCapacity, fSizeOfT);
    const size_t storageSize = safe_bytes(expanded);
    fStorage = static_cast<std::byte*>(sk_realloc_throw(fStorage, storageSize));
    fCapacity = expanded;
}

void SkTDStorage::shrink_to_fit() {
    if (fCapacity == fSize) return;
    if (fSize == 0) {
        reset();
        return;
    }
    const size_t storageSize = safe_bytes(fSize);
    fStorage = static_cast<std::byte*>(sk_realloc_throw(fStorage, storageSize));
    fCapacity = fSize;
}

void SkTDStorage::erase(int index, int count) {
    SkASSERT_RELEASE(index >= 0 && index <= fSize);
    SkASSERT_RELEASE(count >= 0 && count <= fSize - index);
    if (count == 0) return;
    moveTail(index, index + count, fSize);
    fSize -= count;
}

void SkTDStorage::removeShuffle(int index) {
    SkASSERT_RELEASE(index >= 0 && index < fSize);
    moveTail(index, fSize - 1, fSize);
    --fSize;
}

void* SkTDStorage::prepend() & {
    void* result = insert(0);
    return result;
}

void SkTDStorage::append() {
    if (fSize < fCapacity) {
        ++fSize;
        return;
    }
    insert(fSize);
}

void SkTDStorage::append(int count) {
    SkASSERT_RELEASE(count >= 0);
    if (count <= fCapacity - fSize) {
        fSize += count;
        return;
    }
    insert(fSize, count, nullptr);
}

void* SkTDStorage::append(const void* src, int count) & {
    void* result = insert(fSize, count, src);
    return result;
}

void* SkTDStorage::insert(int index) & {
    void* result = insert(index, 1, nullptr);
    return result;
}

void* SkTDStorage::insert(int index, int count, const void* src) & {
    SkASSERT_RELEASE(index >= 0 && index <= fSize);
    SkASSERT_RELEASE(count >= 0 && count <= INT_MAX - fSize);
    if (count == 0) {
        void* result = address(index);
        return result;
    }
    const int oldCount = fSize;
    const int newCount = oldCount + count;
    const size_t insertedBytes = safe_bytes(count);
    const size_t insertionOffset = bytes(index);
    size_t sourceOffset = 0;
    bool internalSource = false;
    if (src != nullptr && fStorage != nullptr) {
        // Compare integer addresses, not ordered pointers into unrelated allocations.
        const uintptr_t sourceAddress = reinterpret_cast<uintptr_t>(src);
        const uintptr_t storageAddress = reinterpret_cast<uintptr_t>(fStorage);
        if (sourceAddress >= storageAddress) {
            const uintptr_t offset = sourceAddress - storageAddress;
            if (offset < bytes(fCapacity)) {
                sourceOffset = static_cast<size_t>(offset);
                const size_t liveBytes = bytes(oldCount);
                SkASSERT_RELEASE(sourceOffset <= liveBytes);
                SkASSERT_RELEASE(insertedBytes <= liveBytes - sourceOffset);
                internalSource = true;
            }
        }
    }
    resize(newCount);
    moveTail(index + count, index, oldCount);
    std::byte* destination = fStorage + insertionOffset;
    if (internalSource) {
        // The tail shift preserves the old source in at most two pieces outside
        // the new gap. Copy those pieces without allocating a snapshot.
        size_t prefixBytes = 0;
        if (sourceOffset < insertionOffset) {
            prefixBytes = std::min(insertedBytes, insertionOffset - sourceOffset);
            memmove(destination, fStorage + sourceOffset, prefixBytes);
        }
        const size_t suffixBytes = insertedBytes - prefixBytes;
        if (suffixBytes > 0) {
            const size_t shiftedOffset = sourceOffset + prefixBytes + insertedBytes;
            memmove(destination + prefixBytes, fStorage + shiftedOffset, suffixBytes);
        }
    } else if (src != nullptr) {
        memmove(destination, src, insertedBytes);
    }
    return destination;
}

bool operator==(const SkTDStorage& a, const SkTDStorage& b) {
    if (a.fSizeOfT != b.fSizeOfT || a.fSize != b.fSize) return false;
    if (a.empty()) return true;
    const size_t byteCount = a.size_bytes();
    const bool equal = memcmp(a.fStorage, b.fStorage, byteCount) == 0;
    return equal;
}

void SkTDStorage::moveTail(int to, int tailStart, int tailEnd) {
    SkASSERT(to >= 0 && to <= fSize);
    SkASSERT(tailStart >= 0 && tailStart <= tailEnd && tailEnd <= fSize);
    const int count = tailEnd - tailStart;
    SkASSERT(count <= fSize - to);
    if (to == tailStart || count == 0) return;
    const size_t byteCount = bytes(count);
    memmove(address(to), address(tailStart), byteCount);
}
