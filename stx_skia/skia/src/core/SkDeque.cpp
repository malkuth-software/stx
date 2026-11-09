/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/SkAssert.h"
#include "include/private/SkDeque.h"
#include "include/private/SkMalloc.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>

struct SkDeque::Block {
    Block* fNext = nullptr;
    Block* fPrev = nullptr;
    char* fBegin = nullptr;
    char* fEnd = nullptr;
    char* fStop = nullptr;

    char* start() {
        char* bytes = reinterpret_cast<char*>(this);
        char* first = bytes + sizeof(Block);
        return first;
    }
    void init(size_t bytes) {
        fNext = nullptr;
        fPrev = nullptr;
        fBegin = nullptr;
        fEnd = nullptr;
        fStop = reinterpret_cast<char*>(this) + bytes;
    }
};

SkDeque::SkDeque(size_t elemSize, int allocCount)
    : fElemSize(elemSize), fAllocCount(allocCount) {
    SkASSERT_RELEASE(elemSize != 0 && allocCount >= 1);
    const size_t available = std::numeric_limits<size_t>::max() - sizeof(Block);
    SkASSERT_RELEASE(static_cast<size_t>(allocCount) <= available / elemSize);
}

SkDeque::SkDeque(size_t elemSize, void* storage, size_t storageSize, int allocCount)
    : SkDeque(elemSize, allocCount) {
    SkASSERT_RELEASE(storageSize == 0 || storage != nullptr);
    fInitialStorage = storage;
    if (storageSize < sizeof(Block) || storageSize - sizeof(Block) < elemSize) return;
    const uintptr_t address = reinterpret_cast<uintptr_t>(storage);
    SkASSERT_RELEASE(address % alignof(Block) == 0);
    // Both ends use the same slot lattice, even when a borrowed buffer has a
    // partial element at its end. No additional capacity field is required.
    const size_t slots = (storageSize - sizeof(Block)) / elemSize;
    const size_t bytes = sizeof(Block) + slots * elemSize;
    fFrontBlock = new (storage) Block;
    (*fFrontBlock).init(bytes);
    fBackBlock = fFrontBlock;
}

SkDeque::~SkDeque() {
    Block* block = fFrontBlock;
    while (block != nullptr) {
        Block* next = (*block).fNext;
        freeBlock(block);
        block = next;
    }
}

void* SkDeque::push_front() & {
    SkASSERT_RELEASE(fCount < std::numeric_limits<int>::max());
    if (fFrontBlock == nullptr) {
        fFrontBlock = allocateBlock(fAllocCount);
        fBackBlock = fFrontBlock;
    }
    Block* first = fFrontBlock;
    if ((*first).fBegin != nullptr) {
        const size_t available = static_cast<size_t>((*first).fBegin - (*first).start());
        if (available < fElemSize) {
            first = allocateBlock(fAllocCount);
            (*first).fNext = fFrontBlock;
            (*fFrontBlock).fPrev = first;
            fFrontBlock = first;
        }
    }
    char* begin = nullptr;
    if ((*first).fBegin == nullptr) {
        (*first).fEnd = (*first).fStop;
        begin = (*first).fStop - fElemSize;
    } else {
        begin = (*first).fBegin - fElemSize;
    }
    (*first).fBegin = begin;
    if (fFront == nullptr) fBack = begin;
    fFront = begin;
    ++fCount;
    return begin;
}

void* SkDeque::push_back() & {
    SkASSERT_RELEASE(fCount < std::numeric_limits<int>::max());
    if (fBackBlock == nullptr) {
        fBackBlock = allocateBlock(fAllocCount);
        fFrontBlock = fBackBlock;
    }
    Block* last = fBackBlock;
    if ((*last).fBegin != nullptr) {
        const size_t available = static_cast<size_t>((*last).fStop - (*last).fEnd);
        if (available < fElemSize) {
            last = allocateBlock(fAllocCount);
            (*last).fPrev = fBackBlock;
            (*fBackBlock).fNext = last;
            fBackBlock = last;
        }
    }
    char* slot = nullptr;
    if ((*last).fBegin == nullptr) {
        slot = (*last).start();
        (*last).fBegin = slot;
    } else {
        slot = (*last).fEnd;
    }
    (*last).fEnd = slot + fElemSize;
    if (fBack == nullptr) fFront = slot;
    fBack = slot;
    ++fCount;
    return slot;
}

void SkDeque::pop_front() {
    SkASSERT_RELEASE(fCount > 0);
    Block* first = fFrontBlock;
    if ((*first).fBegin == nullptr) {
        first = (*first).fNext;
        SkASSERT(first != nullptr);
        (*first).fPrev = nullptr;
        freeBlock(fFrontBlock);
        fFrontBlock = first;
    }
    char* begin = (*first).fBegin + fElemSize;
    SkASSERT(begin <= (*first).fEnd);
    if (begin < (*first).fEnd) {
        (*first).fBegin = begin;
        fFront = begin;
    } else {
        (*first).fBegin = nullptr;
        (*first).fEnd = nullptr;
        Block* next = (*first).fNext;
        if (fCount == 1) {
            fFront = nullptr;
            fBack = nullptr;
        } else {
            SkASSERT(next != nullptr && (*next).fBegin != nullptr);
            fFront = (*next).fBegin;
        }
    }
    --fCount;
}

void SkDeque::pop_back() {
    SkASSERT_RELEASE(fCount > 0);
    Block* last = fBackBlock;
    if ((*last).fEnd == nullptr) {
        last = (*last).fPrev;
        SkASSERT(last != nullptr);
        (*last).fNext = nullptr;
        freeBlock(fBackBlock);
        fBackBlock = last;
    }
    char* end = (*last).fEnd - fElemSize;
    SkASSERT(end >= (*last).fBegin);
    if (end > (*last).fBegin) {
        (*last).fEnd = end;
        fBack = end - fElemSize;
    } else {
        (*last).fBegin = nullptr;
        (*last).fEnd = nullptr;
        Block* previous = (*last).fPrev;
        if (fCount == 1) {
            fFront = nullptr;
            fBack = nullptr;
        } else {
            SkASSERT(previous != nullptr && (*previous).fEnd != nullptr);
            fBack = (*previous).fEnd - fElemSize;
        }
    }
    --fCount;
}

int SkDeque::numBlocksAllocated() const {
    int count = 0;
    for (const Block* block = fFrontBlock; block != nullptr; block = (*block).fNext) ++count;
    return count;
}

SkDeque::Block* SkDeque::allocateBlock(int allocCount) {
    // The constructor established this product and addition for fAllocCount.
    SkASSERT(allocCount == fAllocCount);
    const size_t payload = static_cast<size_t>(allocCount) * fElemSize;
    const size_t bytes = sizeof(Block) + payload;
    void* storage = sk_malloc_throw(bytes);
    Block* block = new (storage) Block;
    (*block).init(bytes);
    return block;
}

void SkDeque::freeBlock(Block* block) {
    std::destroy_at(block);
    // Detaching a borrowed initial block never transfers release authority.
    if (block != fInitialStorage) sk_free(block);
}

SkDeque::Iter::Iter() = default;
SkDeque::Iter::Iter(const SkDeque& deque, IterStart start) { reset(deque, start); }

void* SkDeque::Iter::next() {
    char* current = fPos;
    if (current == nullptr) return nullptr;
    char* next = current + fElemSize;
    SkASSERT(next <= (*fCurBlock).fEnd);
    if (next == (*fCurBlock).fEnd) {
        do {
            fCurBlock = (*fCurBlock).fNext;
        } while (fCurBlock != nullptr && (*fCurBlock).fBegin == nullptr);
        next = fCurBlock != nullptr ? (*fCurBlock).fBegin : nullptr;
    }
    fPos = next;
    return current;
}

void* SkDeque::Iter::prev() {
    char* current = fPos;
    if (current == nullptr) return nullptr;
    // Check the boundary before subtraction, never form a before-block pointer.
    if (current == (*fCurBlock).fBegin) {
        do {
            fCurBlock = (*fCurBlock).fPrev;
        } while (fCurBlock != nullptr && (*fCurBlock).fEnd == nullptr);
        fPos = fCurBlock != nullptr ? (*fCurBlock).fEnd - fElemSize : nullptr;
    } else {
        fPos = current - fElemSize;
    }
    return current;
}

void SkDeque::Iter::reset(const SkDeque& deque, IterStart start) {
    fElemSize = deque.fElemSize;
    switch (start) {
        case kFront_IterStart:
            fCurBlock = deque.fFrontBlock;
            while (fCurBlock != nullptr && (*fCurBlock).fBegin == nullptr) fCurBlock = (*fCurBlock).fNext;
            fPos = fCurBlock != nullptr ? (*fCurBlock).fBegin : nullptr;
            return;
        case kBack_IterStart:
            fCurBlock = deque.fBackBlock;
            while (fCurBlock != nullptr && (*fCurBlock).fEnd == nullptr) fCurBlock = (*fCurBlock).fPrev;
            fPos = fCurBlock != nullptr ? (*fCurBlock).fEnd - fElemSize : nullptr;
            return;
        default:
            fCurBlock = nullptr;
            fPos = nullptr;
            return;
    }
}
