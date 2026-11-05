/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkTSort_DEFINED
#define SkTSort_DEFINED

#include "include/private/SkAssert.h"
#include "src/core/SkMathPriv.h"

#include <climits>
#include <cstddef>
#include <utility>

// In-place sorting of caller-owned live elements. Comparators must provide a
// strict weak ordering, observe operands only, and neither throw nor reenter the
// same storage. Element copy/move/swap must not throw. Sorting retains no borrows
// and allocates no scratch buffer. Equal-key order is unspecified except for
// insertion sort, which is stable. Pointer ranges belong to one array; [null,null)
// is an admitted empty range. Counts describe live elements, not capacity.
namespace skia_sort_detail {
template <typename T> struct ValueLess {
    bool operator()(const T& a, const T& b) const {
        const bool result = a < b;
        return result;
    }
};
template <typename T> struct PointeeLess {
    bool operator()(const T* a, const T* b) const {
        const bool result = *a < *b;
        return result;
    }
};
}

// Heap helpers use one-based indices. All entries except the root already form
// a max-heap. Descent to a leaf followed by upward repair is used during draining,
// when the replacement root is expected to be small. This preserves the original
// comparator and tie schedule while using one held value and a moving hole.
template <typename T, typename C>
void SkTHeapSort_SiftUp(T array[], size_t root, size_t bottom, const C& lessThan) {
    SkASSERT(root >= 1 && root <= bottom);
    const T value = array[root - 1];
    const size_t start = root;
    const size_t lastParent = bottom / 2;
    while (root <= lastParent) {
        size_t child = root * 2;
        if (child < bottom && lessThan(array[child - 1], array[child])) ++child;
        array[root - 1] = array[child - 1];
        root = child;
    }
    size_t parent = root / 2;
    while (parent >= start) {
        if (!lessThan(array[parent - 1], value)) break;
        array[root - 1] = array[parent - 1];
        root = parent;
        parent = root / 2;
    }
    array[root - 1] = value;
}

// Root and bottom are one-based. Check that a child exists before doubling its
// parent's index, so arithmetic cannot wrap even at the size_t count limit.
template <typename T, typename C>
void SkTHeapSort_SiftDown(T array[], size_t root, size_t bottom, const C& lessThan) {
    SkASSERT(root >= 1 && root <= bottom);
    const T value = array[root - 1];
    const size_t lastParent = bottom / 2;
    while (root <= lastParent) {
        size_t child = root * 2;
        if (child < bottom && lessThan(array[child - 1], array[child])) ++child;
        if (!lessThan(value, array[child - 1])) break;
        array[root - 1] = array[child - 1];
        root = child;
    }
    array[root - 1] = value;
}

template <typename T, typename C>
void SkTHeapSort(T array[], size_t count, const C& lessThan) {
    if (count < 2) return;
    SkASSERT_RELEASE(array != nullptr);
    for (size_t root = count / 2; root > 0; --root) {
        SkTHeapSort_SiftDown(array, root, count, lessThan);
    }
    for (size_t last = count - 1; last > 0; --last) {
        using std::swap;
        swap(array[0], array[last]);
        SkTHeapSort_SiftUp(array, 1, last, lessThan);
    }
}

template <typename T> void SkTHeapSort(T array[], size_t count) {
    const skia_sort_detail::ValueLess<T> less{};
    SkTHeapSort(array, count, less);
}

template <typename T, typename C>
void SkTInsertionSort(T* left, int count, const C& lessThan) {
    SkASSERT_RELEASE(count >= 0);
    if (count < 2) return;
    SkASSERT_RELEASE(left != nullptr);
    T* end = left + count;
    for (T* next = left + 1; next < end; ++next) {
        if (!lessThan(*next, *(next - 1))) continue;
        T value = std::move(*next);
        T* hole = next;
        do {
            *hole = std::move(*(hole - 1));
            --hole;
        } while (left < hole && lessThan(value, *(hole - 1)));
        *hole = std::move(value);
    }
}

// Partition a nonempty live range around one of its elements. The returned
// address borrows that range. Caller-provided swaps may specialize element moves.
template <typename T, typename C>
T* SkTQSort_Partition(T* left, int count, T* pivot, const C& lessThan) {
    SkASSERT(count > 0);
    T* right = left + count - 1;
    SkASSERT(left <= pivot && pivot <= right);
    const T pivotValue = *pivot;
    using std::swap;
    swap(*pivot, *right);
    T* newPivot = left;
    while (left < right) {
        if (lessThan(*left, pivotValue)) {
            swap(*left, *newPivot);
            ++newPivot;
        }
        ++left;
    }
    swap(*newPivot, *right);
    return newPivot;
}

// Middle-pivot introsort: insertion sort at 32 elements, heap sort at the depth
// limit. Recurse left and iterate right to retain the original ordering decisions.
// The depth limit bounds recursion; no auxiliary sorting buffer is allocated.
template <typename T, typename C>
void SkTIntroSort(int depth, T* left, int count, const C& lessThan) {
    SkASSERT(depth >= 0 && count >= 0);
    for (;;) {
        if (count <= 32) {
            SkTInsertionSort(left, count, lessThan);
            return;
        }
        if (depth == 0) {
            const size_t heapCount = static_cast<size_t>(count);
            SkTHeapSort(left, heapCount, lessThan);
            return;
        }
        --depth;
        T* middle = left + ((count - 1) / 2);
        T* pivot = SkTQSort_Partition(left, count, middle, lessThan);
        const int pivotCount = static_cast<int>(pivot - left);
        SkTIntroSort(depth, left, pivotCount, lessThan);
        left += pivotCount + 1;
        count -= pivotCount + 1;
    }
}

template <typename T, typename C>
void SkTQSort(T* begin, T* end, const C& lessThan) {
    if (begin == end) return;
    SkASSERT_RELEASE(begin != nullptr && end != nullptr);
    const std::ptrdiff_t distance = end - begin;
    SkASSERT_RELEASE(distance >= 0 && distance <= INT_MAX);
    const int count = static_cast<int>(distance);
    if (count < 2) return;
    // At most twice ceil(log2(count - 1)) recursive partitions.
    const int depth = 2 * SkNextLog2(count - 1);
    SkTIntroSort(depth, begin, count, lessThan);
}

template <typename T> void SkTQSort(T* begin, T* end) {
    const skia_sort_detail::ValueLess<T> less{};
    SkTQSort(begin, end, less);
}

// Pointer elements must all point to live comparable objects for the whole call.
template <typename T> void SkTQSort(T** begin, T** end) {
    const skia_sort_detail::PointeeLess<T> less{};
    SkTQSort(begin, end, less);
}

#endif
