/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkTInternalLList_DEFINED
#define SkTInternalLList_DEFINED

#include "include/private/SkAssert.h"
#include "include/private/SkDebug.h"

/**
 * This macro creates the member variables required by the SkTInternalLList class. It should be
 * placed in the private section of any class that will be stored in a double linked list.
 */
#define SK_DECLARE_INTERNAL_LLIST_INTERFACE(ClassName)              \
    friend class SkTInternalLList<ClassName>;                       \
    /* observing membership pointer - for debugging */              \
    SkDEBUGCODE(SkTInternalLList<ClassName>* fList = nullptr;)      \
    ClassName* fPrev = nullptr;                                     \
    ClassName* fNext = nullptr

// Intrusive, non-owning chain. The caller owns nodes and keeps them alive through
// traversal and mutation. Membership uses the nodes' existing links; there is no
// allocation, reference counting or synchronization. Mutation requires exclusive
// access. Insertion requires a detached node, and removal requires membership.
// Copying is forbidden; concat transfers links without moving or copying nodes.
template <class T> class SkTInternalLList {
public:
    SkTInternalLList() = default;

    // Abandon the chain in constant time; do not destroy or unlink its nodes.
    // Use only when discarding/reinitializing the entire node storage. To reuse
    // individual nodes, remove them before reset so their links are detached.
    void reset() {
        fHead = nullptr;
        fTail = nullptr;
    }

    void remove(T* entry) {
        SkASSERT(fHead && fTail);
        SkASSERT(isInList(entry));

        T* prev = (*entry).fPrev;
        T* next = (*entry).fNext;

        if (prev) {
            (*prev).fNext = next;
        } else {
            fHead = next;
        }
        if (next) {
            (*next).fPrev = prev;
        } else {
            fTail = prev;
        }

        (*entry).fPrev = nullptr;
        (*entry).fNext = nullptr;

#ifdef SK_DEBUG
        (*entry).fList = nullptr;
#endif
    }

    void addToHead(T* entry) {
        SkASSERT(nullptr == (*entry).fPrev && nullptr == (*entry).fNext);
        SkASSERT(nullptr == (*entry).fList);

        (*entry).fPrev = nullptr;
        (*entry).fNext = fHead;
        if (fHead) {
            (*fHead).fPrev = entry;
        }
        fHead = entry;
        if (nullptr == fTail) {
            fTail = entry;
        }

#ifdef SK_DEBUG
        (*entry).fList = this;
#endif
    }

    void addToTail(T* entry) {
        SkASSERT(nullptr == (*entry).fPrev && nullptr == (*entry).fNext);
        SkASSERT(nullptr == (*entry).fList);

        (*entry).fPrev = fTail;
        (*entry).fNext = nullptr;
        if (fTail) {
            (*fTail).fNext = entry;
        }
        fTail = entry;
        if (nullptr == fHead) {
            fHead = entry;
        }

#ifdef SK_DEBUG
        (*entry).fList = this;
#endif
    }

    /**
     * Inserts a new list entry before an existing list entry. The new entry must not already be
     * a member of this or any other list. If existingEntry is NULL then the new entry is added
     * at the tail.
     */
    void addBefore(T* newEntry, T* existingEntry) {
        SkASSERT(newEntry);
        SkASSERT((*newEntry).fPrev == nullptr && (*newEntry).fNext == nullptr);
        SkASSERT((*newEntry).fList == nullptr);

        if (nullptr == existingEntry) {
            addToTail(newEntry);
            return;
        }

        SkASSERT(isInList(existingEntry));
        (*newEntry).fNext = existingEntry;
        T* prev = (*existingEntry).fPrev;
        (*existingEntry).fPrev = newEntry;
        (*newEntry).fPrev = prev;
        if (nullptr == prev) {
            SkASSERT(fHead == existingEntry);
            fHead = newEntry;
        } else {
            (*prev).fNext = newEntry;
        }
#ifdef SK_DEBUG
        (*newEntry).fList = this;
#endif
    }

    /**
     * Inserts a new list entry after an existing list entry. The new entry must not already be
     * a member of this or any other list. If existingEntry is NULL then the new entry is added
     * at the head.
     */
    void addAfter(T* newEntry, T* existingEntry) {
        SkASSERT(newEntry);
        SkASSERT((*newEntry).fPrev == nullptr && (*newEntry).fNext == nullptr);
        SkASSERT((*newEntry).fList == nullptr);

        if (nullptr == existingEntry) {
            addToHead(newEntry);
            return;
        }

        SkASSERT(isInList(existingEntry));
        (*newEntry).fPrev = existingEntry;
        T* next = (*existingEntry).fNext;
        (*existingEntry).fNext = newEntry;
        (*newEntry).fNext = next;
        if (nullptr == next) {
            SkASSERT(fTail == existingEntry);
            fTail = newEntry;
        } else {
            (*next).fPrev = newEntry;
        }
#ifdef SK_DEBUG
        (*newEntry).fList = this;
#endif
    }

    void concat(SkTInternalLList&& list) {
        SkASSERT(this != &list);
        if (list.isEmpty()) {
            return;
        }

        (*list.fHead).fPrev = fTail;
        if (!fHead) {
            SkASSERT(!(*list.fHead).fPrev);
            fHead = list.fHead;
        } else {
            SkASSERT(fTail);
            (*fTail).fNext = list.fHead;
        }
        fTail = list.fTail;

#ifdef SK_DEBUG
        for (T* node = list.fHead; node; node = (*node).fNext) {
            SkASSERT((*node).fList == &list);
            (*node).fList = this;
        }
#endif

        list.fHead = nullptr;
        list.fTail = nullptr;
    }

    bool isEmpty() const {
        SkASSERT((fHead == nullptr) == (fTail == nullptr));
        const bool empty = fHead == nullptr;
        return empty;
    }

    T* head() const { return fHead; }
    T* tail() const { return fTail; }

    // The iterator observes one live node. Advance before removing its current
    // node; removal clears that node's links. Other structural mutation can change
    // the remaining traversal. An exhausted iterator stays exhausted.
    class Iter {
    public:
        enum IterStart : int {
            kHead_IterStart,
            kTail_IterStart
        };

        Iter() = default;
        Iter(const Iter& iter) : fCurr(iter.fCurr) {}
        Iter& operator=(const Iter& iter) {
            fCurr = iter.fCurr;
            return *this;
        }

        T* init(const SkTInternalLList& list, IterStart startLoc) {
            switch (startLoc) {
                case kHead_IterStart:
                    fCurr = list.fHead;
                    break;
                case kTail_IterStart:
                    fCurr = list.fTail;
                    break;
                default:
                    fCurr = nullptr;
                    break;
            }

            return fCurr;
        }
        T* init(const SkTInternalLList&&, IterStart) = delete;

        T* get() { return fCurr; }

        /**
         * Return the next/previous element in the list or NULL if at the end.
         */
        T* next() {
            if (nullptr == fCurr) {
                return nullptr;
            }

            fCurr = (*fCurr).fNext;
            return fCurr;
        }

        T* prev() {
            if (nullptr == fCurr) {
                return nullptr;
            }

            fCurr = (*fCurr).fPrev;
            return fCurr;
        }

        /**
         * C++11 range-for interface.
         */
        bool operator!=(const Iter& that) const {
            const bool different = fCurr != that.fCurr;
            return different;
        }
        T* operator*() { return fCurr; }
        void operator++() { next(); }

    private:
        T* fCurr = nullptr;
    };

    Iter begin() const& {
        Iter iter{};
        iter.init(*this, Iter::kHead_IterStart);
        return iter;
    }

    Iter begin() const&& = delete;

    Iter end() const& {
        const Iter iterator{};
        return iterator;
    }
    Iter end() const&& = delete;

#ifdef SK_DEBUG
    void validate() const {
        SkASSERT(!fHead == !fTail);
        Iter iter{};
        for (T* item = iter.init(*this, Iter::kHead_IterStart); item; item = iter.next()) {
            SkASSERT(isInList(item));
            if (nullptr == (*item).fPrev) {
                SkASSERT(fHead == item);
            } else {
                SkASSERT((*(*item).fPrev).fNext == item);
            }
            if (nullptr == (*item).fNext) {
                SkASSERT(fTail == item);
            } else {
                SkASSERT((*(*item).fNext).fPrev == item);
            }
        }
    }

    /**
     * Debugging-only method that uses the list back pointer to check if 'entry' is indeed in 'this'
     * list.
     */
    bool isInList(const T* entry) const {
        const bool belongs = (*entry).fList == this;
        return belongs;
    }

    /**
     * Debugging-only method that laboriously counts the list entries.
     */
    int countEntries() const {
        int count = 0;
        for (T* entry = fHead; entry; entry = (*entry).fNext) {
            ++count;
        }
        return count;
    }
#endif // SK_DEBUG

private:
    T* fHead = nullptr;
    T* fTail = nullptr;

    SkTInternalLList(const SkTInternalLList&) = delete;
    SkTInternalLList& operator=(const SkTInternalLList&) = delete;
};

#endif
