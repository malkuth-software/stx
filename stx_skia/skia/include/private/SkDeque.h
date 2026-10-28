/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkDeque_DEFINED
#define SkDeque_DEFINED

#include "include/private/SkAPI.h"
#include <cstddef>

// Raw, fixed-width element slots in linked blocks. The caller constructs and
// destroys elements; this owner only allocates/releases blocks. Empty end blocks
// are retained until a later pop crosses them, avoiding boundary push/pop churn.
// Returned slots stay valid until their removal or destruction of the deque.
// One execution context owns all mutation; iteration requires no intervening
// mutation. No element alignment beyond pointer alignment is promised for heap
// blocks; element size must preserve the alignment required by the caller.
class SK_API SkDeque {
public:
    explicit SkDeque(size_t elemSize, int allocCount = 1);
    // Borrow storage until destruction, even if its initial block is detached.
    // A usable buffer must be pointer-aligned. Trailing partial slots are unused.
    // An undersized buffer is ignored. Heap growth uses allocCount slots/block.
    SkDeque(size_t elemSize, void* storage, size_t storageSize, int allocCount = 1);
    ~SkDeque();
    SkDeque(const SkDeque&) = delete;
    SkDeque& operator=(const SkDeque&) = delete;
    SkDeque(SkDeque&&) = delete;
    SkDeque& operator=(SkDeque&&) = delete;

    bool empty() const {
        const bool result = fCount == 0;
        return result;
    }
    int count() const { return fCount; }
    size_t elemSize() const { return fElemSize; }
    const void* front() const & { return fFront; }
    const void* back() const & { return fBack; }
    void* front() & { return fFront; }
    void* back() & { return fBack; }
    const void* front() const && = delete;
    const void* back() const && = delete;
    void* front() && = delete;
    void* back() && = delete;

    // Reserve an uninitialized slot. Allocation failure terminates.
    void* push_front() &;
    void* push_back() &;
    void* push_front() && = delete;
    void* push_back() && = delete;
    // Require a nonempty deque and an element already destroyed by the caller.
    void pop_front();
    void pop_back();

private:
    struct Block;

public:
    // Borrow the deque's blocks. Each call returns the current position before
    // advancing in the requested direction. Exhaustion is sticky until reset.
    // The const-deque constructor retains the original mutable-slot interface.
    class Iter {
    public:
        enum IterStart { kFront_IterStart, kBack_IterStart };
        Iter();
        Iter(const SkDeque& deque, IterStart start);
        Iter(const SkDeque&&, IterStart) = delete;
        void* next();
        void* prev();
        void reset(const SkDeque& deque, IterStart start);
        void reset(const SkDeque&&, IterStart) = delete;

    private:
        Block* fCurBlock = nullptr;
        char* fPos = nullptr;
        size_t fElemSize = 0;
    };

    class F2BIter : private Iter {
    public:
        F2BIter() = default;
        explicit F2BIter(const SkDeque& deque) : Iter(deque, kFront_IterStart) {}
        F2BIter(const SkDeque&&) = delete;
        using Iter::next;
        void reset(const SkDeque& deque) { Iter::reset(deque, kFront_IterStart); }
        void reset(const SkDeque&&) = delete;
    };

private:
    friend class DequeUnitTestHelper;
    void* fFront = nullptr;
    void* fBack = nullptr;
    Block* fFrontBlock = nullptr;
    Block* fBackBlock = nullptr;
    size_t fElemSize = 0;
    void* fInitialStorage = nullptr;
    int fCount = 0;
    int fAllocCount = 0;

    Block* allocateBlock(int allocCount);
    void freeBlock(Block* block);
    int numBlocksAllocated() const;
};

#endif
