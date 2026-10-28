/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/core/SkAutoPixmapStorage.h"

#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/private/SkAssert.h"

#include <utility>

SkAutoPixmapStorage::SkAutoPixmapStorage() : fStorage(nullptr) {}

SkAutoPixmapStorage::~SkAutoPixmapStorage() {
    freeStorage();
}

SkAutoPixmapStorage::SkAutoPixmapStorage(SkAutoPixmapStorage&& other) : fStorage(nullptr) {
    *this = std::move(other);
}

SkAutoPixmapStorage& SkAutoPixmapStorage::operator=(SkAutoPixmapStorage&& other) {
    if (this == &other) return *this;
    // Snapshot the view independently of its optional allocation ownership.
    const SkImageInfo next_info = other.info();
    const void* next_pixels = other.addr();
    const size_t next_row_bytes = other.rowBytes();
    freeStorage();
    fStorage = other.fStorage;
    INHERITED::reset(next_info, next_pixels, next_row_bytes);
    other.fStorage = nullptr;
    other.INHERITED::reset();

    return *this;
}

size_t SkAutoPixmapStorage::AllocSize(const SkImageInfo& info, size_t* rowBytes) {
    const size_t rb = info.minRowBytes();
    if (rowBytes) {
        *rowBytes = rb;
    }
    const size_t size = info.computeByteSize(rb);
    return size;
}

bool SkAutoPixmapStorage::tryAlloc(const SkImageInfo& info) {
    // info may borrow this owner's metadata; retain it before clearing the old view.
    const SkImageInfo requested_info = info;
    reset();
    if (requested_info.width() < 0 || requested_info.height() < 0) return false;
    const unsigned int color = static_cast<unsigned int>(requested_info.colorType());
    const unsigned int alpha = static_cast<unsigned int>(requested_info.alphaType());
    if (color > static_cast<unsigned int>(kLastEnum_SkColorType) ||
        alpha > static_cast<unsigned int>(kLastEnum_SkAlphaType)) return false;
    size_t row_bytes = 0;
    const size_t size = AllocSize(requested_info, &row_bytes);
    if (SkImageInfo::ByteSizeOverflowed(size)) return false;
    void* pixels = sk_malloc_canfail(size);
    if (pixels == nullptr) return false;
    INHERITED::reset(requested_info, pixels, row_bytes);
    fStorage = pixels;
    return true;
}

void SkAutoPixmapStorage::alloc(const SkImageInfo& info) {
    const bool allocated = tryAlloc(info);
    SkASSERT_RELEASE(allocated);
}

void* SkAutoPixmapStorage::detachPixels() {
    if (!fStorage) {
        return nullptr;
    }

    void* data = fStorage;
    fStorage = nullptr;
    INHERITED::reset();

    return data;
}

sk_sp<SkData> SkAutoPixmapStorage::detachPixelsAsData() {
    if (!fStorage) {
        return nullptr;
    }

    const size_t length = computeByteSize();
    sk_sp<SkData> data = SkData::MakeFromMalloc(fStorage, length);
    fStorage = nullptr;
    INHERITED::reset();

    return data;
}
