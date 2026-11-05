/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkTHash_DEFINED
#define SkTHash_DEFINED

#include "include/core/SkTypes.h"
#include "src/core/SkChecksum.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace skia_private {

// Descending linear probing with backward-shift deletion. Hash zero marks an
// empty slot; stored values have an active lifetime exactly when hash != zero.
// Traits supplies GetKey(value) and Hash(key); keys remain stable while stored.
// Entry/trait/visitor operations must not throw or reenter a mutating table.
// No internal synchronization. Allocation failure follows package allocation policy.
// Grow at 3/4 occupancy and shrink at 1/4; manual resize may create a full table.

inline int HashCapacityForCount(size_t count) {
    constexpr uint32_t maximumCapacity = uint32_t{1} << 30;
    constexpr size_t maximumCount = maximumCapacity / 4 * 3;
    if (count > maximumCount) {
        SK_ABORT("Hash entry count exceeds supported capacity");
    }
    if (count == 0) {
        return 0;
    }
    uint32_t capacity = std::bit_ceil(static_cast<uint32_t>(count));
    if (static_cast<uint64_t>(count) * 4 > static_cast<uint64_t>(capacity) * 3) {
        capacity *= 2;
    }
    const int result = static_cast<int>(capacity);
    return result;
}

template <typename T, typename K, typename Traits = T>
class THashTable {
public:
    THashTable()  = default;
    ~THashTable() = default;

    THashTable(const THashTable& that)
        : fCount(that.fCount), fCapacity(that.fCapacity), fSlots(allocateSlots(that.fCapacity)) {
        for (int index = 0; index < fCapacity; ++index) {
            fSlots[index] = that.fSlots[index];
        }
    }
    THashTable(THashTable&& that) {
        swap(that);
    }

    THashTable& operator=(const THashTable& that) {
        if (this != &that) {
            THashTable replacement(that);
            swap(replacement);
        }
        return *this;
    }

    THashTable& operator=(THashTable&& that) {
        if (this != &that) {
            swap(that);
            that.reset();
        }
        return *this;
    }

    // Destroy entries and release all slot storage. A moved-from table is empty.
    void reset() {
        fSlots.reset();
        fCount = 0;
        fCapacity = 0;
    }

    // How many entries are in the table?
    int count() const { return fCount; }

    // How many slots does the table contain? (Note that unlike an array, hash tables can grow
    // before reaching 100% capacity.)
    int capacity() const { return fCapacity; }

    // Approximately how many bytes of memory do we use beyond sizeof(*this)?
    size_t approxBytesUsed() const {
        const size_t bytes = static_cast<size_t>(fCapacity) * sizeof(Slot);
        return bytes;
    }

    // Exchange two hash tables.
    void swap(THashTable& that) {
        std::swap(fCount, that.fCount);
        std::swap(fCapacity, that.fCapacity);
        std::swap(fSlots, that.fSlots);
    }

    void swap(THashTable&& that) {
        *this = std::move(that);
    }

    // Values may be edited only without changing their key. Any mutation,
    // resize, swap, move or destruction may invalidate borrowed entry addresses
    // and iterators. Visitors run synchronously and may not mutate table structure.

    // Accept a value by value and move it into the table, returning its stored address.
    // If there already is an entry in the table with the same key, we overwrite it.
    T* set(T val) & {
        // Capacity is zero or a positive power of two. This is ceil(3*capacity/4)
        // without overflowing multiplication, including manual capacities 1 and 2.
        const int growthThreshold = fCapacity - (fCapacity >> 2);
        const bool shouldGrow = fCount >= growthThreshold;
        if (shouldGrow) {
            constexpr int maximumCapacity = 1 << 30;
            if (fCapacity >= maximumCapacity) {
                SK_ABORT("Hash growth exceeds supported capacity");
            }
            const int capacity = fCapacity > 0 ? fCapacity * 2 : 4;
            resize(capacity);
        }
        T* result = uncheckedSet(std::move(val));
        return result;
    }
    T* set(T val) && = delete;

    // If there is an entry in the table with this key, return a pointer to it.  If not, null.
    T* find(const K& key) const& {
        if (fCapacity == 0) {
            return nullptr;
        }
        const uint32_t hash = Hash(key);
        int index = static_cast<int>(hash & static_cast<uint32_t>(fCapacity - 1));
        for (int n = 0; n < fCapacity; n++) {
            Slot& s = fSlots[index];
            if (s.empty()) {
                return nullptr;
            }
            if (hash == s.fHash && key == Traits::GetKey(*s)) {
                return &*s;
            }
            index = next(index);
        }
        SkASSERT(fCapacity == fCount);
        return nullptr;
    }

    T* find(const K& key) const&& = delete;

    // If there is an entry in the table with this key, return it.  If not, null.
    // This only works for pointer type T, and cannot be used to find an nullptr entry.
    T findOrNull(const K& key) const {
        static_assert(std::is_pointer_v<T>);
        T* p = find(key);
        if (p != nullptr) {
            return *p;
        }
        return nullptr;
    }

    // If a value with this key exists in the hash table, removes it and returns true.
    // Otherwise, returns false.
    bool removeIfExists(const K& key) {
        if (fCapacity == 0) {
            return false;
        }
        const uint32_t hash = Hash(key);
        int index = static_cast<int>(hash & static_cast<uint32_t>(fCapacity - 1));
        for (int n = 0; n < fCapacity; n++) {
            Slot& s = fSlots[index];
            if (s.empty()) {
                return false;
            }
            if (hash == s.fHash && key == Traits::GetKey(*s)) {
                removeSlot(index);
                if (fCapacity > 4) {
                    const bool shouldShrink = fCount <= (fCapacity >> 2);
                    if (shouldShrink) {
                        resize(fCapacity / 2);
                    }
                }
                return true;
            }
            index = next(index);
        }
        SkASSERT(fCapacity == fCount);
        return false;
    }

    // Removes the value with this key from the hash table. Asserts if it is missing.
    void remove(const K& key) {
        const bool removed = removeIfExists(key);
        SkASSERT(removed);
        (void)removed;
    }

    // Hash tables will automatically resize themselves when set() and remove() are called, but
    // resize() can be called to manually grow capacity before a bulk insertion.
    void resize(int capacity) {
        if (capacity < fCount || capacity < 0 ||
            (capacity != 0 && !std::has_single_bit(static_cast<uint32_t>(capacity)))) {
            SK_ABORT("Hash capacity must be a power of two with room for all entries");
        }
        // Allocate before disturbing existing storage. Transfer values into the
        // replacement, then swap ownership; old moved-from entries die afterward.
        THashTable replacement{};
        replacement.fSlots = allocateSlots(capacity);
        replacement.fCapacity = capacity;
        for (int index = 0; index < fCapacity; ++index) {
            Slot& source = fSlots[index];
            if (source.has_value()) {
                replacement.uncheckedSet(std::move(*source));
            }
        }
        SkASSERT(replacement.fCount == fCount);
        swap(replacement);
    }

    // n is a desired entry count, not a slot count. Never shrink. Zero is a no-op.
    void reserve(int n) {
        if (n < 0) {
            SK_ABORT("Hash reserve count must not be negative");
        }
        const int capacity = HashCapacityForCount(static_cast<size_t>(n));
        if (capacity > fCapacity) {
            resize(capacity);
        }
    }

    // Call fn on every entry in the table.  You may mutate the entries, but be very careful.
    template <typename Fn>  // f(T*)
    void foreach(Fn&& fn) {
        for (int i = 0; i < fCapacity; i++) {
            if (fSlots[i].has_value()) {
                fn(&*fSlots[i]);
            }
        }
    }

    // Call fn on every entry in the table.  You may not mutate anything.
    template <typename Fn>  // f(T) or f(const T&)
    void foreach(Fn&& fn) const {
        for (int i = 0; i < fCapacity; i++) {
            if (fSlots[i].has_value()) {
                fn(*fSlots[i]);
            }
        }
    }

    // A basic iterator-like class which disallows mutation; sufficient for range-based for loops.
    // Intended for use by THashMap and THashSet via begin() and end().
    // Adding or removing elements may invalidate all iterators.
    template <typename SlotVal>
    class Iter {
    public:
        using TTable = THashTable<T, K, Traits>;

        Iter(const TTable* table, int slot) : fTable(table), fSlot(slot) {}

        static Iter MakeBegin(const TTable* table) {
            const int slot = (*table).firstPopulatedSlot();
            const Iter iterator(table, slot);
            return iterator;
        }

        static Iter MakeEnd(const TTable* table) {
            const int slot = (*table).capacity();
            const Iter iterator(table, slot);
            return iterator;
        }

        const SlotVal& operator*() const {
            const SlotVal& value = *(*fTable).slot(fSlot);
            return value;
        }

        bool operator==(const Iter& that) const {
            // Iterators from different tables shouldn't be compared against each other.
            SkASSERT(fTable == that.fTable);
            const bool equal = fSlot == that.fSlot;
            return equal;
        }

        bool operator!=(const Iter& that) const {
            const bool different = !(*this == that);
            return different;
        }

        Iter& operator++() {
            fSlot = (*fTable).nextPopulatedSlot(fSlot);
            return *this;
        }

        Iter operator++(int) {
            Iter old = *this;
            operator++();
            return old;
        }

    protected:
        const TTable* fTable;
        int fSlot;
    };

private:
    // Finds the first non-empty slot for an iterator.
    int firstPopulatedSlot() const {
        for (int i = 0; i < fCapacity; i++) {
            if (fSlots[i].has_value()) {
                return i;
            }
        }
        return fCapacity;
    }

    // Increments an iterator's slot.
    int nextPopulatedSlot(int currentSlot) const {
        for (int i = currentSlot + 1; i < fCapacity; i++) {
            if (fSlots[i].has_value()) {
                return i;
            }
        }
        return fCapacity;
    }

    // Reads from an iterator's slot.
    const T* slot(int i) const {
        SkASSERT(fSlots[i].has_value());
        T* value = &*fSlots[i];
        return value;
    }

    T* uncheckedSet(T&& val) {
        const K& key = Traits::GetKey(val);
        SkASSERT(key == key);
        const uint32_t hash = Hash(key);
        int index = static_cast<int>(hash & static_cast<uint32_t>(fCapacity - 1));
        for (int n = 0; n < fCapacity; n++) {
            Slot& s = fSlots[index];
            if (s.empty()) {
                // New entry.
                s.emplace(std::move(val), hash);
                fCount++;
                return &*s;
            }
            if (hash == s.fHash && key == Traits::GetKey(*s)) {
                // Overwrite previous entry.
                // Note: this triggers extra copies when adding the same value repeatedly.
                s.emplace(std::move(val), hash);
                return &*s;
            }

            index = next(index);
        }
        SkASSERT(false);
        return nullptr;
    }

    void removeSlot(int index) {
        const bool full = fCount == fCapacity;
        --fCount;
        // Normal automatic growth leaves an empty terminator. Only manual resize
        // can make this full; choose that extra lifetime work outside the probe loop.
        if (full) [[unlikely]] {
            closeFullHole(index);
        } else {
            closeHole<false>(index);
        }
    }

    // Keep the manual-full-table recovery out of the ordinary deletion body.
    // Native assembly showed that inlining both loops inflated this hot path.
    SK_NEVER_INLINE void closeFullHole(int index) {
        closeHole<true>(index);
    }

    template <bool FullTable>
    void closeHole(int index) {
        const uint32_t mask = static_cast<uint32_t>(fCapacity - 1);
        if constexpr (FullTable) {
            fSlots[index].reset();
        }
        // Rearrange elements to restore the invariants for linear probing.
        for (;;) {
            Slot& emptySlot = fSlots[index];
            const uint32_t emptyIndex = static_cast<uint32_t>(index);
            uint32_t distanceFromNative = 0;
            uint32_t distanceFromHole = 0;
            // Descending probes move native -> hole -> candidate. A candidate
            // may fill the hole precisely when its wrapped displacement reaches
            // past that hole. Unsigned subtraction and the capacity mask also
            // cover wraparound without three separate interval comparisons.
            do {
                index = next(index);
                Slot& s = fSlots[index];
                if (s.empty()) {
                    // We're done shuffling elements around.  Clear the last empty slot.
                    emptySlot.reset();
                    return;
                }
                const uint32_t candidateIndex = static_cast<uint32_t>(index);
                distanceFromNative = (s.fHash - candidateIndex) & mask;
                distanceFromHole = (emptyIndex - candidateIndex) & mask;
            } while (distanceFromNative < distanceFromHole);
            // Move the element to the empty slot.
            Slot& moveFrom = fSlots[index];
            if constexpr (FullTable) {
                emptySlot.emplace(std::move(*moveFrom), moveFrom.fHash);
                moveFrom.reset();
            } else {
                // This is a logical hole: its value remains alive until replaced.
                // The scan established a distinct, live source. No occupancy or
                // self-assignment dispatch is needed for this relocation.
                *emptySlot = std::move(*moveFrom);
                emptySlot.fHash = moveFrom.fHash;
            }
        }
    }

    int next(int index) const {
        index--;
        if (index < 0) { index += fCapacity; }
        return index;
    }

    static uint32_t Hash(const K& key) {
        const uint32_t hash = static_cast<uint32_t>(Traits::Hash(key));
        const uint32_t nonzero = hash == 0 ? 1 : hash;
        return nonzero;  // We reserve hash 0 to mark empty.
    }

    class Slot {
    public:
        Slot() = default;
        ~Slot() { reset(); }

        Slot(const Slot& that) { *this = that; }
        Slot& operator=(const Slot& that) {
            if (this == &that) {
                return *this;
            }
            if (that.empty()) {
                reset();
                return *this;
            }
            if (has_value()) {
                fVal.fStorage = that.fVal.fStorage;
            } else {
                std::construct_at(&fVal.fStorage, that.fVal.fStorage);
            }
            fHash = that.fHash;
            return *this;
        }

        Slot(Slot&& that) { *this = std::move(that); }
        Slot& operator=(Slot&& that) {
            if (this == &that) {
                return *this;
            }
            if (that.empty()) {
                reset();
                return *this;
            }
            if (has_value()) {
                fVal.fStorage = std::move(that.fVal.fStorage);
            } else {
                std::construct_at(&fVal.fStorage, std::move(that.fVal.fStorage));
            }
            fHash = that.fHash;
            return *this;
        }

        T& operator*() & { return fVal.fStorage; }
        const T& operator*() const& { return fVal.fStorage; }

        Slot& emplace(T&& v, uint32_t h) {
            reset();
            std::construct_at(&fVal.fStorage, std::move(v));
            fHash = h;
            return *this;
        }

        bool has_value() const {
            const bool present = fHash != 0;
            return present;
        }
        bool empty() const {
            const bool absent = !has_value();
            return absent;
        }

        void reset() {
            if (fHash) {
                std::destroy_at(&fVal.fStorage);
                fHash = 0;
            }
        }

        uint32_t fHash = 0;

    private:
        union Storage {
            T fStorage;
            Storage() {}
            ~Storage() {}
        } fVal{};
    };

    static std::unique_ptr<Slot[]> allocateSlots(int capacity) {
        const size_t count = static_cast<size_t>(capacity);
        if (capacity < 0 || count > std::numeric_limits<size_t>::max() / sizeof(Slot)) {
            SK_ABORT("Hash slot allocation extent overflows");
        }
        std::unique_ptr<Slot[]> slots{};
        if (count != 0) {
            // Default-initialize slots: initialize occupancy, not inactive payload bytes.
            slots.reset(new Slot[count]);
        }
        return slots;
    }

    int fCount = 0;
    int fCapacity = 0;
    std::unique_ptr<Slot[]> fSlots{};
};

// Maps keys to values.  A more user-friendly wrapper around THashTable, suitable for most use cases.
// K and V are treated as ordinary copyable C++ types, with no assumed relationship between the two.
template <typename K, typename V, typename HashK = SkGoodHash>
class THashMap {
public:
    // Allow default construction and assignment.
    THashMap() = default;

    THashMap(THashMap<K, V, HashK>&& that) = default;
    THashMap(const THashMap<K, V, HashK>& that) = default;

    THashMap<K, V, HashK>& operator=(THashMap<K, V, HashK>&& that) = default;
    THashMap<K, V, HashK>& operator=(const THashMap<K, V, HashK>& that) = default;

    // Construct with an initializer list of key-value pairs.
    struct Pair : public std::pair<K, V> {
        using std::pair<K, V>::pair;
        static const K& GetKey(const Pair& p) { return p.first; }
        static const K& GetKey(const Pair&&) = delete;
        static uint32_t Hash(const K& key) {
            const HashK hasher{};
            const uint32_t hash = static_cast<uint32_t>(hasher(key));
            return hash;
        }
    };

    THashMap(std::initializer_list<Pair> pairs) {
        const int requested = HashCapacityForCount(pairs.size());
        const int capacity = requested < 4 ? 4 : requested;
        fTable.resize(capacity);
        for (const Pair& p : pairs) {
            fTable.set(p);
        }
    }

    // Clear the map.
    void reset() { fTable.reset(); }

    // How many key/value pairs are in the table?
    int count() const {
        const int count = fTable.count();
        return count;
    }

    // Is empty?
    bool empty() const {
        const bool empty = fTable.count() == 0;
        return empty;
    }

    // Approximately how many bytes of memory do we use beyond sizeof(*this)?
    size_t approxBytesUsed() const {
        const size_t bytes = fTable.approxBytesUsed();
        return bytes;
    }

    // Reserve extra capacity.
    void reserve(int n) { fTable.reserve(n); }

    // Exchange two hash maps.
    void swap(THashMap& that) { fTable.swap(that.fTable); }
    void swap(THashMap&& that) { fTable.swap(std::move(that.fTable)); }

    // Returned borrows expire on mutation, resize, swap, move or destruction.
    // Const find retains the historical mutable-value API; keys must not change.

    // Set key to val in the table, replacing any previous value with the same key.
    // We copy both key and val, and return a pointer to the value copy now in the table.
    V* set(K key, V val) & {
        Pair entry(std::move(key), std::move(val));
        Pair* out = fTable.set(std::move(entry));
        V* value = &(*out).second;
        return value;
    }

    // If there is key/value entry in the table with this key, return a pointer to the value.
    // If not, return null.
    V* find(const K& key) const& {
        Pair* pair = fTable.find(key);
        if (pair != nullptr) {
            V* value = &(*pair).second;
            return value;
        }
        return nullptr;
    }

    V* set(K key, V val) && = delete;
    V* find(const K& key) const&& = delete;

    V& operator[](const K& key) & {
        V* value = find(key);
        if (value == nullptr) {
            value = set(key, V{});
        }
        return *value;
    }

    V& operator[](const K& key) && = delete;

    // Removes the key/value entry in the table with this key. Asserts if the key is not present.
    void remove(const K& key) {
        fTable.remove(key);
    }

    // If the key exists in the table, removes it and returns true. Otherwise, returns false.
    bool removeIfExists(const K& key) {
        const bool removed = fTable.removeIfExists(key);
        return removed;
    }

    // Call fn on every key/value pair in the table.  You may mutate the value but not the key.
    template <typename Fn,  // f(K, V*) or f(const K&, V*)
              std::enable_if_t<std::is_invocable_v<Fn, K, V*>>* = nullptr>
    void foreach(Fn&& fn) {
        MutableVisitor<Fn> visitor{fn};
        fTable.foreach(visitor);
    }

    // Call fn on every key/value pair in the table.  You may not mutate anything.
    template <typename Fn,  // f(K, V), f(const K&, V), f(K, const V&) or f(const K&, const V&).
              std::enable_if_t<std::is_invocable_v<Fn, K, V>>* = nullptr>
    void foreach(Fn&& fn) const {
        ConstVisitor<Fn> visitor{fn};
        fTable.foreach(visitor);
    }

    // Call fn on every key/value pair in the table.  You may not mutate anything.
    template <typename Fn,  // f(Pair), or f(const Pair&)
              std::enable_if_t<std::is_invocable_v<Fn, Pair>>* = nullptr>
    void foreach(Fn&& fn) const {
        fTable.foreach(fn);
    }

    // Iterators borrow read-only key/value pairs from this map.
    using Iter = typename THashTable<Pair, K>::template Iter<std::pair<K, V>>;

    Iter begin() const& {
        const Iter iterator = Iter::MakeBegin(&fTable);
        return iterator;
    }
    Iter begin() const&& = delete;

    Iter end() const& {
        const Iter iterator = Iter::MakeEnd(&fTable);
        return iterator;
    }
    Iter end() const&& = delete;

private:
    template <typename Fn> struct MutableVisitor {
        Fn& function;
        void operator()(Pair* pair) const { function((*pair).first, &(*pair).second); }
    };
    template <typename Fn> struct ConstVisitor {
        Fn& function;
        void operator()(const Pair& pair) const { function(pair.first, pair.second); }
    };
    THashTable<Pair, K> fTable{};
};

// A set of T.  T is treated as an ordinary copyable C++ type.
template <typename T, typename HashT = SkGoodHash>
class THashSet {
public:
    // Allow default construction and assignment.
    THashSet() = default;

    THashSet(THashSet<T, HashT>&& that) = default;
    THashSet(const THashSet<T, HashT>& that) = default;

    THashSet<T, HashT>& operator=(THashSet<T, HashT>&& that) = default;
    THashSet<T, HashT>& operator=(const THashSet<T, HashT>& that) = default;

    // Construct with an initializer list of Ts.
    THashSet(std::initializer_list<T> vals) {
        const int requested = HashCapacityForCount(vals.size());
        const int capacity = requested < 4 ? 4 : requested;
        fTable.resize(capacity);
        for (const T& val : vals) {
            fTable.set(val);
        }
    }

    // Clear the set.
    void reset() { fTable.reset(); }

    // How many items are in the set?
    int count() const {
        const int count = fTable.count();
        return count;
    }

    // Is empty?
    bool empty() const {
        const bool empty = fTable.count() == 0;
        return empty;
    }

    // Approximately how many bytes of memory do we use beyond sizeof(*this)?
    size_t approxBytesUsed() const {
        const size_t bytes = fTable.approxBytesUsed();
        return bytes;
    }

    // Reserve extra capacity.
    void reserve(int n) { fTable.reserve(n); }

    // Exchange two hash sets.
    void swap(THashSet& that) { fTable.swap(that.fTable); }
    void swap(THashSet&& that) { fTable.swap(std::move(that.fTable)); }

    // Copy an item into the set.
    void add(T item) { fTable.set(std::move(item)); }

    // Is this item in the set?
    bool contains(const T& item) const {
        const T* value = find(item);
        const bool present = value != nullptr;
        return present;
    }

    // If an item equal to this is in the set, return a pointer to it, otherwise null.
    // This borrow expires on mutation, resize, swap, move or destruction.
    const T* find(const T& item) const& {
        const T* value = fTable.find(item);
        return value;
    }
    const T* find(const T& item) const&& = delete;

    // Remove the item in the set equal to this.
    void remove(const T& item) {
        SkASSERT(contains(item));
        fTable.remove(item);
    }

    // Call fn on every item in the set.  You may not mutate anything.
    template <typename Fn>  // f(T), f(const T&)
    void foreach (Fn&& fn) const {
        fTable.foreach(fn);
    }

private:
    struct Traits {
        static const T& GetKey(const T& item) { return item; }
        static const T& GetKey(const T&&) = delete;
        static uint32_t Hash(const T& item) {
            const HashT hasher{};
            const uint32_t hash = static_cast<uint32_t>(hasher(item));
            return hash;
        }
    };

public:
    using Iter = typename THashTable<T, T, Traits>::template Iter<T>;

    Iter begin() const& {
        const Iter iterator = Iter::MakeBegin(&fTable);
        return iterator;
    }
    Iter begin() const&& = delete;

    Iter end() const& {
        const Iter iterator = Iter::MakeEnd(&fTable);
        return iterator;
    }
    Iter end() const&& = delete;

private:
    THashTable<T, T, Traits> fTable{};
};

}  // namespace skia_private

#endif  // SkTHash_DEFINED
