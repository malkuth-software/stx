/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkBuffer.h"

#include "include/private/SkAlign.h"
#include "include/private/SkMalloc.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

///////////////////////////////////////////////////////////////////////////////////////////////////

const void* SkRBuffer::skip(size_t size) {
    if (fValid && size <= (*this).available()) {
        const void* pos = fPos;
        fPos += size;
        return pos;
    }
    fValid = false;
    return nullptr;
}

bool SkRBuffer::read(void* buffer, size_t size) {
    if (const void* src = (*this).skip(size)) {
        if (buffer != nullptr) {
            sk_careful_memcpy(buffer, src, size);
        }
        return true;
    }
    return false;
}

bool SkRBuffer::skipToAlign4() {
    const intptr_t pos = reinterpret_cast<intptr_t>(fPos);
    const size_t n = static_cast<size_t>(SkAlign4(pos) - pos);
    if (fValid && n <= (*this).available()) {
        fPos += n;
        return true;
    } else {
        fValid = false;
        return false;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void* SkWBuffer::skip(size_t size) {
    const size_t start = fOffset;
    (*this).writeBytes(nullptr, size);
    return fData == nullptr ? nullptr : fData + start;
}

void SkWBuffer::writeBytes(const void* buffer, size_t size) {
    if (fBounded) {
        SkASSERT_RELEASE(size <= fCapacity - fOffset);
    }
    if (fData && buffer) {
        sk_careful_memcpy(fData + fOffset, buffer, size);
    }
    fOffset += size;
}

size_t SkWBuffer::padToAlign4() {
    const size_t pos = (*this).pos();
    const size_t n = SkAlign4(pos) - pos;

    if (fBounded) {
        SkASSERT_RELEASE(n <= fCapacity - fOffset);
    }
    if (n && fData) {
        std::memset(fData + fOffset, 0, n);
    }
    fOffset += n;
    return n;
}
