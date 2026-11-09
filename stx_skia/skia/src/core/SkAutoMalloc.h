/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkAutoMalloc_DEFINED
#define SkAutoMalloc_DEFINED

#include "include/private/SkAlign.h"
#include "include/private/SkAssert.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkNoncopyable.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <utility>

// Sole owner of an uninitialized heap allocation. get/reset return borrows that
// end on replacement, release or destruction. Release transfers the sk_free duty.
// Capacity can exceed the caller's requested extent under the reuse policy.
class [[nodiscard]] SkAutoMalloc : SkNoncopyable {
public:
    explicit SkAutoMalloc(size_t size = 0) : fCapacity(size) {
        if (size != 0) fPtr.reset(sk_malloc_throw(size));
    }

    SkAutoMalloc(SkAutoMalloc&& source) noexcept
        : fPtr(std::move(source.fPtr)), fCapacity(source.fCapacity) {
        source.fCapacity = 0;
    }
    SkAutoMalloc& operator=(SkAutoMalloc&& source) noexcept {
        if (this != &source) {
            fPtr = std::move(source.fPtr);
            fCapacity = source.fCapacity;
            source.fCapacity = 0;
        }
        return *this;
    }

    enum OnShrink {
        kAlloc_OnShrink = 0, // Replace smaller allocations; old contents are discarded.
        kReuse_OnShrink = 1  // Keep existing capacity and contents when it is sufficient.
    };

    // Replacement does not preserve contents. Allocate before releasing the old
    // block; sk_malloc_throw follows the library's terminating failure policy.
    void* reset(size_t size = 0, OnShrink shrink = kAlloc_OnShrink) & {
        SkASSERT_RELEASE(shrink == kAlloc_OnShrink || shrink == kReuse_OnShrink);
        const bool replace = size != fCapacity && (size > fCapacity || shrink == kAlloc_OnShrink);
        if (replace) {
            void* replacement = nullptr;
            if (size != 0) replacement = sk_malloc_throw(size);
            fPtr.reset(replacement);
            fCapacity = size;
        }
        void* storage = fPtr.get();
        return storage;
    }
    void* reset(size_t = 0, OnShrink = kAlloc_OnShrink) && = delete;

    void* get() & {
        void* storage = fPtr.get();
        return storage;
    }
    const void* get() const & {
        const void* storage = fPtr.get();
        return storage;
    }
    void* get() && = delete;
    const void* get() const && = delete;

    void* release() {
        void* storage = fPtr.release();
        fCapacity = 0;
        return storage;
    }

private:
    struct WrapFree {
        void operator()(void* pointer) const { sk_free(pointer); }
    };
    std::unique_ptr<void, WrapFree> fPtr{};
    size_t fCapacity = 0;
};

// Inline storage follows this owner wherever it lives; larger requests use the
// heap. The caller tracks its requested extent, while this class tracks capacity.
// Borrows end on allocation change or destruction; inline storage cannot transfer.
template <size_t kSizeRequested> class [[nodiscard]] SkAutoSMalloc : SkNoncopyable {
public:
    SkAutoSMalloc() { fPtr = fStorage; }
    explicit SkAutoSMalloc(size_t size) : SkAutoSMalloc() { reset(size); }
    SkAutoSMalloc(SkAutoSMalloc&&) = delete;
    SkAutoSMalloc& operator=(SkAutoSMalloc&&) = delete;

    ~SkAutoSMalloc() {
        if (fPtr != fStorage) sk_free(fPtr);
    }

    void* get() & { return fPtr; }
    const void* get() const & { return fPtr; }
    void* get() && = delete;
    const void* get() const && = delete;

    // Reuse preserves contents; replacement discards them. Heap replacement frees
    // first, preserving the original peak-allocation policy. Failure terminates.
    // didChangeAlloc is optional caller storage, disjoint from this managed block.
    void* reset(size_t size, SkAutoMalloc::OnShrink shrink = SkAutoMalloc::kAlloc_OnShrink,
                bool* didChangeAlloc = nullptr) & {
        SkASSERT_RELEASE(shrink == SkAutoMalloc::kAlloc_OnShrink ||
                         shrink == SkAutoMalloc::kReuse_OnShrink);
        const size_t requestedCapacity = size < kSize ? kSize : size;
        const bool replace = requestedCapacity != fCapacity &&
                             (shrink == SkAutoMalloc::kAlloc_OnShrink || requestedCapacity > fCapacity);
        if (didChangeAlloc != nullptr) *didChangeAlloc = replace;
        if (replace) {
            if (fPtr != fStorage) sk_free(fPtr);
            if (requestedCapacity == kSize) fPtr = fStorage;
            else fPtr = sk_malloc_throw(requestedCapacity);
            fCapacity = requestedCapacity;
        }
        SkASSERT(fCapacity >= requestedCapacity && fCapacity >= kSize);
        SkASSERT(fPtr == fStorage || fCapacity > kSize);
        return fPtr;
    }
    void* reset(size_t, SkAutoMalloc::OnShrink = SkAutoMalloc::kAlloc_OnShrink,
                bool* = nullptr) && = delete;

private:
    static_assert(kSizeRequested > 0);
    static_assert(kSizeRequested <= std::numeric_limits<size_t>::max() - 3);
    static constexpr size_t kSizeAlign4 = SkAlign4(kSizeRequested);
#if defined(SK_BUILD_FOR_GOOGLE3)
    static constexpr size_t kMaxBytes = 4 * 1024;
    static constexpr size_t kSize = kSizeRequested > kMaxBytes ? kMaxBytes : kSizeAlign4;
#else
    static constexpr size_t kSize = kSizeAlign4;
#endif

    void* fPtr = nullptr;
    size_t fCapacity = kSize;
    // Uninitialized capacity. The caller initializes every byte it reads.
    alignas(uint32_t) std::byte fStorage[kSize];
};

#endif
