/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkAutoSTMalloc_DEFINED
#define SkAutoSTMalloc_DEFINED

#include "include/private/SkAlign.h"
#include "include/private/SkAssert.h"
#include "include/private/SkMalloc.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace skia_private {

// Inline or heap storage for trivial elements, with no live-length metadata.
// The caller owns the initialized extent and any borrows. Replacement invalidates
// those borrows. An inline move copies capacity; a heap move transfers its address.
template <size_t kCountRequested, typename T,
          typename = std::enable_if_t<std::is_trivially_default_constructible_v<T> &&
                                      std::is_trivially_destructible_v<T>>>
class AutoSTMalloc {
private:
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(alignof(T) <= alignof(std::max_align_t));
    static_assert(kCountRequested > 0);
    static_assert(kCountRequested <= (std::numeric_limits<size_t>::max() - 3) / sizeof(T));
    static constexpr size_t kCountWithPadding = SkAlign4(kCountRequested * sizeof(T)) / sizeof(T);
#if defined(SK_BUILD_FOR_GOOGLE3)
    static constexpr size_t kMaxBytes = 4 * 1024;
    static constexpr size_t kMinCount = kCountRequested * sizeof(T) > kMaxBytes
        ? kMaxBytes / sizeof(T) : kCountWithPadding;
#else
    static constexpr size_t kMinCount = kCountWithPadding;
#endif

public:
    static constexpr size_t kCount = kMinCount;

    // Default construction exposes inline capacity. An explicit zero request is
    // empty instead, preserving the established distinction between these forms.
    AutoSTMalloc() { fPtr = reinterpret_cast<T*>(fStorage); }
    explicit AutoSTMalloc(size_t count) {
        SkASSERT_RELEASE(count <= std::numeric_limits<size_t>::max() / sizeof(T));
        if (count > kCount) {
            const size_t bytes = count * sizeof(T);
            fPtr = static_cast<T*>(sk_malloc_throw(bytes));
        }
        else if (count != 0) fPtr = reinterpret_cast<T*>(fStorage);
    }
    AutoSTMalloc(const AutoSTMalloc&) = delete;
    AutoSTMalloc& operator=(const AutoSTMalloc&) = delete;
    AutoSTMalloc(AutoSTMalloc&& source) noexcept {
        T* sourceInline = reinterpret_cast<T*>(source.fStorage);
        if (source.fPtr == sourceInline) {
            fPtr = reinterpret_cast<T*>(fStorage);
            // The extent is caller-owned, so copy the whole inline capacity as
            // bytes, including uninitialized bytes; no T values are evaluated.
            std::memcpy(fStorage, source.fStorage, kCount * sizeof(T));
        } else {
            fPtr = source.fPtr;
        }
        source.fPtr = nullptr;
    }
    AutoSTMalloc& operator=(AutoSTMalloc&&) = delete;

    ~AutoSTMalloc() {
        T* inlineStorage = reinterpret_cast<T*>(fStorage);
        if (fPtr != inlineStorage) sk_free(fPtr);
    }

    // Discard contents. Heap storage is released before replacement, preserving
    // the original peak-allocation policy. Allocation failure terminates.
    [[clang::reinitializes]]
    T* reset(size_t count) & {
        SkASSERT_RELEASE(count <= std::numeric_limits<size_t>::max() / sizeof(T));
        T* inlineStorage = reinterpret_cast<T*>(fStorage);
        if (fPtr != inlineStorage) sk_free(fPtr);
        if (count > kCount) {
            const size_t bytes = count * sizeof(T);
            fPtr = static_cast<T*>(sk_malloc_throw(bytes));
        }
        else if (count != 0) fPtr = inlineStorage;
        else fPtr = nullptr;
        return fPtr;
    }
    T* reset(size_t) && = delete;

    // Preserve the smaller old/new extent. Inline growth copies inline capacity;
    // heap shrink remains on the heap, even when it would fit inline. Empty growth
    // also uses the heap. Call reset to select inline storage again.
    void realloc(size_t count) {
        SkASSERT_RELEASE(count <= std::numeric_limits<size_t>::max() / sizeof(T));
        if (count == 0) {
            reset(0);
            return;
        }
        T* inlineStorage = reinterpret_cast<T*>(fStorage);
        const size_t bytes = count * sizeof(T);
        if (fPtr == inlineStorage) {
            if (count <= kCount) return;
            T* storage = static_cast<T*>(sk_malloc_throw(bytes));
            std::memcpy(storage, fStorage, kCount * sizeof(T));
            fPtr = storage;
            return;
        }
        T* storage = static_cast<T*>(sk_realloc_throw(fPtr, bytes));
        fPtr = storage;
    }

    T* get() & { return fPtr; }
    const T* get() const & { return fPtr; }
    T* get() && = delete;
    const T* get() const && = delete;
    operator T*() & { return fPtr; }
    operator const T*() const & { return fPtr; }
    operator T*() && = delete;
    operator const T*() const && = delete;

    // The caller establishes that index belongs to its live extent.
    T& operator[](int index) & { return fPtr[index]; }
    const T& operator[](int index) const & { return fPtr[index]; }
    T& operator[](int) && = delete;
    const T& operator[](int) const && = delete;
    T* data() & { return fPtr; }
    const T* data() const & { return fPtr; }
    T* data() && = delete;
    const T* data() const && = delete;

private:
    static_assert(kCount > 0);
    T* fPtr = nullptr;
    // Byte storage begins the implicit lifetimes needed by the trivial elements.
    // Keep the original four-byte padding and alignment as well as T's alignment.
    alignas(T) alignas(uint32_t) std::byte fStorage[SkAlign4(kCount * sizeof(T))];
};

} // namespace skia_private

#endif
