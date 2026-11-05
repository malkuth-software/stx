/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkTemplates_DEFINED
#define SkTemplates_DEFINED

#include "include/private/SkAutoSTArray.h"
#include "include/private/SkAutoSTMalloc.h"
#include "include/private/SkAutoTArray.h"
#include "include/private/SkAutoTMalloc.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkTLogic.h"

#include <cstddef>
#include <memory>

// Arguments are evaluated normally, including side effects, before this no-op.
template <typename T> inline void sk_ignore_unused_variable(const T&) {}

// Integer inputs require a representable positive magnitude. Preserve the
// original floating comparison: negative zero and NaN pass through unchanged.
template <typename T> static inline T SkTAbs(T value) {
    if (value < 0) value = -value;
    return value;
}

// Borrow within the same allocation, with destination alignment and object
// lifetime established by the caller. Zero offset also admits a null pointer.
// Preserve const/volatile qualification through the intermediate byte pointer.
template <typename D, typename S> inline D* SkTAddOffset(S* pointer, ptrdiff_t byteOffset) {
    using Byte = sknonstd::same_cv_t<char, D>;
    Byte* bytes = reinterpret_cast<Byte*>(pointer);
    Byte* adjusted = bytes + byteOffset;
    D* result = reinterpret_cast<D*>(adjusted);
    return result;
}

namespace skia_private {
struct SkFreeStorage {
    void operator()(void* storage) const { sk_free(storage); }
};
using UniqueVoidPtr = std::unique_ptr<void, SkFreeStorage>;
} // namespace skia_private

#endif
