/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkData.h"
#include "include/core/SkSpan.h"
#include "include/core/SkStream.h"
#include "include/private/SkAssert.h"
#include "include/private/SkMalloc.h"
#include "src/core/SkGraphicsLifetime.h"
#include "src/core/SkStreamPriv.h"
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>

namespace {
void release_subset_owner(const void*, void* context) {
    SkData& owner = *static_cast<SkData*>(context);
    owner.unref();
}

void release_malloc_storage(const void* address, void*) {
    sk_free(const_cast<void*>(address));
}
}

SkData::SkData(SkSpan<std::byte> span, ReleaseProc proc, void* context)
    : fReleaseProc(proc), fReleaseProcContext(context), fSpan(span) {}

// The owned bytes follow the SkData object in the same allocation.
SkData::SkData(std::size_t size)
    : fReleaseProc(nullptr), fReleaseProcContext(nullptr),
      fSpan{reinterpret_cast<std::byte*>(this + 1), size} {}

SkData::~SkData() {
    if (fReleaseProc != nullptr) fReleaseProc(fSpan.data(), fReleaseProcContext);
}

bool SkData::operator==(const SkData& other) const {
    if (this == &other) return true;
    if (size() != other.size()) return false;
    const int comparison = sk_careful_memcmp(data(), other.data(), size());
    return comparison == 0;
}

std::size_t SkData::copyRange(std::size_t offset, std::size_t length, void* buffer) const {
    const std::size_t total = size();
    if (offset >= total || length == 0) return 0;
    const std::size_t available = total - offset;
    if (length > available) length = available;
    if (buffer != nullptr) std::memcpy(buffer, bytes() + offset, length);
    return length;
}

sk_sp<SkData> SkData::shareSubset(std::size_t offset, std::size_t length) {
    const std::size_t total = size();
    if (offset > total || length > total - offset) return nullptr;
    if (offset == 0 && length == total) {
        sk_sp<SkData> result = sk_ref_sp(this);
        return result;
    }
    if (length == 0) {
        sk_sp<SkData> result = MakeEmpty();
        return result;
    }
    ref();
    sk_sp<SkData> result = MakeWithProc(bytes() + offset, length, release_subset_owner, this);
    return result;
}

sk_sp<const SkData> SkData::shareSubset(std::size_t offset, std::size_t length) const {
    SkData& owner = *const_cast<SkData*>(this);
    sk_sp<const SkData> result = owner.shareSubset(offset, length);
    return result;
}

sk_sp<SkData> SkData::copySubset(std::size_t offset, std::size_t length) const {
    const std::size_t total = size();
    if (offset > total || length > total - offset) return nullptr;
    if (length == 0) {
        sk_sp<SkData> result = MakeEmpty();
        return result;
    }
    sk_sp<SkData> result = MakeWithCopy(bytes() + offset, length);
    return result;
}

void SkData::operator delete(void* storage) {
    ::operator delete(storage);
}

sk_sp<SkData> SkData::PrivateNewWithCopy(const void* source, std::size_t length) {
    if (length == 0) {
        sk_sp<SkData> result = MakeEmpty();
        return result;
    }
    SkASSERT_RELEASE(length <= std::numeric_limits<std::size_t>::max() - sizeof(SkData));
    const std::size_t allocation_size = sizeof(SkData) + length;
    void* storage = ::operator new(allocation_size);
    sk_sp<SkData> result(new (storage) SkData(length));
    if (source != nullptr) std::memcpy((*result).writable_data(), source, length);
    return result;
}

void SkData::NoopReleaseProc(const void*, void*) {}

SkBorrowedData::SkBorrowedData(const void* bytes, size_t length)
        : fData(SkData::MakeWithoutCopy(bytes, length)) {}

SkBorrowedData::~SkBorrowedData() {
    // A holder that outlives the borrow would read bytes the caller is about to release.
    SkASSERT_RELEASE((*fData).unique());
}

// The shared empty data holds the process's reference until SkGraphics::Shutdown() drops it.
SkData*& SkData::EmptySlot() {
    static SkData* empty = new SkData({}, nullptr, nullptr);
    return empty;
}

sk_sp<SkData> SkData::MakeEmpty() {
    SkGraphicsCheckRunning();
    sk_sp<SkData> result = sk_ref_sp(EmptySlot());
    return result;
}

void SkDataReleaseEmpty() {
    SkData*& slot = SkData::EmptySlot();
    SkSafeUnref(slot);
    slot = nullptr;
}

sk_sp<SkData> SkData::MakeFromMalloc(const void* data, std::size_t length) {
    std::byte* address = static_cast<std::byte*>(const_cast<void*>(data));
    sk_sp<SkData> result(new SkData({address, length}, release_malloc_storage, nullptr));
    return result;
}

sk_sp<SkData> SkData::MakeWithCopy(const void* source, std::size_t length) {
    SkASSERT(source);
    sk_sp<SkData> result = PrivateNewWithCopy(source, length);
    return result;
}

sk_sp<SkData> SkData::MakeUninitialized(std::size_t length) {
    sk_sp<SkData> result = PrivateNewWithCopy(nullptr, length);
    return result;
}

sk_sp<SkData> SkData::MakeZeroInitialized(std::size_t length) {
    sk_sp<SkData> result = MakeUninitialized(length);
    if (length != 0) std::memset((*result).writable_data(), 0, length);
    return result;
}

sk_sp<SkData> SkData::MakeWithProc(const void* data, std::size_t length,
                                ReleaseProc proc, void* context) {
    std::byte* address = static_cast<std::byte*>(const_cast<void*>(data));
    sk_sp<SkData> result(new SkData({address, length}, proc, context));
    return result;
}

sk_sp<SkData> SkData::MakeWithCString(const char text[]) {
    if (text == nullptr) text = "";
    const std::size_t size = std::strlen(text) + 1;
    sk_sp<SkData> result = MakeWithCopy(text, size);
    return result;
}

sk_sp<SkData> SkData::MakeFromStream(SkStream* stream, std::size_t size) {
    if (stream == nullptr) return nullptr;
    if (SkStreamPriv::RemainingLengthIsBelow(*stream, size)) return nullptr;
    sk_sp<SkData> result = MakeUninitialized(size);
    uint8_t* output = static_cast<uint8_t*>((*result).writable_data());
    std::size_t copied = 0;
    while (copied < size) {
        const std::size_t remaining = size - copied;
        const std::size_t count = (*stream).read(output + copied, remaining);
        if (count == 0 || count > remaining) return nullptr;
        copied += count;
    }
    return result;
}
