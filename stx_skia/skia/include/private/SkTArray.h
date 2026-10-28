/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkTArray_DEFINED
#define SkTArray_DEFINED

#include "include/core/SkSpan.h"
#include "include/private/SkASAN.h"  // IWYU pragma: keep
#include "include/private/SkAlignedStorage.h"
#include "include/private/SkAssert.h"
#include "include/private/SkAttributes.h"
#include "include/private/SkContainers.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkMath.h"
#include "include/private/SkTo.h"
#include "include/private/SkTypeTraits.h"  // IWYU pragma: keep

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace skia_private {
/** TArray<T> implements a typical, mostly std::vector-like array.
    Each T will be default-initialized on allocation, and ~T will be called on destruction.

    MEM_MOVE controls the behavior when a T needs to be moved (e.g. when the array is resized)
      - true: T will be bit-copied via memcpy.
      - false: T will be moved via move-constructors.
*/
template <typename T, bool MEM_MOVE = sk_is_trivially_relocatable_v<T>> class TArray {
    static_assert(alignof(T) <= alignof(std::max_align_t),
                  "TArray heap storage supports only fundamental alignment");
public:
    using value_type = T;

    /**
     * Creates an empty array with no initial storage
     */
    TArray() : fOwnMemory(true), fCapacity{0} {}

    /**
     * Creates an empty array that will preallocate space for reserveCount elements.
     */
    explicit TArray(int reserveCount) : TArray() { (*this).reserve_exact(reserveCount); }

    /**
     * Copies one array to another. The new array will be heap allocated.
     */
    TArray(const TArray& that) : TArray(that.fData, that.fSize) {}

    TArray(TArray&& that) : TArray() { *this = std::move(that); }

    /**
     * Creates a TArray by copying contents of a standard C array. The new
     * array will be heap allocated. Be careful not to use this constructor
     * when you really want the (void*, int) version.
     */
    // The source is a live, disjoint range; null is admitted only for zero count.
    // Delegation establishes the empty owner before any element constructor runs.
    // If copying throws, the standard algorithm destroys its partial range and
    // this already-constructed owner releases any heap storage.
    TArray(const T* array, int count) : TArray() {
        SkASSERT_RELEASE(count >= 0 && count <= kMaxCapacity);
        SkASSERT_RELEASE(count == 0 || array != nullptr);
        reserve_exact(count);
        copyConstruct(array, count);
    }

    /**
     * Creates a TArray by copying contents from an SkSpan. The new array will be heap allocated.
     */
    TArray(SkSpan<const T> data) : TArray(data.data(), CheckedCount(data.size())) {}

    /**
     * Creates a TArray by copying contents of an initializer list.
     */
    TArray(std::initializer_list<T> data) : TArray(data.begin(), CheckedCount(data.size())) {}

    TArray& operator=(const TArray& that) {
        if (this == &that) {
            return *this;
        }
        clear();
        if (that.fSize > capacity()) checkRealloc(that.fSize, kExactFit);
        copyConstruct(that.fData, that.fSize);
        return *this;
    }

    TArray& operator=(TArray&& that) {
        if (this != &that) {
            (*this).clear();
            if (that.fOwnMemory) {
                // The storage is on the heap, so move the data pointer.
                unpoison();
                that.unpoison();
                if (fOwnMemory) {
                    sk_free(fData);
                }

                fData = std::exchange(that.fData, nullptr);

                // Can't use exchange with bitfields.
                fCapacity = that.fCapacity;
                that.fCapacity = 0;

                fOwnMemory = true;

                (*this).changeSize(that.fSize);
            } else {
                // The data is stored inline in that, so move it element-by-element.
                checkRealloc(that.size(), kExactFit);
                UnpoisonedCapacity construction(*this);
                if (that.fSize != 0) {
                    if constexpr (MEM_MOVE) {
                        sk_careful_memcpy(fData, that.fData, Bytes(that.fSize));
                    } else {
                        std::uninitialized_move_n(that.fData, that.fSize, fData);
                    }
                }
                changeSize(that.fSize);
                construction.release();
                if constexpr (!MEM_MOVE) that.destroyAll();
            }
            that.changeSize(0);
        }
        return *this;
    }

    ~TArray() {
        (*this).destroyAll();
        (*this).unpoison();
        if (fOwnMemory) {
            sk_free(fData);
        }
    }

    /**
     * Resets to size() = n newly constructed T objects and resets any reserve count.
     */
    // Construction failure leaves an empty owner retaining its current capacity.
    // Default initialization intentionally leaves scalar values uninitialized.
    void reset(int count) {
        SkASSERT_RELEASE(count >= 0 && count <= kMaxCapacity);
        clear();
        checkRealloc(count, kExactFit);
        UnpoisonedCapacity construction(*this);
        if (count != 0) std::uninitialized_default_construct_n(fData, count);
        changeSize(count);
        construction.release();
    }

    // Replace from a disjoint source, or preserve a live subrange of this array
    // before ending its elements. Nontrivial aliases need a temporary owner.
    void reset(SkSpan<const T> source) {
        const int count = CheckedCount(source.size());
        const T* elements = source.data();
        SkASSERT_RELEASE(count == 0 || elements != nullptr);
        if (count != 0 && fSize != 0) {
            const uintptr_t beginAddress = reinterpret_cast<uintptr_t>(fData);
            const uintptr_t sourceAddress = reinterpret_cast<uintptr_t>(elements);
            const size_t liveBytes = Bytes(fSize);
            if (sourceAddress >= beginAddress && sourceAddress - beginAddress < liveBytes) {
                const size_t byteOffset = sourceAddress - beginAddress;
                SkASSERT_RELEASE(byteOffset % sizeof(T) == 0);
                const size_t offset = byteOffset / sizeof(T);
                SkASSERT_RELEASE(static_cast<size_t>(count) <= static_cast<size_t>(fSize) - offset);
                if constexpr (std::is_trivially_copyable_v<T>) {
                    // Copy while the source elements are still live and readable.
                    const size_t bytes = Bytes(count);
                    sk_careful_memmove(fData, elements, bytes);
                    changeSize(count);
                } else {
                    TArray snapshot(elements, count);
                    *this = snapshot;
                }
                return;
            }
        }
        clear();
        if (count > capacity()) checkRealloc(count, kExactFit);
        copyConstruct(elements, count);
    }

    /**
     * Ensures there is enough reserved space for at least n elements. This is guaranteed at least
     * until the array size grows above n and subsequently shrinks below n, any version of reset()
     * is called, or reserve() is called again.
     */
    void reserve(int n) {
        SkASSERT_RELEASE(n >= 0 && n <= kMaxCapacity);
        if (n > (*this).size()) {
            (*this).checkRealloc(n - (*this).size(), kGrowing);
        }
    }

    /**
     * Ensures there is enough reserved space for exactly n elements. The same capacity guarantees
     * as above apply.
     */
    void reserve_exact(int n) {
        SkASSERT_RELEASE(n >= 0 && n <= kMaxCapacity);
        if (n > (*this).size()) {
            (*this).checkRealloc(n - (*this).size(), kExactFit);
        }
    }

    void removeShuffle(int n) {
        static_assert(MEM_MOVE || std::is_nothrow_move_constructible_v<T>,
                      "removeShuffle requires a nonthrowing element relocation");
        SkASSERT_RELEASE(n >= 0 && n < size());
        const int newCount = fSize - 1;
        fData[n].~T();
        if (n != newCount) {
            (*this).move(n, newCount);
        }
        (*this).changeSize(newCount);
    }

    // Is the array empty.
    bool empty() const { return fSize == 0; }

    /**
     * Adds one new default-initialized T value and returns it by reference. Note that the reference
     * only remains valid until the next call that adds or removes elements.
     */
    T& push_back() & {
        T* element = push_back_n(1);
        return *element;
    }
    T& push_back() && = delete;

    /**
     * Adds one new T value which is copy-constructed, returning it by reference. As always,
     * the reference only remains valid until the next call that adds or removes elements.
     */
    T& push_back(const T& t) & {
        UnpoisonedCapacity construction(*this);
        T* newT = nullptr;
        if ((*this).capacity() > fSize) SK_LIKELY {
            // Copy over the element directly.
            newT = new (fData + fSize) T(t);
        } else {
            newT = (*this).growAndConstructAtEnd(t);
        }

        changeSize(fSize + 1);
        construction.release();
        return *newT;
    }
    T& push_back(const T&) && = delete;

    /**
     * Adds one new T value which is copy-constructed, returning it by reference.
     */
    T& push_back(T&& t) & {
        UnpoisonedCapacity construction(*this);
        T* newT = nullptr;
        if ((*this).capacity() > fSize) SK_LIKELY {
            // Move over the element directly.
            newT = new (fData + fSize) T(std::move(t));
        } else {
            newT = (*this).growAndConstructAtEnd(std::move(t));
        }

        changeSize(fSize + 1);
        construction.release();
        return *newT;
    }
    T& push_back(T&&) && = delete;

    /**
     *  Constructs a new T at the back of this array, returning it by reference.
     */
    template <typename... Args> T& emplace_back(Args&&... args) & {
        UnpoisonedCapacity construction(*this);
        T* newT = nullptr;
        if ((*this).capacity() > fSize) SK_LIKELY {
            // Emplace the new element in directly.
            newT = new (fData + fSize) T(std::forward<Args>(args)...);
        } else {
            newT = (*this).growAndConstructAtEnd(std::forward<Args>(args)...);
        }

        changeSize(fSize + 1);
        construction.release();
        return *newT;
    }
    template <typename... Args> T& emplace_back(Args&&...) && = delete;

    /**
     * Allocates n more default-initialized T values, and returns the address of
     * the start of that new range. Note: this address is only valid until the
     * next API call made on the array that might add or remove elements.
     */
    // Failure destroys only the new prefix; the original live count is unchanged.
    // Capacity may have grown before element construction begins.
    T* push_back_n(int count) & {
        SkASSERT_RELEASE(count >= 0 && count <= kMaxCapacity - fSize);
        if (count == 0) {
            T* unchangedEnd = end();
            return unchangedEnd;
        }
        checkRealloc(count, kGrowing);
        UnpoisonedCapacity construction(*this);
        T* appended = fData + fSize;
        std::uninitialized_default_construct_n(appended, count);
        const int newSize = fSize + count;
        changeSize(newSize);
        construction.release();
        return appended;
    }
    T* push_back_n(int) && = delete;

    /**
     * Version of above that uses a copy constructor to initialize all n items
     * to the same T.
     */
    T* push_back_n(int n, const T& t) & {
        SkASSERT_RELEASE(n >= 0 && n <= kMaxCapacity - fSize);
        if (n == 0) return end();
        std::optional<T> aliasValue{};
        const T* source = &t;
        if (capacity() - fSize < n && ownsLiveElement(source)) {
            aliasValue.emplace(t);
            source = &*aliasValue;
        }
        checkRealloc(n, kGrowing);
        UnpoisonedCapacity construction(*this);
        T* appended = fData + fSize;
        std::uninitialized_fill_n(appended, n, *source);
        changeSize(fSize + n);
        construction.release();
        return appended;
    }
    T* push_back_n(int, const T&) && = delete;

    /**
     * Version of above that uses a copy constructor to initialize the n items
     * to separate T values.
     */
    T* push_back_n(int n, const T t[]) & {
        SkASSERT_RELEASE(n >= 0 && n <= kMaxCapacity - fSize);
        if (n == 0) return end();
        SkASSERT_RELEASE(t != nullptr);
        std::optional<TArray> aliasValues{};
        const T* source = t;
        if (ownsLiveElement(source)) {
            // A source among the live elements must end with them, whether or not the array
            // grows: past them it would read the elements this call is constructing.
            const size_t byteOffset = reinterpret_cast<uintptr_t>(source) -
                                      reinterpret_cast<uintptr_t>(fData);
            SkASSERT_RELEASE(byteOffset % sizeof(T) == 0);
            const size_t offset = byteOffset / sizeof(T);
            SkASSERT_RELEASE(static_cast<size_t>(n) <= static_cast<size_t>(fSize) - offset);
            if (capacity() - fSize < n) {
                // Growth frees the live elements, so copy them first.
                aliasValues.emplace(source, n);
                source = (*aliasValues).data();
            }
        }
        checkRealloc(n, kGrowing);
        UnpoisonedCapacity construction(*this);
        T* appended = fData + fSize;
        std::uninitialized_copy_n(source, n, appended);
        changeSize(fSize + n);
        construction.release();
        return appended;
    }
    T* push_back_n(int, const T[]) && = delete;

    /**
     * Version of above that uses the move constructor to set n items.
     */
    T* move_back_n(int n, T* t) & {
        SkASSERT_RELEASE(n >= 0 && n <= kMaxCapacity - fSize);
        if (n == 0) return end();
        SkASSERT_RELEASE(t != nullptr && !ownsLiveElement(t));
        checkRealloc(n, kGrowing);
        UnpoisonedCapacity construction(*this);
        T* appended = fData + fSize;
        std::uninitialized_move_n(t, n, appended);
        changeSize(fSize + n);
        construction.release();
        return appended;
    }
    T* move_back_n(int, T*) && = delete;

    /**
     * Removes the last element. Not safe to call when size() == 0.
     */
    void pop_back() {
        sk_collection_not_empty((*this).empty());
        fData[fSize - 1].~T();
        (*this).changeSize(fSize - 1);
    }

    /**
     * Removes the last n elements. Not safe to call when size() < n.
     */
    void pop_back_n(int n) {
        SkASSERT_RELEASE(n >= 0 && n <= size());
        int i = fSize;
        while (i-- > fSize - n) {
            (*this)[i].~T();
        }
        (*this).changeSize(fSize - n);
    }

    /**
     * Pushes or pops from the back to resize. Pushes will be default initialized.
     */
    void resize_back(int newCount) {
        SkASSERT_RELEASE(newCount >= 0 && newCount <= kMaxCapacity);
        if (newCount > (*this).size()) {
            if ((*this).empty()) {
                // When the container is completely empty, grow to exactly the requested size.
                (*this).checkRealloc(newCount, kExactFit);
            }
            (*this).push_back_n(newCount - fSize);
        } else if (newCount < (*this).size()) {
            (*this).pop_back_n(fSize - newCount);
        }
    }

    /** Swaps the contents of this array with that array. Does a pointer swap if possible,
        otherwise copies the T values. */
    void swap(TArray& that) {
        using std::swap;
        if (this == &that) {
            return;
        }
        if (fOwnMemory && that.fOwnMemory) {
            swap(fData, that.fData);
            swap(fSize, that.fSize);

            // Can't use swap because fCapacity is a bit field.
            const uint32_t allocCount = fCapacity;
            fCapacity = that.fCapacity;
            that.fCapacity = allocCount;
#ifdef SK_SANITIZE_ADDRESS
            swap(fPoisoned, that.fPoisoned);
#endif
        } else {
            // This could be more optimal...
            TArray copy(std::move(that));
            that = std::move(*this);
            *this = std::move(copy);
        }
    }

    /**
     * Moves all elements of `that` to the end of this array, leaving `that` empty.
     * This is a no-op if `that` is empty or equal to this array.
     */
    void move_back(TArray& that) {
        if (that.empty() || &that == this) {
            return;
        }
        const int oldSize = fSize;
        const int movedCount = that.fSize;
        checkRealloc(movedCount, kGrowing);
        UnpoisonedCapacity construction(*this);
        T* appended = fData + oldSize;
        if constexpr (MEM_MOVE) {
            sk_careful_memcpy(appended, that.fData, Bytes(movedCount));
        } else {
            std::uninitialized_move_n(that.fData, movedCount, appended);
        }
        changeSize(oldSize + movedCount);
        construction.release();
        if constexpr (!MEM_MOVE) that.destroyAll();
        that.changeSize(0);
    }

    T* begin() & {
        return fData;
    }
    const T* begin() const & {
        return fData;
    }
    T* begin() && = delete;
    const T* begin() const && = delete;

    T* end() & {
        if (fData == nullptr) {
            SkASSERT(fSize == 0);
            return nullptr;
        }
        return fData + fSize;
    }
    const T* end() const & {
        if (fData == nullptr) {
            SkASSERT(fSize == 0);
            return nullptr;
        }
        return fData + fSize;
    }
    T* end() && = delete;
    const T* end() const && = delete;
    T* data() & { return fData; }
    const T* data() const & { return fData; }
    T* data() && = delete;
    const T* data() const && = delete;
    int size() const { return fSize; }
    size_t size_bytes() const { return Bytes(fSize); }
    void resize(size_t count) {
        const int checkedCount = CheckedCount(count);
        resize_back(checkedCount);
    }

    void clear() {
        (*this).destroyAll();
        (*this).changeSize(0);
    }

    void shrink_to_fit() {
        if (!fOwnMemory || fSize == fCapacity) {
            return;
        }
        if (fSize == 0) {
            unpoison();
            sk_free(fData);
            fData = nullptr;
            fCapacity = 0;
        } else {
            SkSpan<std::byte> allocation = Allocate(fSize);
            installDataAndUpdateCapacity(allocation);
        }
    }

    /**
     * Get the i^th element.
     */
    T& operator[] (int i) & {
        return fData[sk_collection_check_bounds(i, (*this).size())];
    }

    const T& operator[] (int i) const & {
        return fData[sk_collection_check_bounds(i, (*this).size())];
    }
    T& operator[](int) && = delete;
    const T& operator[](int) const && = delete;

    T& at(int i) & { return (*this)[i]; }
    const T& at(int i) const & { return (*this)[i]; }
    T& at(int) && = delete;
    const T& at(int) const && = delete;

    /**
     * equivalent to operator[](0)
     */
    T& front() & {
        sk_collection_not_empty((*this).empty());
        return fData[0];
    }

    const T& front() const & {
        sk_collection_not_empty((*this).empty());
        return fData[0];
    }
    T& front() && = delete;
    const T& front() const && = delete;

    /**
     * equivalent to operator[](size() - 1)
     */
    T& back() & {
        sk_collection_not_empty((*this).empty());
        return fData[fSize - 1];
    }

    const T& back() const & {
        sk_collection_not_empty((*this).empty());
        return fData[fSize - 1];
    }
    T& back() && = delete;
    const T& back() const && = delete;

    /**
     * equivalent to operator[](size()-1-i)
     */
    T& fromBack(int i) & {
        SkASSERT_RELEASE(i >= 0 && i < fSize);
        const int forwardIndex = fSize - 1 - i;
        return fData[forwardIndex];
    }

    const T& fromBack(int i) const & {
        SkASSERT_RELEASE(i >= 0 && i < fSize);
        const int forwardIndex = fSize - 1 - i;
        return fData[forwardIndex];
    }
    T& fromBack(int) && = delete;
    const T& fromBack(int) const && = delete;

    bool operator==(const TArray<T, MEM_MOVE>& right) const {
        const int leftCount = (*this).size();
        if (leftCount != right.size()) {
            return false;
        }
        for (int index = 0; index < leftCount; ++index) {
            if (!(fData[index] == right.fData[index])) {
                return false;
            }
        }
        return true;
    }

    bool operator!=(const TArray<T, MEM_MOVE>& right) const {
        return !(*this == right);
    }

    int capacity() const {
        return fCapacity;
    }

protected:
    // The storage base outlives this owner. Capacity is raw storage; no elements
    // become live here. Growth transfers to a heap allocation when necessary.
    template <int InitialCapacity>
    explicit TArray(SkAlignedSTStorage<InitialCapacity, T>* storage) : TArray() {
        static_assert(InitialCapacity > 0);
        SkASSERT_RELEASE(storage != nullptr);
        SkSpan<std::byte> bytes{(*storage).data(), (*storage).size()};
        setDataFromBytes(bytes);
        fOwnMemory = false;
    }

    template <int InitialCapacity>
    TArray(const T* array, int count, SkAlignedSTStorage<InitialCapacity, T>* storage)
        : TArray(storage) {
        SkASSERT_RELEASE(count >= 0 && count <= kMaxCapacity);
        SkASSERT_RELEASE(count == 0 || array != nullptr);
        reserve_exact(count);
        copyConstruct(array, count);
    }
    template <int InitialCapacity>
    TArray(SkSpan<const T> data, SkAlignedSTStorage<InitialCapacity, T>* storage)
        : TArray(data.data(), CheckedCount(data.size()), storage) {}

private:
    // Restores the unused-capacity ASan boundary if element construction exits
    // early. No per-element state or persistent array field is needed.
    class UnpoisonedCapacity {
    public:
        explicit UnpoisonedCapacity(TArray& owner) : fOwner(owner) { fOwner.unpoison(); }
        ~UnpoisonedCapacity() {
            if (fActive) fOwner.poison();
        }
        void release() { fActive = false; }
    private:
        TArray& fOwner;
        bool fActive = true;
    };

    struct FreeAllocation {
        void operator()(std::byte* storage) const { sk_free(storage); }
    };
    struct DestroyNewElement {
        void operator()(T* element) const { std::destroy_at(element); }
    };

    bool ownsLiveElement(const T* element) const {
        if (fSize == 0) return false;
        const uintptr_t beginAddress = reinterpret_cast<uintptr_t>(fData);
        const uintptr_t elementAddress = reinterpret_cast<uintptr_t>(element);
        const bool inside = elementAddress >= beginAddress &&
                            elementAddress - beginAddress < Bytes(fSize);
        return inside;
    }

    // Growth factors for checkRealloc.
    static constexpr double kExactFit = 1.0;
    static constexpr double kGrowing = 1.5;

    static constexpr int kMinHeapAllocCount = 8;
    static_assert(SkIsPow2(kMinHeapAllocCount), "min alloc count not power of two.");

    // Note for 32-bit machines kMaxCapacity will be <= SIZE_MAX. For 64-bit machines it will
    // just be INT_MAX if the sizeof(T) < 2^32.
    static constexpr int kMaxCapacity = SkToInt(std::min(SIZE_MAX / sizeof(T), static_cast<size_t>(INT_MAX)));

    void setDataFromBytes(SkSpan<std::byte> allocation) {
        T* data = TCast(allocation.data());
        // We have gotten extra bytes back from the allocation limit, pin to kMaxCapacity. It
        // would seem like the SkContainerAllocator should handle the divide, but it would have
        // to a full divide instruction. If done here the size is known at compile, and usually
        // can be implemented by a right shift. The full divide takes ~50X longer than the shift.
        const size_t size = std::min(allocation.size() / sizeof(T), SkToSizeT(kMaxCapacity));
        (*this).setData(SkSpan<T>(data, size));
    }

    void setData(SkSpan<T> array) {
        (*this).unpoison();

        fData = array.data();
        fCapacity = SkToU32(array.size());
        fOwnMemory = true;

        (*this).poison();
    }

    void unpoison() {
#ifdef SK_SANITIZE_ADDRESS
        if (fData && fPoisoned) {
            // SkDebugf("UNPOISONING %p : 0 -> %zu\n", fData, Bytes(fCapacity));
            sk_asan_unpoison_memory_region((*this).begin(), Bytes(fCapacity));
            fPoisoned = false;
        }
#endif
    }

    void poison() {
#ifdef SK_SANITIZE_ADDRESS
        if (fData && fCapacity > SkToUInt(fSize)) {
            // SkDebugf("  POISONING %p : %zu -> %zu\n", fData, Bytes(fSize), Bytes(fCapacity));
            sk_asan_poison_memory_region((*this).end(), Bytes(fCapacity - fSize));
            fPoisoned = true;
        }
#endif
    }

    void changeSize(int n) {
        (*this).unpoison();
        fSize = n;
        (*this).poison();
    }

    // We disable Control-Flow Integrity sanitization (go/cfi) when casting item-array buffers.
    // CFI flags this code as dangerous because we are casting `buffer` to a T* while the buffer's
    // contents might still be uninitialized memory. When T has a vtable, this is especially risky
    // because we could hypothetically access a virtual method on fItemArray and jump to an
    // unpredictable location in memory. Of course, TArray won't actually use fItemArray in this
    // way, and we don't want to construct a T before the user requests one. There's no real risk
    // here, so disable CFI when doing these casts.
    SK_NO_SANITIZE_CFI
    static T* TCast(void* buffer) {
        T* elements = static_cast<T*>(buffer);
        return elements;
    }

    static size_t Bytes(int n) {
        SkASSERT(n <= kMaxCapacity);
        return SkToSizeT(n) * sizeof(T);
    }

    static SkSpan<std::byte> Allocate(int capacity, double growthFactor = 1.0) {
        return SkContainerAllocator{sizeof(T), kMaxCapacity}.allocate(capacity, growthFactor);
    }

    void destroyAll() {
        if (!(*this).empty()) {
            T* cursor = (*this).begin();
            T* const end = (*this).end();
            do {
                (*cursor).~T();
                ++cursor;
            } while (cursor < end);
        }
    }

    // The caller has ended prior elements and reserved count slots. The standard
    // algorithm cleans its constructed prefix if a copy throws; fSize stays zero.
    // No count publication or destruction bookkeeping occurs per element here.
    void copyConstruct(const T* source, int count) {
        SkASSERT(fSize == 0 && count >= 0 && count <= capacity());
        if constexpr (std::is_trivially_copyable_v<T>) {
            // No constructor can throw or observe this count. Preserve the
            // original store-before-copy fast path for trivial elements.
            changeSize(count);
            if (count != 0) {
                const size_t bytes = Bytes(count);
                sk_careful_memcpy(fData, source, bytes);
            }
        } else {
            UnpoisonedCapacity construction(*this);
            if (count != 0) std::uninitialized_copy_n(source, count, fData);
            changeSize(count);
            construction.release();
        }
    }

    static int CheckedCount(size_t count) {
        SkASSERT_RELEASE(count <= static_cast<size_t>(kMaxCapacity));
        const int result = static_cast<int>(count);
        return result;
    }

    void move(int dst, int src) {
        if constexpr (MEM_MOVE) {
            std::memcpy(static_cast<void*>(&fData[dst]),
                   static_cast<const void*>(&fData[src]),
                   sizeof(T));
        } else {
            new (&fData[dst]) T(std::move(fData[src]));
            fData[src].~T();
        }
    }

    // A failed move leaves the source live and the standard algorithm destroys
    // the constructed destination prefix. The source may contain moved-from
    // values until the caller retries or clears it.
    void relocateTo(T* destination) {
        if (fSize == 0) return;
        if constexpr (MEM_MOVE) {
            sk_careful_memcpy(destination, fData, Bytes(fSize));
        } else {
            std::uninitialized_move_n(fData, fSize, destination);
        }
    }

    // Only called after relocation succeeds. Bit-relocated source objects must
    // not run their destructors; constructor-relocated source objects must.
    void finishRelocation(SkSpan<std::byte> allocation) {
        if constexpr (!MEM_MOVE) destroyAll();
        unpoison();
        if (fOwnMemory) sk_free(fData);
        setDataFromBytes(allocation);
    }

    template <typename... Args>
    SK_ALWAYS_INLINE T* growAndConstructAtEnd(Args&&... args) {
        SkSpan<std::byte> buffer = preallocateNewData(/*delta=*/1, kGrowing);
        std::unique_ptr<std::byte, FreeAllocation> allocationOwner(buffer.data());
        T* newT = new (TCast(buffer.data()) + fSize) T(std::forward<Args>(args)...);
        std::unique_ptr<T, DestroyNewElement> newElementOwner(newT);
        relocateTo(TCast(buffer.data()));
        finishRelocation(buffer);
        newElementOwner.release();
        allocationOwner.release();
        return newT;
    }

    void checkRealloc(int delta, double growthFactor) {
        SkASSERT(delta >= 0);
        SkASSERT(fSize >= 0);
        SkASSERT(fCapacity >= 0);
        if (capacity() - fSize < delta) {
            SkSpan<std::byte> allocation = preallocateNewData(delta, growthFactor);
            installDataAndUpdateCapacity(allocation);
        }
    }

    SkSpan<std::byte> preallocateNewData(int delta, double growthFactor) {
        SkASSERT(delta >= 0);
        SkASSERT(fSize >= 0);
        SkASSERT(fCapacity >= 0);

        // Don't overflow fSize or size_t later in the memory allocation. Overflowing memory
        // allocation really only applies to fSizes on 32-bit machines; on 64-bit machines this
        // will probably never produce a check. Since kMaxCapacity is bounded above by INT_MAX,
        // this also checks the bounds of fSize.
        if (delta > kMaxCapacity - fSize) {
            sk_report_container_overflow_and_die();
        }
        const int newCount = fSize + delta;

        return Allocate(newCount, growthFactor);
    }

    void installDataAndUpdateCapacity(SkSpan<std::byte> allocation) {
        std::unique_ptr<std::byte, FreeAllocation> allocationOwner(allocation.data());
        relocateTo(TCast(allocation.data()));
        finishRelocation(allocation);
        allocationOwner.release();
        SkASSERT(fData != nullptr);
    }

    T* fData{nullptr};
    int fSize{0};
    uint32_t fOwnMemory : 1 = 1;
    uint32_t fCapacity : 31 = 0;
#ifdef SK_SANITIZE_ADDRESS
    bool fPoisoned = false;
#endif
};

template <typename T, bool M> inline void swap(TArray<T, M>& a, TArray<T, M>& b) {
    a.swap(b);
}

// Subclass of TArray that contains a pre-allocated memory block for the array.
template <int Nreq, typename T, bool MEM_MOVE = sk_is_trivially_relocatable_v<T>>
class STArray : private SkAlignedSTStorage<SkContainerAllocator::RoundUp<T>(Nreq), T>,
                public TArray<T, MEM_MOVE> {
    // We round up the requested array size to the next capacity multiple.
    // This space would likely otherwise go to waste.
    static constexpr int N = SkContainerAllocator::RoundUp<T>(Nreq);
    static_assert(Nreq > 0);
    static_assert(N >= Nreq);

    using Storage = SkAlignedSTStorage<N,T>;

public:
    STArray()
        : Storage{}
        , TArray<T, MEM_MOVE>(this) {}  // Must use () to avoid confusion with initializer_list
                                        // when T=bool because * are convertable to bool.

    STArray(const T* array, int count)
        : Storage{}
        , TArray<T, MEM_MOVE>{array, count, this} {}

    STArray(SkSpan<const T> data)
        : Storage{}
        , TArray<T, MEM_MOVE>{data, this} {}

    STArray(std::initializer_list<T> data)
        : STArray{SkSpan<const T>(data.begin(), data.size())} {}

    explicit STArray(int reserveCount)
        : STArray() { (*this).reserve_exact(reserveCount); }

    STArray(const STArray& that)
        : STArray() { *this = that; }

    explicit STArray(const TArray<T, MEM_MOVE>& that)
        : STArray() { *this = that; }

    STArray(STArray&& that)
        : STArray() { *this = std::move(that); }

    explicit STArray(TArray<T, MEM_MOVE>&& that)
        : STArray() { *this = std::move(that); }

    STArray& operator=(const STArray& that) {
        TArray<T, MEM_MOVE>::operator=(that);
        return *this;
    }

    STArray& operator=(const TArray<T, MEM_MOVE>& that) {
        TArray<T, MEM_MOVE>::operator=(that);
        return *this;
    }

    STArray& operator=(STArray&& that) {
        TArray<T, MEM_MOVE>::operator=(std::move(that));
        return *this;
    }

    STArray& operator=(TArray<T, MEM_MOVE>&& that) {
        TArray<T, MEM_MOVE>::operator=(std::move(that));
        return *this;
    }

    // Force the use of TArray for data() and size().
    using TArray<T, MEM_MOVE>::data;
    using TArray<T, MEM_MOVE>::size;
};
}  // namespace skia_private
#endif  // SkTArray_DEFINED
