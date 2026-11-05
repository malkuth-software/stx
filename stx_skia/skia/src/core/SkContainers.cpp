// Copyright 2019 Google LLC
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "include/private/SkContainers.h"
#include "include/private/SkAlign.h"
#include "include/private/SkAssert.h"
#include "include/private/SkMalloc.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {
constexpr size_t kMinBytes = alignof(std::max_align_t);

SkSpan<std::byte> complete_size(void* storage, size_t requested) {
    if (storage == nullptr) return {};
    const size_t usable = sk_malloc_size(storage, requested);
    SkSpan<std::byte> allocation{static_cast<std::byte*>(storage), usable};
    return allocation;
}
}

SkSpan<std::byte> SkContainerAllocator::allocate(int capacity, double growthFactor) {
    SkASSERT_RELEASE(capacity >= 0 && capacity <= fMaxCapacity);
    SkASSERT_RELEASE(std::isfinite(growthFactor) && growthFactor >= 1.0);
    size_t chosenCapacity = static_cast<size_t>(capacity);
    if (growthFactor > 1.0 && capacity > 0) {
        chosenCapacity = growthFactorCapacity(capacity, growthFactor);
    }
    // Construction established that every admitted capacity fits this product.
    const size_t bytes = chosenCapacity * fSizeOfT;
    SkSpan<std::byte> allocation = sk_allocate_throw(bytes);
    return allocation;
}

size_t SkContainerAllocator::roundUpCapacity(int64_t capacity) const {
    SkASSERT(capacity >= 0);
    if (capacity < fMaxCapacity - kCapacityMultiple) {
        const int64_t rounded = SkAlignTo(capacity, kCapacityMultiple);
        const size_t result = static_cast<size_t>(rounded);
        return result;
    }
    const size_t maximum = static_cast<size_t>(fMaxCapacity);
    return maximum;
}

size_t SkContainerAllocator::growthFactorCapacity(int capacity, double growthFactor) const {
    SkASSERT(capacity >= 0 && std::isfinite(growthFactor) && growthFactor >= 1.0);
    const double grown = static_cast<double>(capacity) * growthFactor;
    // Clamp before conversion. A finite factor may produce an infinite product;
    // both that case and an ordinary overshoot saturate at the configured maximum.
    if (grown >= static_cast<double>(fMaxCapacity)) {
        const size_t maximum = static_cast<size_t>(fMaxCapacity);
        return maximum;
    }
    const int64_t truncated = static_cast<int64_t>(grown);
    const size_t rounded = roundUpCapacity(truncated);
    return rounded;
}

SkSpan<std::byte> sk_allocate_canfail(size_t size) {
    const size_t adjustedSize = std::max(size, kMinBytes);
    void* storage = sk_malloc_canfail(adjustedSize);
    SkSpan<std::byte> allocation = complete_size(storage, adjustedSize);
    return allocation;
}

SkSpan<std::byte> sk_allocate_throw(size_t size) {
    if (size == 0) return {};
    const size_t adjustedSize = std::max(size, kMinBytes);
    void* storage = sk_malloc_throw(adjustedSize);
    SkSpan<std::byte> allocation = complete_size(storage, adjustedSize);
    return allocation;
}

void sk_report_container_overflow_and_die() {
    SK_ABORT("Requested capacity is too large.");
}
