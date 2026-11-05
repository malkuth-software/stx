// Copyright 2022 Google LLC
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SkAlignedStorage_DEFINED
#define SkAlignedStorage_DEFINED

#include <cstddef>
#include <limits>

// Fixed raw storage, aligned for N objects of T. No T is constructed or destroyed
// here. The borrowing container owns element lifetimes and must end them before
// this storage ends. Moving or copying the storage would invalidate those objects.
// Bytes are intentionally uninitialized; callers initialize each region they read.
template <int N, typename T> class SkAlignedSTStorage {
    static_assert(N > 0);
    static_assert(static_cast<size_t>(N) <= std::numeric_limits<size_t>::max() / sizeof(T));
    static constexpr size_t kBytes = static_cast<size_t>(N) * sizeof(T);

public:
    SkAlignedSTStorage() {}
    SkAlignedSTStorage(SkAlignedSTStorage&&) = delete;
    SkAlignedSTStorage(const SkAlignedSTStorage&) = delete;
    SkAlignedSTStorage& operator=(SkAlignedSTStorage&&) = delete;
    SkAlignedSTStorage& operator=(const SkAlignedSTStorage&) = delete;

    // Borrow only from an existing owner. get() exposes raw construction storage;
    // data() exposes the same byte extent. Neither establishes live T objects.
    void* get() & { return fStorage; }
    const void* get() const & { return fStorage; }
    void* get() && = delete;
    const void* get() const && = delete;

    std::byte* data() & { return fStorage; }
    const std::byte* data() const & { return fStorage; }
    std::byte* data() && = delete;
    const std::byte* data() const && = delete;
    constexpr size_t size() const { return kBytes; }

private:
    alignas(T) std::byte fStorage[kBytes];
};

#endif  // SkAlignedStorage_DEFINED
