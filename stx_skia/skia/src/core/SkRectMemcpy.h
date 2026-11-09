/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRectMemcpy_DEFINED
#define SkRectMemcpy_DEFINED

#include "include/private/SkAssert.h"
#include "src/core/SkSafeMath.h"

#include <cstdint>
#include <cstring>

// Caller supplies valid disjoint row ranges or the identical view. The last
// row need not include trailing stride padding. Empty copies access no storage.
static inline void SkRectMemcpy(void* dst, size_t dstRB, const void* src, size_t srcRB,
                                size_t trimRowBytes, int rowCount) {
    if (rowCount <= 0 || trimRowBytes == 0) return;
    SkASSERT(rowCount == 1 || trimRowBytes <= dstRB);
    SkASSERT(rowCount == 1 || trimRowBytes <= srcRB);
    if (dst == src && (rowCount == 1 || dstRB == srcRB)) return;
    const size_t rows = static_cast<size_t>(rowCount);
    SkSafeMath extent;
    const size_t src_last_row = extent.mul(rows - 1, srcRB);
    const size_t dst_last_row = extent.mul(rows - 1, dstRB);
    extent.add(src_last_row, trimRowBytes);
    extent.add(dst_last_row, trimRowBytes);
    SkASSERT_RELEASE(extent.ok());
    if (trimRowBytes == dstRB && trimRowBytes == srcRB) {
        const size_t bytes = extent.mul(trimRowBytes, rows);
        SkASSERT_RELEASE(extent.ok());
        std::memcpy(dst, src, bytes);
        return;
    }
    uint8_t* dst_bytes = static_cast<uint8_t*>(dst);
    const uint8_t* src_bytes = static_cast<const uint8_t*>(src);
    // The offsets never exceed the last-row offsets validated above.
    size_t dst_offset = 0;
    size_t src_offset = 0;
    for (size_t row = 0; row < rows; ++row) {
        std::memcpy(dst_bytes + dst_offset, src_bytes + src_offset, trimRowBytes);
        dst_offset += dstRB;
        src_offset += srcRB;
    }
}

#endif
