/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkAutoSTArray_DEFINED
#define SkAutoSTArray_DEFINED

#include "include/private/SkAlign.h"
#include "include/private/SkAssert.h"
#include "include/private/SkMalloc.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace skia_private {

// Constructed elements in inline or heap storage. fCount is the live length,
// not allocation capacity. Trimming destroys a tail but retains its storage.
// Reset replaces all elements; equal positive counts reuse the existing block.
template <int kCountRequested, typename T> class AutoSTArray {
private:
    static_assert(kCountRequested >= 0);
    static_assert(std::is_nothrow_destructible_v<T>);
    static_assert(alignof(T) <= alignof(std::max_align_t));
    static constexpr size_t kAlignment = std::max(alignof(T*), alignof(T));
    static_assert(static_cast<size_t>(kCountRequested) <=
                  (std::numeric_limits<size_t>::max() - sizeof(int) - kAlignment + 1) / sizeof(T));
#if defined(SK_BUILD_FOR_GOOGLE3)
    static constexpr size_t kMaxBytes = 4 * 1024;
    static constexpr size_t kMinCount = static_cast<size_t>(kCountRequested) * sizeof(T) > kMaxBytes
        ? kMaxBytes / sizeof(T) : static_cast<size_t>(kCountRequested);
#else
    static constexpr size_t kMinCount = static_cast<size_t>(kCountRequested);
#endif
    // Retain the original capacity rounding, including space gained from padding
    // around the trailing count. No separate capacity field is required.
    static_assert(alignof(int) <= alignof(T*) || alignof(int) <= alignof(T));
    static constexpr size_t kInlineCount =
        SkAlignTo(kMinCount * sizeof(T) + sizeof(int), kAlignment) / sizeof(T);
    static_assert(kInlineCount > 0);
    static_assert(kInlineCount <= static_cast<size_t>(std::numeric_limits<int>::max()));

    struct FreeHeap {
        void operator()(T* storage) const { sk_free(storage); }
    };

public:
    static constexpr int kCount = static_cast<int>(kInlineCount);

    AutoSTArray() = default;
    explicit AutoSTArray(int count) { reset(count); }
    AutoSTArray(const AutoSTArray&) = delete;
    AutoSTArray& operator=(const AutoSTArray&) = delete;
    AutoSTArray(AutoSTArray&& source) noexcept(std::is_nothrow_move_constructible_v<T>) {
        T* sourceInline = reinterpret_cast<T*>(source.fStorage);
        if (source.fArray == sourceInline) {
            T* inlineStorage = reinterpret_cast<T*>(fStorage);
            if (source.fCount != 0) {
                std::uninitialized_move_n(source.fArray, source.fCount, inlineStorage);
            }
            fArray = inlineStorage;
            fCount = source.fCount;
            source.destroy_tail(0);
        } else {
            fArray = source.fArray;
            fCount = source.fCount;
            source.fCount = 0;
        }
        source.fArray = nullptr;
    }
    AutoSTArray& operator=(AutoSTArray&&) = delete;

    ~AutoSTArray() {
        destroy_tail(0);
        T* inlineStorage = reinterpret_cast<T*>(fStorage);
        if (fArray != inlineStorage) sk_free(fArray);
    }

    // Destroy old elements in reverse order, then default-construct replacements.
    // Scalar elements remain uninitialized. Heap replacement releases first.
    // In exception-enabled consumers, construction failure leaves this owner empty;
    // the standard construction algorithm cleans its prefix and the local owner
    // frees heap storage. Production builds do not enable exception handling.
    [[clang::reinitializes]]
    void reset(int count) {
        SkASSERT_RELEASE(count >= 0);
        const size_t requested = static_cast<size_t>(count);
        SkASSERT_RELEASE(requested <= std::numeric_limits<size_t>::max() / sizeof(T));
        const int previousCount = fCount;
        destroy_tail(0);
        T* inlineStorage = reinterpret_cast<T*>(fStorage);
        T* replacement = fArray;
        fArray = nullptr;
        if (count != previousCount || count == 0) {
            if (replacement != inlineStorage) sk_free(replacement);
            replacement = nullptr;
            if (count > kCount) {
                const size_t bytes = requested * sizeof(T);
                replacement = static_cast<T*>(sk_malloc_throw(bytes));
            } else if (count != 0) {
                replacement = inlineStorage;
            }
        }
        T* heapStorage = replacement == inlineStorage ? nullptr : replacement;
        std::unique_ptr<T, FreeHeap> pending(heapStorage);
        if (count != 0) std::uninitialized_default_construct_n(replacement, requested);
        pending.release();
        fArray = replacement;
        fCount = count;
    }

    // Retain allocation and prefix elements. A zero live count can therefore have
    // non-null begin/end pointers until reset or destruction releases the storage.
    void trimTo(int count) {
        SkASSERT_RELEASE(count >= 0);
        if (count >= fCount) return;
        destroy_tail(count);
    }

    int count() const { return fCount; }
    size_t size() const {
        const size_t count = static_cast<size_t>(fCount);
        return count;
    }
    T* get() & { return fArray; }
    const T* get() const & { return fArray; }
    T* get() && = delete;
    const T* get() const && = delete;
    T* data() & { return fArray; }
    const T* data() const & { return fArray; }
    T* data() && = delete;
    const T* data() const && = delete;
    T* begin() & { return fArray; }
    const T* begin() const & { return fArray; }
    T* begin() && = delete;
    const T* begin() const && = delete;
    T* end() & {
        if (fCount == 0) return fArray;
        T* end = fArray + fCount;
        return end;
    }
    const T* end() const & {
        if (fCount == 0) return fArray;
        const T* end = fArray + fCount;
        return end;
    }
    T* end() && = delete;
    const T* end() const && = delete;
    T& operator[](int index) & {
        const int checkedIndex = sk_collection_check_bounds(index, fCount);
        return fArray[checkedIndex];
    }
    const T& operator[](int index) const & {
        const int checkedIndex = sk_collection_check_bounds(index, fCount);
        return fArray[checkedIndex];
    }
    T& operator[](int) && = delete;
    const T& operator[](int) const && = delete;

private:
    // Callers establish 0 <= retained <= fCount. Publish the new live length after
    // destruction, preserving the original reverse destruction order.
    void destroy_tail(int retained) noexcept {
        for (int index = fCount; index > retained;) {
            --index;
            std::destroy_at(fArray + index);
        }
        fCount = retained;
    }

    T* fArray = nullptr;
    alignas(T) std::byte fStorage[kInlineCount * sizeof(T)];
    int fCount = 0;
};

} // namespace skia_private

#endif
