/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkAutoTArray_DEFINED
#define SkAutoTArray_DEFINED

#include "include/private/SkAssert.h"

#include <cstddef>
#include <limits>
#include <memory>
#include <utility>

namespace skia_private {

// Sole owner of a fixed-length array of constructed elements. Scalar elements
// are uninitialized until the caller writes them. Reset replaces all elements;
// move transfers the allocation and leaves the source empty. Borrowed elements
// survive a move under the destination owner. Replacing or destroying an array
// ends borrows of its old elements, including replacement by move assignment.
template <typename T> class AutoTArray {
public:
    AutoTArray() = default;
    explicit AutoTArray(size_t count) {
        SkASSERT_RELEASE(count <= std::numeric_limits<size_t>::max() / sizeof(T));
        if (count != 0) {
            T* storage = new T[count];
            fData.reset(storage);
            fSize = count;
        }
    }
    explicit AutoTArray(int count) {
        SkASSERT_RELEASE(count >= 0);
        const size_t size = static_cast<size_t>(count);
        reset(size);
    }

    AutoTArray(const AutoTArray&) = delete;
    AutoTArray& operator=(const AutoTArray&) = delete;
    AutoTArray(AutoTArray&& source) noexcept
        : fData(std::move(source.fData)), fSize(source.fSize) {
        source.fSize = 0;
    }
    AutoTArray& operator=(AutoTArray&& source) noexcept {
        if (this != &source) {
            fData = std::move(source.fData);
            fSize = source.fSize;
            source.fSize = 0;
        }
        return *this;
    }

    // Construct the replacement before committing. With exceptions enabled, a
    // failed allocation or element constructor leaves this array unchanged.
    // The production library is built without exception handling.
    [[clang::reinitializes]]
    void reset(size_t count = 0) {
        AutoTArray replacement(count);
        *this = std::move(replacement);
    }

    T* get() & {
        T* storage = fData.get();
        return storage;
    }
    const T* get() const & {
        const T* storage = fData.get();
        return storage;
    }
    T* get() && = delete;
    const T* get() const && = delete;

    T& operator[](size_t index) & {
        const size_t checkedIndex = sk_collection_check_bounds(index, fSize);
        return fData[checkedIndex];
    }
    const T& operator[](size_t index) const & {
        const size_t checkedIndex = sk_collection_check_bounds(index, fSize);
        return fData[checkedIndex];
    }
    T& operator[](size_t) && = delete;
    const T& operator[](size_t) const && = delete;

    T* data() & {
        T* storage = fData.get();
        return storage;
    }
    const T* data() const & {
        const T* storage = fData.get();
        return storage;
    }
    T* data() && = delete;
    const T* data() const && = delete;

    size_t size() const { return fSize; }
    bool empty() const {
        const bool result = fSize == 0;
        return result;
    }
    size_t size_bytes() const {
        const size_t bytes = sizeof(T) * fSize;
        return bytes;
    }

    T* begin() & {
        T* storage = fData.get();
        return storage;
    }
    const T* begin() const & {
        const T* storage = fData.get();
        return storage;
    }
    T* begin() && = delete;
    const T* begin() const && = delete;

    T* end() & {
        if (fSize == 0) return nullptr;
        T* storage = fData.get();
        T* end = storage + fSize;
        return end;
    }
    const T* end() const & {
        if (fSize == 0) return nullptr;
        const T* storage = fData.get();
        const T* end = storage + fSize;
        return end;
    }
    T* end() && = delete;
    const T* end() const && = delete;

private:
    std::unique_ptr<T[]> fData{};
    size_t fSize = 0;
};

} // namespace skia_private

#endif
