/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkMallocPixelRef.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkColorType.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixelRef.h"
#include "include/private/SkMalloc.h"

#include <utility>

namespace {
bool is_valid(const SkImageInfo& info) {
    const unsigned int color_type = static_cast<unsigned int>(info.colorType());
    const unsigned int alpha_type = static_cast<unsigned int>(info.alphaType());
    if (info.width() < 0 || info.height() < 0 ||
        color_type > static_cast<unsigned int>(kLastEnum_SkColorType) ||
        alpha_type > static_cast<unsigned int>(kLastEnum_SkAlphaType)) {
        return false;
    }
    return true;
}

class AllocatedPixelRef final : public SkPixelRef {
public:
    AllocatedPixelRef(int width, int height, void* pixels, size_t row_bytes)
        : SkPixelRef(width, height, pixels, row_bytes) {}

    ~AllocatedPixelRef() override {
        sk_free(pixels());
    }
};

class DataPixelRef final : public SkPixelRef {
public:
    DataPixelRef(int width, int height, void* pixels, size_t row_bytes, sk_sp<SkData> data)
        : SkPixelRef(width, height, pixels, row_bytes), fData(std::move(data)) {}

private:
    // Retain the data owner for every access through the base pixel view.
    sk_sp<SkData> fData;
};
}

sk_sp<SkPixelRef> SkMallocPixelRef::MakeAllocate(const SkImageInfo& info, size_t row_bytes) {
    if (row_bytes == 0) {
        // Unknown color types and overflowing minimum strides may still give zero.
        row_bytes = info.minRowBytes();
    }
    if (!is_valid(info) || !info.validRowBytes(row_bytes)) return nullptr;
    const std::optional<size_t> size = info.checkedByteSize(row_bytes);
    if (!size) return nullptr;
#if defined(SK_BUILD_FOR_FUZZER)
    if (*size > 10000000) return nullptr;
#endif
    void* pixels = sk_calloc_canfail(*size);
    if (pixels == nullptr) return nullptr;
    sk_sp<SkPixelRef> result(new AllocatedPixelRef(info.width(), info.height(), pixels, row_bytes));
    return result;
}

sk_sp<SkPixelRef> SkMallocPixelRef::MakeWithData(const SkImageInfo& info,
                                              size_t row_bytes, sk_sp<SkData> data) {
    if (!data || !is_valid(info) || !info.validRowBytes(row_bytes)) return nullptr;
    const std::optional<size_t> required_bytes = info.checkedByteSize(row_bytes);
    if (!required_bytes) return nullptr;
    if ((*data).size() < *required_bytes) return nullptr;
    if (*required_bytes != 0 && (*data).data() == nullptr) return nullptr;

    void* pixels = const_cast<void*>((*data).data());
    sk_sp<SkPixelRef> result(new DataPixelRef(info.width(), info.height(), pixels,
                                           row_bytes, std::move(data)));
    (*result).setImmutable();
    return result;
}
