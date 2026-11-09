/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkWriter32.h"

#include "include/core/SkSamplingOptions.h"
#include "src/core/SkSafeMath.h"
#include "src/core/SkMatrixPriv.h"

#include <algorithm>

class SkMatrix;

void SkWriter32::writeMatrix(const SkMatrix& matrix) {
    const size_t bytes = SkMatrixPriv::WriteToMemory(matrix, nullptr);
    void* destination = reserve(bytes);
    SkMatrixPriv::WriteToMemory(matrix, destination);
}

void SkWriter32::writeSampling(const SkSamplingOptions& sampling) {
    write32(sampling.maxAniso);
    if (sampling.isAniso()) return;
    writeBool(sampling.useCubic);
    if (sampling.useCubic) {
        writeScalar(sampling.cubic.B);
        writeScalar(sampling.cubic.C);
        return;
    }
    const int32_t filter = static_cast<int32_t>(sampling.filter);
    const int32_t mipmap = static_cast<int32_t>(sampling.mipmap);
    write32(filter);
    write32(mipmap);
}

void SkWriter32::writeString(const char* text, size_t length) {
    if (text == nullptr) {
        text = "";
        length = 0;
    }
    if (length == UnknownLength) length = std::strlen(text);
    const size_t bytes = WriteStringSize(text, length);
    uint8_t* storage = reinterpret_cast<uint8_t*>(reserve(bytes));
    const uint32_t encodedLength = static_cast<uint32_t>(length);
    std::memcpy(storage, &encodedLength, sizeof(encodedLength));
    uint8_t* characters = storage + sizeof(encodedLength);
    if (length != 0) std::memcpy(characters, text, length);
    const size_t padding = bytes - sizeof(encodedLength) - length;
    std::memset(characters + length, 0, padding);
}

size_t SkWriter32::WriteStringSize(const char* text, size_t length) {
    if (text == nullptr) length = 0;
    else if (length == UnknownLength) length = std::strlen(text);
    SkASSERT_RELEASE(length <= std::numeric_limits<uint32_t>::max());
    SkASSERT_RELEASE(length <= MaximumExtent - 8);
    const size_t bytes = SkAlign4(sizeof(uint32_t) + length + 1);
    return bytes;
}

void SkWriter32::growToAtLeast(size_t size) {
    const bool wasExternal = fExternal != nullptr && fData == fExternal;
    SkSafeMath safe{};
    const size_t geometric = safe.add(fCapacity, fCapacity / 2);
    const size_t requested = std::max(size, geometric);
    size_t capacity = safe.add(4096, requested);
    // Preserve ordinary growth, but near the representable limit allocate only
    // the requested extent rather than overflowing speculative spare capacity.
    if (!safe.ok() || capacity > MaximumExtent) capacity = size;
    fInternal.realloc(capacity);
    uint8_t* replacement = fInternal.get();
    if (wasExternal && fUsed != 0) std::memcpy(replacement, fExternal, fUsed);
    fData = replacement;
    fCapacity = capacity;
}

sk_sp<SkData> SkWriter32::snapshotAsData() const {
    if (fUsed == 0) {
        sk_sp<SkData> empty = SkData::MakeEmpty();
        return empty;
    }
    sk_sp<SkData> data = SkData::MakeWithCopy(fData, fUsed);
    return data;
}
