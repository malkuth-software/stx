/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkArenaAlloc_DEFINED
#define SkArenaAlloc_DEFINED

#include "include/core/SkSpan.h"
#include "include/private/SkASAN.h"
#include "include/private/SkAssert.h"
#include "include/private/SkTFitsIn.h"
#include "include/private/SkTo.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <cstddef>
#include <new>
#include <type_traits>  // IWYU pragma: keep
#include <utility>

// We found allocating strictly doubling amounts of memory from the heap left too
// much unused slop, particularly on Android.  Instead we'll follow a Fibonacci-like
// progression.

// SkFibonacci47 is the first 47 Fibonacci numbers. Fib(47) is the largest value less than 2 ^ 32.
extern std::array<const uint32_t, 47> SkFibonacci47;
template<uint32_t kMaxSize>
class SkFibBlockSizes {
public:
    // staticBlockSize, and firstAllocationSize are parameters describing the initial memory
    // layout. staticBlockSize describes the size of the inlined memory, and firstAllocationSize
    // describes the size of the first block to be allocated if the static block is exhausted. By
    // convention, firstAllocationSize is the first choice for the block unit size followed by
    // staticBlockSize followed by the default of 1024 bytes.
    SkFibBlockSizes(uint32_t staticBlockSize, uint32_t firstAllocationSize) {
        fBlockUnitSize = firstAllocationSize > 0 ? firstAllocationSize :
                         staticBlockSize     > 0 ? staticBlockSize     : 1024;

        SkASSERT_RELEASE(0 < fBlockUnitSize);
        SkASSERT_RELEASE(fBlockUnitSize < std::min(kMaxSize, (1u << 26) - 1));
    }

    uint32_t nextBlockSize() {
        const uint32_t result = SkFibonacci47[fIndex] * fBlockUnitSize;

        if (SkTo<size_t>(fIndex + 1) < SkFibonacci47.size() &&
            SkFibonacci47[fIndex + 1] < kMaxSize / fBlockUnitSize)
        {
            fIndex += 1;
        }

        return result;
    }

private:
    uint32_t fIndex : 6 = 0;
    uint32_t fBlockUnitSize : 26 = 0;
};

// SkArenaAlloc allocates object and destroys the allocated objects when destroyed. It's designed
// to minimize the number of underlying block allocations. SkArenaAlloc allocates first out of an
// (optional) user-provided block of memory, and when that's exhausted it allocates on the heap,
// starting with an allocation of firstHeapAllocation bytes.  If your data (plus a small overhead)
// fits in the user-provided block, SkArenaAlloc never uses the heap, and if it fits in
// firstHeapAllocation bytes, it'll use the heap only once. If 0 is specified for
// firstHeapAllocation, then blockSize is used unless that too is 0, then 1024 is used.
//
// Examples:
//
//   char block[mostCasesSize];
//   SkArenaAlloc arena(block, mostCasesSize);
//
// If mostCasesSize is too large for the stack, you can use the following pattern.
//
//   std::unique_ptr<char[]> block{new char[mostCasesSize]};
//   SkArenaAlloc arena(block.get(), mostCasesSize, almostAllCasesSize);
//
// If the program only sometimes allocates memory, use the following pattern.
//
//   SkArenaAlloc arena(nullptr, 0, almostAllCasesSize);
//
// The storage does not necessarily need to be on the stack. Embedding the storage in a class also
// works.
//
//   class Foo {
//       char storage[mostCasesSize];
//       SkArenaAlloc arena (storage, mostCasesSize);
//   };
//
// In addition, the system is optimized to handle POD data including arrays of PODs (where
// POD is really data with no destructors). For POD data it has zero overhead per item, and a
// typical per block overhead of 8 bytes. For non-POD objects there is a per item overhead of 4
// bytes. For arrays of non-POD objects there is a per array overhead of typically 8 bytes. There
// is an addition overhead when switching from POD data to non-POD data of typically 8 bytes.
//
// If additional blocks are needed they are increased exponentially. This strategy bounds the
// recursion of the RunDtorsOnBlock to be limited to O(log size-of-memory). Block size grow using
// the Fibonacci sequence which means that for 2^32 memory there are 48 allocations, and for 2^48
// there are 71 allocations.
class SkArenaAlloc {
public:
    SkArenaAlloc(char* block, size_t blockSize, size_t firstHeapAllocation);

    explicit SkArenaAlloc(size_t firstHeapAllocation)
        : SkArenaAlloc(nullptr, 0, firstHeapAllocation) {}

    SkArenaAlloc(const SkArenaAlloc&) = delete;
    SkArenaAlloc& operator=(const SkArenaAlloc&) = delete;
    SkArenaAlloc(SkArenaAlloc&&) = delete;
    SkArenaAlloc& operator=(SkArenaAlloc&&) = delete;

    ~SkArenaAlloc();

    // Constructs an object with 'ctor', which is called with the object's storage and returns
    // a pointer to the object it constructed there.
    template <typename Ctor>
    std::invoke_result_t<Ctor&, std::nullptr_t> make(Ctor&& ctor) {
        using T = std::remove_pointer_t<std::invoke_result_t<Ctor&, std::nullptr_t>>;
        if constexpr (std::is_trivially_destructible_v<T>) {
            char* objStart = (*this).reserveObject<T>();
            // This must be last to make objects with nested use of this allocator work.
            T* object = ctor(objStart);
            return object;
        } else {
            char* footerEnd = (*this).reserveObjectWithFooter<T>();
            T* object = ctor(SkipObject<T>(footerEnd));
            setFooterAction(footerEnd, DestroyObject<T>);
            return object;
        }
    }

    template <typename T, typename... Args>
    T* make(Args&&... args) {
        if constexpr (std::is_trivially_destructible_v<T>) {
            char* objStart = (*this).reserveObject<T>();
            T* object = new (objStart) T(std::forward<Args>(args)...);
            return object;
        } else {
            char* footerEnd = (*this).reserveObjectWithFooter<T>();
            T* object = new (SkipObject<T>(footerEnd)) T(std::forward<Args>(args)...);
            setFooterAction(footerEnd, DestroyObject<T>);
            return object;
        }
    }

    template <typename T>
    T* make() {
        if constexpr (std::is_standard_layout_v<T> && std::is_trivial_v<T>) {
            // Just allocate some aligned bytes. This generates smaller code.
            T* object = static_cast<T*>((*this).makeBytesAlignedTo(sizeof(T), alignof(T)));
            return object;
        } else if constexpr (std::is_trivially_destructible_v<T>) {
            // This isn't a POD type, so construct the object properly.
            char* objStart = (*this).reserveObject<T>();
            T* object = new (objStart) T();
            return object;
        } else {
            char* footerEnd = (*this).reserveObjectWithFooter<T>();
            T* object = new (SkipObject<T>(footerEnd)) T();
            setFooterAction(footerEnd, DestroyObject<T>);
            return object;
        }
    }

    template <typename T>
    T* makeArrayDefault(size_t count) {
        T* array = (*this).allocUninitializedArray<T>(count);
        for (size_t i = 0; i < count; i++) {
            // Default initialization: if T is primitive then the value is left uninitialized.
            new (&array[i]) T;
            recordConstructedArrayElement(array, count, i + 1);
        }
        return array;
    }

    template <typename T>
    T* makeArray(size_t count) {
        T* array = (*this).allocUninitializedArray<T>(count);
        for (size_t i = 0; i < count; i++) {
            // Value initialization: if T is primitive then the value is zero-initialized.
            new (&array[i]) T();
            recordConstructedArrayElement(array, count, i + 1);
        }
        return array;
    }

    template <typename T, typename Initializer>
    T* makeInitializedArray(size_t count, Initializer initializer) {
        T* array = (*this).allocUninitializedArray<T>(count);
        for (size_t i = 0; i < count; i++) {
            new (&array[i]) T(initializer(i));
            recordConstructedArrayElement(array, count, i + 1);
        }
        return array;
    }

    template <typename T>
    T* makeArrayCopy(SkSpan<const T> toCopy) {
        T* array = (*this).allocUninitializedArray<T>(toCopy.size());
        if constexpr (std::is_trivially_copyable_v<T>) {
            if (!toCopy.empty()) std::memcpy(array, toCopy.data(), toCopy.size_bytes());
        } else {
            for (size_t i = 0; i < toCopy.size(); ++i) {
                new (&array[i]) T(toCopy[i]);
                recordConstructedArrayElement(array, toCopy.size(), i + 1);
            }
        }
        return array;
    }

    // Only use makeBytesAlignedTo if none of the typed variants are practical to use.
    void* makeBytesAlignedTo(size_t size, size_t align) {
        AssertRelease(SkTFitsIn<uint32_t>(size));
        AssertRelease(SkTFitsIn<uint32_t>(align));
        AssertRelease(align != 0 && (align & (align - 1)) == 0);
        char* objStart = allocObject(SkToU32(size), SkToU32(align));
        fCursor = objStart + size;
        sk_asan_unpoison_memory_region(objStart, size);
        return objStart;
    }

protected:
    using FooterAction = char* (char*);
    struct Footer {
        uint8_t unaligned_action[sizeof(FooterAction*)];
        uint8_t padding;
    };

    char* cursor() { return fCursor; }
    char* end() { return fEnd; }

    // Ends every object made so far, frees every heap block, and starts again as if newly
    // constructed with these arguments, without ending this object itself.
    void restart(char* block, size_t blockSize, size_t firstHeapAllocation);

private:
    static void AssertRelease(bool cond) { if (!cond) { ::abort(); } }

    // A footer records the padding before its object in one byte, so an object or array with a
    // destructor may be aligned to at most 256 bytes (padding of at most 255). Trivially
    // destructible allocations have no footer and no such limit.
    static constexpr size_t kMaxFooterAlignment = 256;

    // Installs the first footer in 'block', or leaves the arena without storage when the block
    // cannot hold one.
    void startAt(char* block, size_t blockSize);
    static uint32_t CheckedArenaSize(size_t size) {
        AssertRelease(SkTFitsIn<uint32_t>(size));
        const uint32_t checked = static_cast<uint32_t>(size);
        return checked;
    }

    template <typename T> static char* SkipObject(char* footerEnd) {
        char* objectStart = footerEnd - (sizeof(T) + sizeof(Footer));
        return objectStart;
    }

    template <typename T> static char* DestroyObject(char* footerEnd) {
        char* objectStart = SkipObject<T>(footerEnd);
        T* object = reinterpret_cast<T*>(objectStart);
        std::destroy_at(object);
        return objectStart;
    }

    static void setFooterAction(char* footerEnd, FooterAction* action) {
        char* actionStorage = footerEnd - sizeof(Footer);
        std::memcpy(actionStorage, &action, sizeof(action));
    }

    template <typename T> static char* DestroyArrayPrefix(char* footerEnd) {
        char* metadata = footerEnd - (sizeof(Footer) + 2 * sizeof(uint32_t));
        uint32_t allocatedCount = 0;
        uint32_t constructedCount = 0;
        std::memcpy(&allocatedCount, metadata, sizeof(allocatedCount));
        std::memcpy(&constructedCount, metadata + sizeof(uint32_t), sizeof(constructedCount));
        AssertRelease(constructedCount <= allocatedCount);
        const size_t arrayBytes = static_cast<size_t>(allocatedCount) * sizeof(T);
        char* arrayStart = metadata - arrayBytes;
        T* array = reinterpret_cast<T*>(arrayStart);
        for (uint32_t index = 0; index < constructedCount; ++index) {
            std::destroy_at(&array[index]);
        }
        return arrayStart;
    }

    template <typename T>
    static void recordConstructedArrayElement(T* array, size_t requested, size_t constructed) {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            AssertRelease(constructed <= requested);
            const size_t arrayBytes = requested * sizeof(T);
            char* metadata = reinterpret_cast<char*>(array) + arrayBytes;
            char* countStorage = metadata + sizeof(uint32_t);
            const uint32_t liveCount = static_cast<uint32_t>(constructed);
            std::memcpy(countStorage, &liveCount, sizeof(liveCount));
        }
    }

    static char* SkipPod(char* footerEnd);
    static void RunDtorsOnBlock(char* footerEnd);
    static char* NextBlock(char* footerEnd);

    template <typename T>
    void installRaw(const T& val) {
        sk_asan_unpoison_memory_region(fCursor, sizeof(val));
        std::memcpy(fCursor, &val, sizeof(val));
        fCursor += sizeof(val);
    }
    void installFooter(FooterAction* releaser, uint32_t padding);

    void ensureSpace(uint32_t size, uint32_t alignment);

    // Reserves aligned storage for a trivially destructible T and returns its start.
    template <typename T>
    char* reserveObject() {
        const uint32_t size = SkToU32(sizeof(T));
        char* objStart = (*this).allocObject(size, SkToU32(alignof(T)));
        fCursor = objStart + size;
        sk_asan_unpoison_memory_region(objStart, size);
        return objStart;
    }

    // Reserves aligned storage for a T with a destructor and installs its footer, whose action
    // only skips the object until setFooterAction makes it destroy the object. Returns the end of
    // the footer; the object starts at SkipObject<T>(footerEnd).
    template <typename T>
    char* reserveObjectWithFooter() {
        static_assert(alignof(T) <= kMaxFooterAlignment,
                      "SkArenaAlloc cannot record the padding of a destructible object aligned "
                      "beyond 256 bytes");
        const uint32_t size = SkToU32(sizeof(T));
        AssertRelease(size <= std::numeric_limits<uint32_t>::max() - sizeof(Footer));
        char* objStart = (*this).allocObjectWithFooter(size + sizeof(Footer), SkToU32(alignof(T)));
        // Can never be UB because max value is alignof(T).
        const uint32_t padding = SkToU32(objStart - fCursor);

        // Advance to end of object to install footer.
        fCursor = objStart + size;
        sk_asan_unpoison_memory_region(objStart, size);
        installFooter(SkipObject<T>, padding);
        char* footerEnd = fDtorCursor;
        return footerEnd;
    }

    char* allocObject(uint32_t size, uint32_t alignment) {
        if (fCursor == nullptr) ensureSpace(size, alignment);
        const uintptr_t mask = alignment - 1;
        uintptr_t alignedOffset = (~reinterpret_cast<uintptr_t>(fCursor) + 1) & mask;
        const uintptr_t totalSize = size + alignedOffset;
        AssertRelease(totalSize >= size);
        if (totalSize > static_cast<uintptr_t>(fEnd - fCursor)) {
            (*this).ensureSpace(size, alignment);
            alignedOffset = (~reinterpret_cast<uintptr_t>(fCursor) + 1) & mask;
        }

        char* object = fCursor + alignedOffset;

        SkASSERT((reinterpret_cast<uintptr_t>(object) & (alignment - 1)) == 0);
        SkASSERT(object + size <= fEnd);

        return object;
    }

    char* allocObjectWithFooter(uint32_t sizeIncludingFooter, uint32_t alignment);

    template <typename T>
    T* allocUninitializedArray(size_t countZ) {
        static_assert(!std::has_virtual_destructor_v<T>,
                      "Can't make an array of objects which have a vtable");
        AssertRelease(SkTFitsIn<uint32_t>(countZ));
        const uint32_t count = SkToU32(countZ);

        char* objStart = nullptr;
        AssertRelease(count <= std::numeric_limits<uint32_t>::max() / sizeof(T));
        const uint32_t arraySize = SkToU32(count * sizeof(T));
        const uint32_t alignment = SkToU32(alignof(T));

        if constexpr (std::is_trivially_destructible_v<T>) {
            objStart = (*this).allocObject(arraySize, alignment);
            fCursor = objStart + arraySize;
            sk_asan_unpoison_memory_region(objStart, arraySize);
        } else {
            static_assert(alignof(T) <= kMaxFooterAlignment,
                          "SkArenaAlloc cannot record the padding of a destructible array aligned "
                          "beyond 256 bytes");
            constexpr uint32_t overhead = sizeof(Footer) + 2 * sizeof(uint32_t);
            AssertRelease(arraySize <= std::numeric_limits<uint32_t>::max() - overhead);
            const uint32_t totalSize = arraySize + overhead;
            objStart = (*this).allocObjectWithFooter(totalSize, alignment);

            // Can never be UB because max value is alignof(T).
            const uint32_t padding = SkToU32(objStart - fCursor);

            // Advance to end of array to install footer.
            fCursor = objStart + arraySize;
            sk_asan_unpoison_memory_region(objStart, arraySize);
            installRaw(SkToU32(count));
            installRaw(uint32_t{0});
            installFooter(DestroyArrayPrefix<T>, padding);
        }

        T* array = reinterpret_cast<T*>(objStart);
        return array;
    }

    char*          fDtorCursor = nullptr;
    char*          fCursor = nullptr;
    char*          fEnd = nullptr;

    SkFibBlockSizes<std::numeric_limits<uint32_t>::max()> fFibonacciProgression;
};

class SkArenaAllocWithReset : public SkArenaAlloc {
public:
    SkArenaAllocWithReset(char* block, size_t blockSize, size_t firstHeapAllocation);

    explicit SkArenaAllocWithReset(size_t firstHeapAllocation)
            : SkArenaAllocWithReset(nullptr, 0, firstHeapAllocation) {}

    // Destroy all allocated objects, free any heap allocations.
    void reset();

    // Returns true if the alloc has never made any objects.
    bool isEmpty();

private:
    char* const    fFirstBlock = nullptr;
    const uint32_t fFirstSize = 0;
    const uint32_t fFirstHeapAllocationSize = 0;
};

// Helper for defining allocators with inline/reserved storage.
// For argument declarations, stick to the base type (SkArenaAlloc).
// Note: Inheriting from the storage first means the storage will outlive the
// SkArenaAlloc, letting ~SkArenaAlloc read it as it calls destructors.
// (This is mostly only relevant for strict tools like MSAN.)
template <size_t InlineStorageSize>
class SkSTArenaAlloc : private std::array<char, InlineStorageSize>, public SkArenaAlloc {
public:
    explicit SkSTArenaAlloc(size_t firstHeapAllocation = InlineStorageSize)
        : SkArenaAlloc{(*this).data(), (*this).size(), firstHeapAllocation} {}

    ~SkSTArenaAlloc() {
        // Be sure to unpoison the memory that is probably on the stack.
        sk_asan_unpoison_memory_region((*this).data(), (*this).size());
    }
};

template <size_t InlineStorageSize>
class SkSTArenaAllocWithReset
        : private std::array<char, InlineStorageSize>, public SkArenaAllocWithReset {
public:
    explicit SkSTArenaAllocWithReset(size_t firstHeapAllocation = InlineStorageSize)
            : SkArenaAllocWithReset{(*this).data(), (*this).size(), firstHeapAllocation} {}

    ~SkSTArenaAllocWithReset() {
        // Be sure to unpoison the memory that is probably on the stack.
        sk_asan_unpoison_memory_region((*this).data(), (*this).size());
    }
};

#endif  // SkArenaAlloc_DEFINED
