#include "src/core/SkTSort.h"
#include "checked_size.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct Record {
    int key = 0;
    int identity = 0;
};
struct Trace {
    uint64_t comparisons = 0;
    uint64_t digest = UINT64_C(1469598103934665603);
};
void hash_value(uint64_t& digest, uint32_t value) {
    digest ^= value;
    digest *= UINT64_C(1099511628211);
}
struct Less {
    Trace* trace = nullptr;
    bool operator()(const Record& a, const Record& b) const {
        if (trace != nullptr) {
            ++(*trace).comparisons;
            hash_value((*trace).digest, static_cast<uint32_t>(a.identity));
            hash_value((*trace).digest, static_cast<uint32_t>(b.identity));
        }
        const bool result = a.key < b.key;
        return result;
    }
};
struct IntegerLess {
    bool operator()(const int& a, const int& b) const {
        const bool result = a < b;
        return result;
    }
};
struct StringLess {
    bool operator()(const std::string& a, const std::string& b) const {
        const bool result = a < b;
        return result;
    }
};
enum class Algorithm { Heap, Insertion, Intro, HeapFallback };
void sort_records(Algorithm algorithm, Record* data, int count, const Less& less) {
    switch (algorithm) {
        case Algorithm::Heap: SkTHeapSort(data, static_cast<size_t>(count), less); return;
        case Algorithm::Insertion: SkTInsertionSort(data, count, less); return;
        case Algorithm::Intro: SkTQSort(data, data + count, less); return;
        case Algorithm::HeapFallback: SkTIntroSort(0, data, count, less); return;
        default: SkASSERT_RELEASE(false); return;
    }
}

bool verify(const std::vector<int>& keys, Algorithm algorithm, Trace& trace, uint64_t& digest) {
    const int count = test_sizes::to_int(keys.size());
    std::vector<Record> guarded(keys.size() + 2);
    guarded.front() = {-10001, -10001};
    guarded.back() = {-10002, -10002};
    for (int index = 0; index < count; ++index) {
        guarded[static_cast<size_t>(index + 1)] = {keys[static_cast<size_t>(index)], index};
    }
    Record* values = guarded.data() + 1;
    const Less less{&trace};
    sort_records(algorithm, values, count, less);
    if (guarded.front().key != -10001 || guarded.front().identity != -10001) return false;
    if (guarded.back().key != -10002 || guarded.back().identity != -10002) return false;
    std::vector<bool> seen(keys.size(), false);
    for (int index = 0; index < count; ++index) {
        const Record& value = values[index];
        if (value.identity < 0 || value.identity >= count) return false;
        const size_t identity = static_cast<size_t>(value.identity);
        if (seen[identity] || value.key != keys[identity]) return false;
        seen[identity] = true;
        if (index > 0 && values[index - 1].key > value.key) return false;
        if (algorithm == Algorithm::Insertion && index > 0 && values[index - 1].key == value.key) {
            if (values[index - 1].identity > value.identity) return false;
        }
        hash_value(digest, static_cast<uint32_t>(value.identity));
        hash_value(digest, static_cast<uint32_t>(value.key));
    }
    return true;
}

bool permutations(Trace& trace, uint64_t& digest, int& cases) {
    for (int count = 1; count <= 7; ++count) {
        std::vector<int> order(static_cast<size_t>(count));
        for (int index = 0; index < count; ++index) order[static_cast<size_t>(index)] = index;
        do {
            for (int divisor = 1; divisor <= 3; ++divisor) {
                std::vector<int> keys(order);
                for (int& key : keys) key /= divisor;
                for (const Algorithm algorithm : std::array<Algorithm, 3>{
                         Algorithm::Heap, Algorithm::Insertion, Algorithm::Intro}) {
                    if (!verify(keys, algorithm, trace, digest)) return false;
                    ++cases;
                }
            }
        } while (std::next_permutation(order.begin(), order.end()));
    }
    return true;
}

int patterned_key(int index, int count, int pattern) {
    switch (pattern) {
        case 0: return index;
        case 1: { const int result = count - index; return result; }
        case 2: return 0;
        case 3: { const int result = index % 7; return result; }
        case 4: { const int result = (index * 919) % count; return result; }
        case 5: { const int result = std::min(index, count - index); return result; }
        default: return 0;
    }
}

bool boundary_patterns(Trace& trace, uint64_t& digest, int& cases) {
    for (const int count : std::array<int, 14>{1, 2, 3, 15, 16, 17, 31, 32, 33, 63, 64, 65, 257, 1024}) {
        for (int pattern = 0; pattern < 6; ++pattern) {
            std::vector<int> keys(static_cast<size_t>(count));
            for (int index = 0; index < count; ++index) {
                keys[static_cast<size_t>(index)] = patterned_key(index, count, pattern);
            }
            for (const Algorithm algorithm : std::array<Algorithm, 4>{
                     Algorithm::Heap, Algorithm::Insertion, Algorithm::Intro, Algorithm::HeapFallback}) {
                if (!verify(keys, algorithm, trace, digest)) return false;
                ++cases;
            }
        }
    }
    return true;
}

bool defaults_and_owning_values() {
    std::array<int, 5> integers{7, 2, 9, 1, 5};
    SkTHeapSort(integers.data(), integers.size());
    if (!std::is_sorted(integers.begin(), integers.end())) return false;
    std::reverse(integers.begin(), integers.end());
    SkTQSort(integers.data(), integers.data() + integers.size());
    if (!std::is_sorted(integers.begin(), integers.end())) return false;
    std::array<const int*, 5> pointers{};
    for (size_t index = 0; index < pointers.size(); ++index) pointers[index] = &integers[4 - index];
    SkTQSort(pointers.data(), pointers.data() + pointers.size());
    for (size_t index = 0; index < pointers.size(); ++index) {
        if (*pointers[index] != integers[index]) return false;
    }
    std::vector<std::string> originals{};
    for (int index = 0; index < 128; ++index) {
        std::string value(80, static_cast<char>('a' + index % 26));
        value += std::to_string((index * 31) % 43);
        originals.push_back(value);
    }
    std::vector<std::string> expected(originals);
    std::sort(expected.begin(), expected.end());
    const StringLess less{};
    for (int mode = 0; mode < 3; ++mode) {
        std::vector<std::string> values(originals);
        switch (mode) {
            case 0: SkTHeapSort(values.data(), values.size(), less); break;
            case 1: SkTInsertionSort(values.data(), test_sizes::to_int(values.size()), less); break;
            case 2:
                if (!values.empty()) {
                    SkTQSort(values.data(), &values.back() + 1, less);
                }
                break;
            default: return false;
        }
        if (values != expected) return false;
    }
#ifndef STX_SKIA_REFERENCE
    const IntegerLess integerLess{};
    SkTHeapSort<int>(nullptr, 0);
    SkTInsertionSort<int>(nullptr, 0, integerLess);
    SkTIntroSort<int>(0, nullptr, 0, integerLess);
    SkTQSort<int>(nullptr, nullptr);
    SkTHeapSort(integers.data(), 0);
    SkTInsertionSort(integers.data(), 0, integerLess);
    SkTQSort(integers.data(), integers.data());
#endif
    return true;
}

using Clock = std::chrono::steady_clock;
void benchmark() {
    const std::array<const char*, 6> patterns{"ascending", "descending", "equal", "few_keys", "permuted", "organ_pipe"};
    for (const int count : std::array<int, 3>{32, 256, 4096}) {
        const int rounds = 262144 / count;
        for (int pattern = 0; pattern < 6; ++pattern) {
            std::vector<Record> original(static_cast<size_t>(count));
            for (int index = 0; index < count; ++index) {
                const int key = patterned_key(index, count, pattern);
                original[static_cast<size_t>(index)] = {key, index};
            }
            std::vector<Record> values(original);
            const Less less{};
            uint64_t digest = UINT64_C(1469598103934665603);
            const Clock::time_point start = Clock::now();
            for (int round = 0; round < rounds; ++round) {
                std::copy(original.begin(), original.end(), values.begin());
                SkTQSort(values.data(), &values.back() + 1, less);  // count >= 32
                hash_value(digest, static_cast<uint32_t>(values.front().identity));
                hash_value(digest, static_cast<uint32_t>(values.back().identity));
            }
            const Clock::time_point end = Clock::now();
            const double seconds = std::chrono::duration<double>(end - start).count();
            std::printf("%s_%d,%d,%.9f,%llu\n", patterns[static_cast<size_t>(pattern)], count,
                        count * rounds, seconds, static_cast<unsigned long long>(digest));
        }
    }
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) {
        benchmark();
        return 0;
    }
    Trace trace{};
    uint64_t digest = UINT64_C(1469598103934665603);
    int cases = 0;
    if (!permutations(trace, digest, cases) || !boundary_patterns(trace, digest, cases) ||
        !defaults_and_owning_values()) return 1;
    std::printf("sort: %d cases, comparisons=%llu trace=%llu output=%llu\n", cases,
                static_cast<unsigned long long>(trace.comparisons),
                static_cast<unsigned long long>(trace.digest),
                static_cast<unsigned long long>(digest));
    return 0;
}
