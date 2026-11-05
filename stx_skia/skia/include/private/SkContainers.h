// Copyright 2022 Google LLC
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SkContainers_DEFINED
#define SkContainers_DEFINED

#include "include/core/SkSpan.h"
#include "include/private/SkAPI.h"
#include "include/private/SkAlign.h"
#include "include/private/SkAssert.h"

#include <cstddef>
#include <cstdint>
#include <limits>

class SK_SPI SkContainerAllocator {
public:
    SkContainerAllocator(size_t sizeOfT, int maxCapacity)
        : fSizeOfT(sizeOfT), fMaxCapacity(maxCapacity) {
        SkASSERT_RELEASE(sizeOfT != 0 && maxCapacity >= 0);
        SkASSERT_RELEASE(static_cast<size_t>(maxCapacity) <= std::numeric_limits<size_t>::max() / sizeOfT);
    }

    // Transfer responsibility for an allocation to the caller, represented by its
    // pointer and usable byte extent. Release with sk_free. Zero capacity is empty.
    // Failure terminates. growthFactor is finite and at least one.
    SkSpan<std::byte> allocate(int capacity, double growthFactor = 1.0);

    // Preserve the original byte-based rounding used for inline array capacity.
    // The separately rounded runtime growth policy below operates in elements.
    template <typename T> static constexpr size_t RoundUp(size_t capacity) {
        constexpr size_t alignment = static_cast<size_t>(kCapacityMultiple);
        constexpr size_t maximum = (std::numeric_limits<size_t>::max() - (alignment - 1)) / sizeof(T);
        SkASSERT_RELEASE(capacity <= maximum);
        const size_t bytes = capacity * sizeof(T);
        const size_t alignedBytes = SkAlignTo(bytes, alignment);
        const size_t elements = alignedBytes / sizeof(T);
        return elements;
    }

private:
    friend struct SkContainerAllocatorTestingPeer;
    static constexpr int64_t kCapacityMultiple = 8;
    size_t roundUpCapacity(int64_t capacity) const;
    size_t growthFactorCapacity(int capacity, double growthFactor) const;
    const size_t fSizeOfT;
    const int64_t fMaxCapacity;
};

// Transfer responsibility for the returned allocation to the caller; release
// data() with sk_free. Failure returns an empty span. This fallible entry retains
// its original minimum-sized allocation behavior even for a zero request.
SkSpan<std::byte> sk_allocate_canfail(size_t size);

// A zero request is empty. Nonzero allocation failure terminates.
SkSpan<std::byte> sk_allocate_throw(size_t size);

[[noreturn]] SK_SPI void sk_report_container_overflow_and_die();
#endif
