/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkArenaAlloc.h"

#include "include/private/SkAssert.h"
#include "include/private/SkMalloc.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

static char* end_chain(char*) { return nullptr; }

SkArenaAlloc::SkArenaAlloc(char* block, size_t size, size_t firstHeapAllocation)
    : fFibonacciProgression{CheckedArenaSize(size), CheckedArenaSize(firstHeapAllocation)}
{
    (*this).startAt(block, size);
}

void SkArenaAlloc::startAt(char* block, size_t size) {
    SkASSERT_RELEASE(block != nullptr || size == 0);
    if (size < sizeof(Footer)) {
        fEnd = fCursor = fDtorCursor = nullptr;
        return;
    }
    fDtorCursor = block;
    fCursor = block;
    fEnd = block + size;
    (*this).installFooter(end_chain, 0);
    sk_asan_poison_memory_region(fCursor, fEnd - fCursor);
}

void SkArenaAlloc::restart(char* block, size_t size, size_t firstHeapAllocation) {
    RunDtorsOnBlock(fDtorCursor);
    fFibonacciProgression = SkFibBlockSizes<std::numeric_limits<uint32_t>::max()>(
            CheckedArenaSize(size), CheckedArenaSize(firstHeapAllocation));
    (*this).startAt(block, size);
}

SkArenaAlloc::~SkArenaAlloc() {
    RunDtorsOnBlock(fDtorCursor);
}

void SkArenaAlloc::installFooter(FooterAction* action, uint32_t padding) {
    AssertRelease(SkTFitsIn<uint8_t>(padding));
    (*this).installRaw(action);
    (*this).installRaw(static_cast<uint8_t>(padding));
    fDtorCursor = fCursor;
}

char* SkArenaAlloc::SkipPod(char* footerEnd) {
    char* objEnd = footerEnd - (sizeof(Footer) + sizeof(uint32_t));
    uint32_t skip = 0;
    std::memmove(&skip, objEnd, sizeof(uint32_t));
    return objEnd - static_cast<ptrdiff_t>(skip);
}

void SkArenaAlloc::RunDtorsOnBlock(char* footerEnd) {
    while (footerEnd != nullptr) {
        FooterAction* action = nullptr;
        uint8_t       padding = 0;

        std::memcpy(&action,  footerEnd - sizeof(Footer),  sizeof(action));
        std::memcpy(&padding, footerEnd - sizeof(padding), sizeof(padding));

        footerEnd = action(footerEnd) - static_cast<ptrdiff_t>(padding);
    }
}

char* SkArenaAlloc::NextBlock(char* footerEnd) {
    char* objEnd = footerEnd - (sizeof(char*) + sizeof(Footer));
    char* next = nullptr;
    std::memmove(&next, objEnd, sizeof(char*));
    RunDtorsOnBlock(next);
    sk_free(objEnd);
    return nullptr;
}

void SkArenaAlloc::ensureSpace(uint32_t size, uint32_t alignment) {
    constexpr uint32_t headerSize = sizeof(Footer) + sizeof(ptrdiff_t);
    constexpr uint32_t maxSize = std::numeric_limits<uint32_t>::max();
    constexpr uint32_t overhead = headerSize + sizeof(Footer);
    AssertRelease(size <= maxSize - overhead);
    uint32_t objSizeAndOverhead = size + overhead;

    const uint32_t alignmentOverhead = alignment - 1;
    AssertRelease(objSizeAndOverhead <= maxSize - alignmentOverhead);
    objSizeAndOverhead += alignmentOverhead;

    const uint32_t minAllocationSize = fFibonacciProgression.nextBlockSize();
    uint32_t allocationSize = std::max(objSizeAndOverhead, minAllocationSize);

    // Round up to a nice size. If > 32K align to 4K boundary else up to max_align_t. The > 32K
    // heuristic is from the JEMalloc behavior.
    {
        const uint32_t mask = allocationSize > (1 << 15) ? (1 << 12) - 1 : 16 - 1;
        AssertRelease(allocationSize <= maxSize - mask);
        allocationSize = (allocationSize + mask) & ~mask;
    }

    char* newBlock = static_cast<char*>(sk_malloc_throw(allocationSize));

    char* const previousDtor = fDtorCursor;
    fCursor = newBlock;
    fDtorCursor = newBlock;
    fEnd = fCursor + allocationSize;

    // poison the unused bytes in the block.
    sk_asan_poison_memory_region(fCursor, fEnd - fCursor);

    (*this).installRaw(previousDtor);
    (*this).installFooter(NextBlock, 0);
}

char* SkArenaAlloc::allocObjectWithFooter(uint32_t sizeIncludingFooter, uint32_t alignment) {
    const uintptr_t mask = alignment - 1;

    // Each pass either places the object in the current block or makes a new block and retries.
    for (;;) {
        uint32_t skipOverhead = 0;
        const bool needsSkipFooter = fCursor != fDtorCursor;
        if (needsSkipFooter) {
            skipOverhead = sizeof(Footer) + sizeof(uint32_t);
        }
        AssertRelease(sizeIncludingFooter <= std::numeric_limits<uint32_t>::max() - skipOverhead);
        const uint32_t totalSize = sizeIncludingFooter + skipOverhead;

        // Math on null fCursor/fEnd is undefined behavior, so explicitly check for first alloc.
        if (!fCursor) {
            (*this).ensureSpace(totalSize, alignment);
            continue;
        }

        SkASSERT(fEnd);
        // This test alone would be enough nullptr were defined to be 0, but it's not.
        const uintptr_t cursorAddress = reinterpret_cast<uintptr_t>(fCursor);
        const uintptr_t unalignedAddress = cursorAddress + skipOverhead;
        AssertRelease(unalignedAddress >= cursorAddress);
        const uintptr_t roundedAddress = unalignedAddress + mask;
        AssertRelease(roundedAddress >= unalignedAddress);
        const uintptr_t alignedAddress = roundedAddress & ~mask;
        const uintptr_t alignedOffset = alignedAddress - cursorAddress;
        const size_t availableBytes = static_cast<size_t>(fEnd - fCursor);
        if (alignedOffset > availableBytes || totalSize > availableBytes - alignedOffset) {
            (*this).ensureSpace(totalSize, alignment);
            continue;
        }

        char* objStart = fCursor + alignedOffset;

        // Install a skip footer if needed, thus terminating a run of POD data. The calling code
        // is responsible for installing the footer after the object.
        if (needsSkipFooter) {
            (*this).installRaw(SkToU32(fCursor - fDtorCursor));
            (*this).installFooter(SkipPod, 0);
        }

        return objStart;
    }
}

SkArenaAllocWithReset::SkArenaAllocWithReset(char* block,
                                             size_t size,
                                             size_t firstHeapAllocation)
        : SkArenaAlloc(block, size, firstHeapAllocation)
        , fFirstBlock{block}
        , fFirstSize{SkToU32(size)}
        , fFirstHeapAllocationSize{SkToU32(firstHeapAllocation)} {}

// Reinitializes in place. Ending this object and constructing a new SkArenaAllocWithReset in its
// storage, as the intake code did, is not a transparent replacement when this is a base-class
// subobject (SkSTArenaAllocWithReset, or any other subclass), so the old object could not be
// used afterwards.
void SkArenaAllocWithReset::reset() {
    (*this).restart(fFirstBlock, fFirstSize, fFirstHeapAllocationSize);
}

bool SkArenaAllocWithReset::isEmpty() {
    if (cursor() == nullptr) return true;
    if (fFirstBlock == nullptr) return false;
    return cursor() == fFirstBlock + sizeof(Footer);
}

// SkFibonacci47 is the first 47 Fibonacci numbers. Fib(47) is the largest value less than 2 ^ 32.
// Used by SkFibBlockSizes.
std::array<const uint32_t, 47> SkFibonacci47 {
                1,         1,          2,          3,          5,          8,
               13,        21,         34,         55,         89,        144,
              233,       377,        610,        987,       1597,       2584,
             4181,      6765,      10946,      17711,      28657,      46368,
            75025,    121393,     196418,     317811,     514229,     832040,
          1346269,   2178309,    3524578,    5702887,    9227465,   14930352,
         24157817,  39088169,   63245986,  102334155,  165580141,  267914296,
        433494437, 701408733, 1134903170, 1836311903, 2971215073,
};
