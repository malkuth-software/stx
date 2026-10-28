/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkTDArray_DEFINED
#define SkTDArray_DEFINED

#include "include/private/SkAPI.h"
#include "include/private/SkAssert.h"
#include "include/private/SkDebug.h"
#include "include/private/SkTo.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <type_traits>
#include <initializer_list>
#include <utility>

// Owns raw contiguous storage. Counts are elements; allocation failure aborts.
// Mutation requires exclusive access. Borrows survive only until reallocation,
// reset or destruction; erased elements are no longer live. No element callbacks.
// Assignment and swap require equal element widths. Invalid extents abort before
// mutation. Move empties the source; clear retains allocation, reset releases it.

class SK_SPI SkTDStorage {
public:
    explicit SkTDStorage(int sizeOfT);
    SkTDStorage(const void* src, int size, int sizeOfT);

    // Copy
    SkTDStorage(const SkTDStorage& that);
    SkTDStorage& operator= (const SkTDStorage& that);

    // Move
    SkTDStorage(SkTDStorage&& that);
    SkTDStorage& operator= (SkTDStorage&& that);

    ~SkTDStorage();

    void reset();
    void swap(SkTDStorage& that);

    // Size routines
    bool empty() const { const bool result = fSize == 0; return result; }
    void clear() { fSize = 0; }
    int size() const { return fSize; }
    void resize(int newSize);
    size_t size_bytes() const { const size_t result = bytes(fSize); return result; }

    // Capacity routines
    int capacity() const { return fCapacity; }
    void reserve(int newCapacity);
    void shrink_to_fit();

    void* data() & { return fStorage; }
    const void* data() const & { return fStorage; }

    void* data() && = delete;
    const void* data() const && = delete;

    // Deletion routines
    void erase(int index, int count);
    // Removes the entry at 'index' and replaces it with the last array element
    void removeShuffle(int index);

    // Insertion routines
    void* prepend() &;
    void* prepend() && = delete;

    void append();
    void append(int count);
    void* append(const void* src, int count) &;
    void* append(const void*, int) && = delete;

    void* insert(int index) &;
    void* insert(int) && = delete;
    void* insert(int index, int count, const void* src) &;
    void* insert(int, int, const void*) && = delete;

    void pop_back() {
        SkASSERT_RELEASE(fSize > 0);
        fSize--;
    }

    friend bool operator==(const SkTDStorage& a, const SkTDStorage& b);
    friend bool operator!=(const SkTDStorage& a, const SkTDStorage& b) {
        const bool different = !(a == b); return different;
    }

private:
    // Fast path for pointer arithmetic. Assumes 'n' has already been bounds-checked.
    size_t bytes(int n) const {
        const size_t result = static_cast<size_t>(n) * static_cast<size_t>(fSizeOfT);
        return result;
    }

    // Safe path for memory allocations; protects against 32-bit overflow.
    size_t safe_bytes(int n) const {
        SkASSERT_RELEASE(n >= 0);
        const size_t size = static_cast<size_t>(n);
        const size_t sizeOfT = static_cast<size_t>(fSizeOfT);
        SkASSERT_RELEASE(size <= SIZE_MAX / sizeOfT);
        const size_t result = size * sizeOfT;
        return result;
    }

    void* address(int n) {
        if (fStorage == nullptr) return nullptr;
        void* result = fStorage + bytes(n);
        return result;
    }

    // Move the tail of the array defined by the indexes tailStart and tailEnd to dstIndex. The
    // elements at dstIndex are overwritten by the tail.
    void moveTail(int dstIndex, int tailStart, int tailEnd);

    const int fSizeOfT;
    std::byte* fStorage{nullptr};
    int fCapacity{0};  // size of the allocation (#elements)
    int fSize{0};    // logical number of elements (fSize <= fCapacity)
};

static inline void swap(SkTDStorage& a, SkTDStorage& b) {
    a.swap(b);
}

// SkTDArray<T> implements a std::vector-like array for raw data-only objects that do not require
// construction or destruction. The constructor and destructor for T will not be called; T objects
// will always be moved via raw memcpy. Newly created T objects will contain uninitialized memory.
template <typename T> class SkTDArray {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::is_trivially_destructible_v<T>);
    static_assert(std::is_trivially_default_constructible_v<T> ||
                  std::is_trivially_copy_constructible_v<T> ||
                  std::is_trivially_move_constructible_v<T>);
    static_assert(alignof(T) <= alignof(std::max_align_t));
    static_assert(sizeof(T) <= INT_MAX);
public:
    SkTDArray() : fStorage{static_cast<int>(sizeof(T))} {}
    SkTDArray(const T src[], int count) : fStorage{src, count, static_cast<int>(sizeof(T))} { }
    SkTDArray(const std::initializer_list<T>& list) : SkTDArray(list.begin(), checked_count(list.size())) {}

    // Copy
    SkTDArray(const SkTDArray<T>& src) : SkTDArray(src.data(), src.size()) {}
    SkTDArray<T>& operator=(const SkTDArray<T>& src) {
        fStorage = src.fStorage;
        return *this;
    }

    // Move
    SkTDArray(SkTDArray<T>&& src) : fStorage{std::move(src.fStorage)} {}
    SkTDArray<T>& operator=(SkTDArray<T>&& src) {
        fStorage = std::move(src.fStorage);
        return *this;
    }

    friend bool operator==(const SkTDArray<T>& a, const SkTDArray<T>& b) {
        const bool equal = a.fStorage == b.fStorage;
        return equal;
    }
    friend bool operator!=(const SkTDArray<T>& a, const SkTDArray<T>& b) { const bool different = !(a == b); return different; }

    void swap(SkTDArray<T>& that) {
        fStorage.swap(that.fStorage);
    }

    bool empty() const { const bool result = fStorage.empty(); return result; }

    // Return the number of elements in the array
    int size() const { const int result = fStorage.size(); return result; }

    // Return the total number of elements allocated.
    // Note: capacity() - size() gives you the number of elements you can add without causing an
    // allocation.
    int capacity() const { const int result = fStorage.capacity(); return result; }

    // return the number of bytes in the array: count * sizeof(T)
    size_t size_bytes() const { const size_t result = fStorage.size_bytes(); return result; }

    T* data() & {
        T* result = static_cast<T*>(fStorage.data());
        return result;
    }
    const T* data() const & {
        const T* result = static_cast<const T*>(fStorage.data());
        return result;
    }
    T* data() && = delete;
    const T* data() const && = delete;
    T* begin() & { T* result = data(); return result; }
    const T* begin() const & { const T* result = data(); return result; }
    T* begin() && = delete;
    const T* begin() const && = delete;
    T* end() & {
        T* result = data();
        if (fStorage.size() != 0) result += fStorage.size();
        return result;
    }
    const T* end() const & {
        const T* result = data();
        if (fStorage.size() != 0) result += fStorage.size();
        return result;
    }
    T* end() && = delete;
    const T* end() const && = delete;
    T& operator[](int index) & {
        const int checkedIndex = sk_collection_check_bounds(index, size());
        T* elements = data();
        return elements[checkedIndex];
    }
    const T& operator[](int index) const & {
        const int checkedIndex = sk_collection_check_bounds(index, size());
        const T* elements = data();
        return elements[checkedIndex];
    }
    T& operator[](int) && = delete;
    const T& operator[](int) const && = delete;
    const T& back() const & {
        sk_collection_not_empty(empty());
        const T* elements = data();
        return elements[size() - 1];
    }
    T& back() & {
        sk_collection_not_empty(empty());
        T* elements = data();
        return elements[size() - 1];
    }
    T& back() && = delete;
    const T& back() const && = delete;

    void reset() {
        fStorage.reset();
    }

    void clear() {
        fStorage.clear();
    }

    // Sets the number of elements in the array.
    // If the array does not have space for count elements, it will increase
    // the storage allocated to some amount greater than that required.
    // It will never shrink the storage.
    void resize(int count) {
        fStorage.resize(count);
    }

    void reserve(int n) {
        fStorage.reserve(n);
    }

    // Sources may be a live subrange of this array, including across insertion.
    // Null sources reserve uninitialized elements for immediate caller writes.
    T* append() & {
        fStorage.append();
        T* result = data() + size() - 1;
        return result;
    }
    T* append(int count) & {
        const int oldSize = size();
        fStorage.append(count);
        T* result = data();
        if (oldSize != 0) result += oldSize;
        return result;
    }
    T* append(int count, const T* src) & {
        T* result = static_cast<T*>(fStorage.append(src, count));
        return result;
    }
    T* insert(int index) & {
        T* result = static_cast<T*>(fStorage.insert(index));
        return result;
    }
    T* insert(int index, int count, const T* src = nullptr) & {
        T* result = static_cast<T*>(fStorage.insert(index, count, src));
        return result;
    }
    T* append() && = delete;
    T* append(int) && = delete;
    T* append(int, const T*) && = delete;
    T* insert(int) && = delete;
    T* insert(int, int, const T* = nullptr) && = delete;

    void remove(int index, int count = 1) {
        fStorage.erase(index, count);
    }

    void removeShuffle(int index) {
        fStorage.removeShuffle(index);
    }

    // routines to treat the array like a stack
    void push_back(const T& v) {
        const T value = v;
        T* destination = append();
        *destination = value;
    }
    void pop_back() { fStorage.pop_back(); }

    void shrink_to_fit() {
        fStorage.shrink_to_fit();
    }

private:
    static int checked_count(size_t count) {
        SkASSERT_RELEASE(count <= INT_MAX);
        const int result = static_cast<int>(count);
        return result;
    }
    SkTDStorage fStorage;
};

template <typename T> static inline void swap(SkTDArray<T>& a, SkTDArray<T>& b) { a.swap(b); }

#endif
