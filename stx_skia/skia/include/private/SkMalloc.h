/*
 * Copyright 2017 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkMalloc_DEFINED
#define SkMalloc_DEFINED

#include "include/private/SkAPI.h"

#include <cstddef>
#include <cstring>

// Allocations belong to the caller and must be released with sk_free. Null is
// accepted by free. Zero-byte allocation follows libc and may return null or a
// freeable allocation; it never grants access to any bytes.
SK_API void sk_free(void* storage);
[[noreturn]] SK_API void sk_out_of_memory();

enum SkMallocFlags : unsigned {
    SK_MALLOC_ZERO_INITIALIZE = 1 << 0,
    SK_MALLOC_THROW = 1 << 1
};

// The supported native ports use malloc/calloc, including their fundamental
// alignment. These two flags select zero initialization and fatal failure.
// Other flag bits are ignored, as before. THROW is a historical name: failure
// of a nonzero allocation terminates; this port does not throw C++ exceptions.
SK_API void* sk_malloc_flags(size_t size, unsigned flags);

// Preserve min(old size, new size) bytes; growth is uninitialized. A zero size
// releases the old block and returns null. Nonzero allocation failure terminates.
SK_API void* sk_realloc_throw(void* storage, size_t size);

// Query a live allocator-owned block. requested is its known requested extent.
// The platform query returns at least requested; callers must not pass borrows
// into a block, freed storage or unrelated allocations.
SK_API size_t sk_malloc_size(void* storage, size_t requested);

static inline void* sk_malloc_throw(size_t size) {
    void* storage = sk_malloc_flags(size, SK_MALLOC_THROW);
    return storage;
}
static inline void* sk_calloc_throw(size_t size) {
    void* storage = sk_malloc_flags(size, SK_MALLOC_THROW | SK_MALLOC_ZERO_INITIALIZE);
    return storage;
}
static inline void* sk_calloc_canfail(size_t size) {
    void* storage = sk_malloc_flags(size, SK_MALLOC_ZERO_INITIALIZE);
    return storage;
}
static inline void* sk_malloc_canfail(size_t size) {
    void* storage = sk_malloc_flags(size, 0);
    return storage;
}

// Count products are checked before contacting the allocator. Overflow terminates
// in fatal operations and returns null in the fallible operation.
SK_API void* sk_calloc_throw(size_t count, size_t elementSize);
SK_API void* sk_malloc_throw(size_t count, size_t elementSize);
SK_API void* sk_realloc_throw(void* storage, size_t count, size_t elementSize);
SK_API void* sk_malloc_canfail(size_t count, size_t elementSize);

// The byte operations borrow their ranges only for this call. Null is admitted
// for zero-length ranges. Nonempty ranges must be valid for the requested access.
static inline void sk_bzero(void* storage, size_t size) {
    if (size != 0) std::memset(storage, 0, size);
}

// Source and destination must not overlap for a nonempty copy.
static inline void* sk_careful_memcpy(void* destination, const void* source, size_t size) {
    if (size != 0) std::memcpy(destination, source, size);
    return destination;
}

// Overlap is permitted; the result is as if the source bytes were preserved.
static inline void* sk_careful_memmove(void* destination, const void* source, size_t size) {
    if (size != 0) std::memmove(destination, source, size);
    return destination;
}

static inline int sk_careful_memcmp(const void* first, const void* second, size_t size) {
    if (size == 0) return 0;
    const int comparison = std::memcmp(first, second, size);
    return comparison;
}

#endif
