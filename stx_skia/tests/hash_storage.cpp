#include "src/core/SkTHash.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace {
struct Entry {
    int key = 0;
    int value = 0;
    static int GetKey(const Entry& entry) { return entry.key; }
    static uint32_t Hash(int key) { return static_cast<uint32_t>(key); }
};
struct CollidingEntry : Entry {
    static uint32_t Hash(int) { return 3; }
};

template <typename Item>
bool full_tables() {
    for (const int capacity : std::array<int, 5>{1, 2, 4, 8, 16}) {
        for (int victim = 0; victim < capacity; ++victim) {
            skia_private::THashTable<Item, int> table{};
            for (int key = 0; key < capacity; ++key) {
                Item entry{};
                entry.key = key;
                entry.value = key * 7;
                table.set(entry);
            }
            table.resize(capacity);
            if (table.count() != capacity || table.capacity() != capacity) return false;
            if (!table.removeIfExists(victim) || table.find(victim) != nullptr) return false;
            for (int key = 0; key < capacity; ++key) {
                if (key == victim) continue;
                const Item* found = table.find(key);
                if (found == nullptr || (*found).value != key * 7) return false;
            }
            for (int key = capacity - 1; key >= 0; --key) {
                if (key == victim) continue;
                if (!table.removeIfExists(key)) return false;
            }
            if (table.count() != 0 || table.find(100) != nullptr) return false;
        }
    }
    return true;
}

bool ordinary() {
    skia_private::THashTable<Entry, int> table{};
    for (int key = 0; key < 100; ++key) table.set(Entry{key, key * 3});
    for (int key = 0; key < 100; key += 2) table.remove(key);
    for (int key = 0; key < 100; ++key) {
        const Entry* found = table.find(key);
        if (key % 2 == 0) {
            if (found != nullptr) return false;
        } else if (found == nullptr || (*found).value != key * 3) return false;
    }
    return true;
}
struct ZeroHash { uint32_t operator()(int) const { return 0; } };
using Map = skia_private::THashMap<int, int, ZeroHash>;
struct MutableVisit { void operator()(int key, int* value) const { *value = key * 5; } };
struct ConstVisit {
    int& sum;
    void operator()(int, int value) const { sum += value; }
};
struct PairVisit {
    int& sum;
    void operator()(const Map::Pair& pair) const { sum += pair.second; }
};
struct SetVisit { int& sum; void operator()(int value) const { sum += value; } };
#ifndef STX_SKIA_REFERENCE
template <typename Table> concept TemporaryFind = requires { Table{}.find(1); };
template <typename Table> concept TemporaryBegin = requires { Table{}.begin(); };
static_assert(!TemporaryFind<Map>);
static_assert(!TemporaryBegin<Map>);
static_assert(!TemporaryFind<skia_private::THashSet<int>>);
static_assert(!TemporaryFind<skia_private::THashTable<Entry, int>>);
#endif

bool maps_and_sets() {
    Map map{{1, 11}, {2, 22}, {3, 33}, {4, 44}};
    MutableVisit edit{};
    map.foreach(edit);
    int sum = 0;
    ConstVisit observe{sum};
    const Map& view = map;
    view.foreach(observe);
    if (sum != 50) return false;
    PairVisit pairs{sum};
    view.foreach(pairs);
    if (sum != 100) return false;
    for (const std::pair<int, int>& pair : view) sum += pair.second;
    if (sum != 150) return false;
    Map::Iter current = map.begin();
    const Map::Iter old = current++;
    if ((*old).first == (*current).first) return false;
    map[9] = 45;
    if (map[10] != 0 || map.count() != 6) return false;
    Map copy(map);
    copy.remove(1);
    if (!map.find(1) || copy.find(1)) return false;
    Map moved(std::move(copy));
    if (!copy.empty() || moved.count() != 5) return false;
    map.swap(moved);
    if (map.count() != 5 || moved.count() != 6) return false;
    map = moved;
    if (map.count() != 6) return false;
    map = std::move(moved);
    if (!moved.empty() || map.count() != 6) return false;
    map.reset();
    if (!map.empty() || map.approxBytesUsed() != 0) return false;
    skia_private::THashSet<int, ZeroHash> set{1, 2, 3, 3, 4};
    if (set.count() != 4 || !set.contains(3) || set.contains(9)) return false;
    set.remove(3);
    sum = 0;
    SetVisit elements{sum};
    set.foreach(elements);
    for (const int value : set) sum += value;
    if (sum != 14) return false;
    skia_private::THashSet<int, ZeroHash> setCopy(set);
    setCopy.add(9);
    if (set.contains(9) || !setCopy.contains(9)) return false;
    set.swap(std::move(setCopy));
    if (!setCopy.empty() || !set.contains(9)) return false;
    return true;
}

struct Counts { int live = 0; int created = 0; int destroyed = 0; };
struct Payload {
    Payload(int value, Counts& counts) : value(value), counts(counts) { ++counts.live; ++counts.created; }
    ~Payload() { --counts.live; ++counts.destroyed; }
    int value;
    Counts& counts;
};
struct OwnedEntry {
    OwnedEntry(int key, Counts& counts) : key(key), payload(std::make_unique<Payload>(key * 2, counts)) {}
    OwnedEntry(const OwnedEntry& other) : key(other.key),
        payload(std::make_unique<Payload>((*other.payload).value, (*other.payload).counts)) {}
    OwnedEntry(OwnedEntry&& other) noexcept : key(other.key), payload(std::move(other.payload)) {}
    OwnedEntry& operator=(const OwnedEntry& other) {
        if (this != &other) {
            std::unique_ptr<Payload> replacement = std::make_unique<Payload>(
                (*other.payload).value, (*other.payload).counts);
            key = other.key;
            payload = std::move(replacement);
        }
        return *this;
    }
    OwnedEntry& operator=(OwnedEntry&& other) noexcept {
        if (this != &other) { key = other.key; payload = std::move(other.payload); }
        return *this;
    }
    static int GetKey(const OwnedEntry& entry) { return entry.key; }
    static uint32_t Hash(int key) { return static_cast<uint32_t>(key % 3); }
    int key;
    std::unique_ptr<Payload> payload;
};
bool lifetimes() {
    Counts counts{};
    {
        skia_private::THashTable<OwnedEntry, int> table{};
        for (int key = 0; key < 64; ++key) table.set(OwnedEntry(key, counts));
        if (counts.live != 64) return false;
        table.reserve(512);
        if (counts.live != 64) return false;
        skia_private::THashTable<OwnedEntry, int> copied(table);
        if (counts.live != 128) return false;
        for (int key = 0; key < 64; key += 2) table.remove(key);
        if (counts.live != 96) return false;
        for (int key = 1; key < 64; key += 2) table.set(OwnedEntry(key, counts));
        if (counts.live != 96) return false;
        copied = table;
        if (counts.live != 64) return false;
        table = std::move(copied);
        if (counts.live != 32 || copied.count() != 0 || copied.capacity() != 0) return false;
        // Explicit aliases exercise self-copy/move without compiler-specific diagnostics.
        skia_private::THashTable<OwnedEntry, int>& same = table;
        table = same;
        table = std::move(same);
        if (counts.live != 32) return false;
        for (int key = 1; key < 64; key += 2) {
            const OwnedEntry* entry = table.find(key);
            if (!entry || !(*entry).payload || (*(*entry).payload).value != key * 2) return false;
        }
        table.reset();
        if (counts.live != 0 || table.capacity() != 0) return false;
    }
    if (counts.live != 0 || counts.created != counts.destroyed) return false;
    return true;
}

#ifndef STX_SKIA_REFERENCE
bool full_owned_tables() {
    for (const int capacity : std::array<int, 5>{1, 2, 4, 8, 16}) {
        for (int victim = 0; victim < capacity; ++victim) {
            Counts counts{};
            {
                skia_private::THashTable<OwnedEntry, int> table{};
                for (int key = 0; key < capacity; ++key) table.set(OwnedEntry(key, counts));
                table.resize(capacity);
                if (counts.live != capacity) return false;
                if (!table.removeIfExists(victim) || counts.live != capacity - 1) return false;
                for (int key = 0; key < capacity; ++key) {
                    if (key == victim) continue;
                    const OwnedEntry* entry = table.find(key);
                    if (!entry || !(*entry).payload || (*(*entry).payload).value != key * 2) return false;
                }
            }
            if (counts.live != 0 || counts.created != counts.destroyed) return false;
        }
    }
    return true;
}
#endif

template <typename Item>
bool schedules() {
    // Exhaust small insertion permutations and each first deletion, including
    // hash-zero remapping, wraparound clusters and deliberately full tables.
    for (int count = 1; count <= 6; ++count) {
        std::vector<int> keys(static_cast<size_t>(count));
        for (int key = 0; key < count; ++key) keys[static_cast<size_t>(key)] = key;
        do {
            skia_private::THashTable<Item, int> original{};
            for (const int key : keys) {
                Item item{}; item.key = key; item.value = key * 13;
                original.set(item);
            }
#ifndef STX_SKIA_REFERENCE
            if (count == 1 || count == 2 || count == 4) original.resize(count);
#endif
            for (const int victim : keys) {
                skia_private::THashTable<Item, int> table(original);
                if (!table.removeIfExists(victim)) return false;
                if (table.removeIfExists(victim)) return false;
                for (const int key : keys) {
                    const Item* item = table.find(key);
                    if (key == victim) { if (item) return false; }
                    else if (!item || (*item).value != key * 13) return false;
                }
                for (const int key : keys) if (key != victim) table.remove(key);
                if (table.count() != 0) return false;
            }
        } while (std::next_permutation(keys.begin(), keys.end()));
    }
    return true;
}

struct PointerTraits {
    static int GetKey(const Entry* entry) { return (*entry).key; }
    static uint32_t Hash(int key) { return Entry::Hash(key); }
};
bool capacity_and_pointers() {
    std::array<Entry, 2> values{{{4, 5}, {6, 7}}};
    skia_private::THashTable<Entry*, int, PointerTraits> table{};
    table.set(&values[0]); table.set(&values[1]);
    if (table.findOrNull(4) != &values[0] || table.findOrNull(9) != nullptr) return false;
    for (int count = 1; count < 130; ++count) {
        skia_private::THashTable<Entry, int> reserved{};
        reserved.reserve(count);
        const int capacity = reserved.capacity();
        for (int key = 0; key < count; ++key) reserved.set(Entry{key, key});
        if (reserved.capacity() != capacity) return false;
        reserved.reserve(1);
        if (reserved.capacity() != capacity) return false;
    }
#ifndef STX_SKIA_REFERENCE
    if (skia_private::HashCapacityForCount(0) != 0 ||
        skia_private::HashCapacityForCount(1) != 2 ||
        skia_private::HashCapacityForCount(805306368) != 1073741824) return false;
    skia_private::THashTable<Entry, int> empty{};
    empty.reserve(0); empty.resize(0);
    if (empty.capacity() != 0 || empty.approxBytesUsed() != 0) return false;
#endif
    return true;
}

struct MixedEntry : Entry {
    static uint32_t Hash(int key) {
        const uint32_t hash = SkChecksum::Mix(static_cast<uint32_t>(key));
        return hash;
    }
};
template <typename Item>
bool measure_case(const char* mode, int count, int iterations, bool churn) {
    skia_private::THashTable<Item, int> table{};
    table.reserve(count);
    std::vector<int> keys(static_cast<size_t>(count));
    for (int key = 0; key < count; ++key) {
        Item entry{}; entry.key = key; entry.value = key * 3;
        table.set(entry); keys[static_cast<size_t>(key)] = key;
    }
    uint64_t digest = 0;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (int index = 0; index < iterations; ++index) {
        if (churn) {
            const size_t slot = static_cast<size_t>(index % count);
            if (!table.removeIfExists(keys[slot])) return false;
            Item entry{}; entry.key = count + index; entry.value = index;
            table.set(entry); keys[slot] = entry.key;
            digest += static_cast<uint64_t>(table.count());
        } else {
            const int key = (index * 17) % (count * 2);
            const Item* entry = table.find(key);
            if (entry != nullptr) digest += static_cast<uint64_t>((*entry).value);
        }
    }
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;
    std::printf("%s,%d,%d,%.9f,%llu,%zu,%zu\n", mode, count, iterations, elapsed.count(),
        static_cast<unsigned long long>(digest), sizeof(table), table.approxBytesUsed());
    return true;
}
bool measure() {
    if (!measure_case<MixedEntry>("lookup", 256, 2000000, false)) return false;
    if (!measure_case<MixedEntry>("lookup", 4096, 2000000, false)) return false;
    if (!measure_case<CollidingEntry>("collision_lookup", 64, 200000, false)) return false;
    if (!measure_case<MixedEntry>("churn", 256, 300000, true)) return false;
    if (!measure_case<CollidingEntry>("collision_churn", 64, 50000, true)) return false;
    return true;
}

}
int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) {
        const bool passed = measure();
        return passed ? 0 : 10;
    }
    if (argc == 2 && std::strcmp(argv[1], "--full-probe") == 0) {
        skia_private::THashTable<Entry, int> table{};
        table.set(Entry{1, 1}); table.resize(1);
        std::puts("Removing the sole entry from a full one-slot table");
        std::fflush(stdout);
        table.remove(1);
        return table.count() == 0 ? 0 : 11;
    }
    if (!ordinary()) return 1;
#ifndef STX_SKIA_REFERENCE
    if (!full_tables<Entry>() || !full_tables<CollidingEntry>()) return 2;
    if (!full_owned_tables()) return 7;
#endif
    if (!maps_and_sets()) return 3;
    if (!lifetimes()) return 4;
    if (!schedules<Entry>() || !schedules<CollidingEntry>()) return 5;
    if (!capacity_and_pointers()) return 6;
    std::puts("hash storage: lookup, deletion, ownership, traversal, capacity and collision schedules PASS");
    return 0;
}
