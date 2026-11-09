/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkAutoTMalloc_DEFINED
#define SkAutoTMalloc_DEFINED

#include "include/private/SkAssert.h"
#include "include/private/SkMalloc.h"

#include <cstddef>
#include <limits>
#include <memory>
#include <type_traits>

namespace skia_private {

// Sole owner of malloc-compatible storage for trivial elements. The caller keeps
// the live extent and initializes bytes before reading them. No count is stored.
// Borrows end on reset, realloc, release or destruction; moves transfer ownership.
template <typename T,
          typename = std::enable_if_t<std::is_trivially_default_constructible_v<T> &&
                                      std::is_trivially_destructible_v<T>>>
class AutoTMalloc {
public:
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(alignof(T) <= alignof(std::max_align_t));

    // Adopt storage that can be released with sk_free; null is an empty owner.
    explicit AutoTMalloc(T* pointer = nullptr) : fPtr(pointer) {}
    explicit AutoTMalloc(size_t count) {
        if (count != 0) {
            T* storage = static_cast<T*>(sk_malloc_throw(count, sizeof(T)));
            fPtr.reset(storage);
        }
    }

    AutoTMalloc(const AutoTMalloc&) = delete;
    AutoTMalloc& operator=(const AutoTMalloc&) = delete;
    AutoTMalloc(AutoTMalloc&&) noexcept = default;
    AutoTMalloc& operator=(AutoTMalloc&&) noexcept = default;

    // Preserve the smaller old/new byte extent. Growth is uninitialized. Check
    // the product before releasing ownership; allocation failure terminates.
    void realloc(size_t count) {
        SkASSERT_RELEASE(count <= std::numeric_limits<size_t>::max() / sizeof(T));
        if (count == 0) {
            fPtr.reset();
            return;
        }
        const size_t bytes = count * sizeof(T);
        T* previous = fPtr.release();
        T* storage = static_cast<T*>(sk_realloc_throw(previous, bytes));
        fPtr.reset(storage);
    }

    // Replace storage even at the same count; no contents are preserved. Prepare
    // the new allocation before releasing the old one, as in the original helper.
    [[clang::reinitializes]]
    T* reset(size_t count = 0) & {
        T* storage = nullptr;
        if (count != 0) storage = static_cast<T*>(sk_malloc_throw(count, sizeof(T)));
        fPtr.reset(storage);
        return storage;
    }
    T* reset(size_t = 0) && = delete;

    T* get() & {
        T* storage = fPtr.get();
        return storage;
    }
    const T* get() const & {
        const T* storage = fPtr.get();
        return storage;
    }
    T* get() && = delete;
    const T* get() const && = delete;

    operator T*() & {
        T* storage = fPtr.get();
        return storage;
    }
    operator const T*() const & {
        const T* storage = fPtr.get();
        return storage;
    }
    operator T*() && = delete;
    operator const T*() const && = delete;

    // The caller establishes that index belongs to its live extent.
    T& operator[](int index) & {
        T* storage = fPtr.get();
        return storage[index];
    }
    const T& operator[](int index) const & {
        const T* storage = fPtr.get();
        return storage[index];
    }
    T& operator[](int) && = delete;
    const T& operator[](int) const && = delete;

    T* data() & {
        T* storage = fPtr.get();
        return storage;
    }
    const T* data() const & {
        const T* storage = fPtr.get();
        return storage;
    }
    T* data() && = delete;
    const T* data() const && = delete;

    // Transfer the allocation and sk_free obligation to the caller.
    T* release() {
        T* storage = fPtr.release();
        return storage;
    }

private:
    struct FreeStorage {
        void operator()(T* storage) const { sk_free(storage); }
    };
    std::unique_ptr<T, FreeStorage> fPtr{};
};

} // namespace skia_private

#endif
