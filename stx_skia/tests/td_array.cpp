#include "include/private/SkTDArray.h"
#include "checked_size.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace {
bool equals(const SkTDArray<int>& actual, const std::vector<int>& expected) {
    if (actual.size() != test_sizes::to_int(expected.size())) return false;
    if (actual.capacity() < actual.size()) return false;
    for (int index = 0; index < actual.size(); ++index) {
        if (actual[index] != expected[static_cast<size_t>(index)]) return false;
    }
    return true;
}

bool basic_operations() {
    SkTDArray<int> values{};
    if (!values.empty() || values.data() != nullptr) return false;
#ifndef STX_SKIA_REFERENCE
    if (values.end() != nullptr || values.append(0) != nullptr) return false;
    if (values.insert(0, 0) != nullptr) return false;
#endif
    values.reserve(40);
    const int capacity = values.capacity();
    std::vector<int> expected{};
    for (int index = 0; index < 40; ++index) {
        values.push_back(index);
        expected.push_back(index);
    }
    if (!equals(values, expected) || values.capacity() != capacity) return false;
    for (int index = 39; index >= 0; --index) {
        if (values.back() != index) return false;
        values.pop_back();
    }
    values.resize(40);
    for (int index = 0; index < 40; ++index) values[index] = index;
    SkTDArray<int> copy(values);
    if (!(copy == values)) return false;
    SkTDArray<int> assigned{};
    assigned = copy;
    if (!(assigned == values)) return false;
    assigned.clear();
    assigned = copy;
    SkTDArray<int>& same = assigned;
    assigned = same;
    if (!(assigned == values)) return false;
    values.remove(11, 17);
    expected.erase(expected.begin() + 11, expected.begin() + 28);
    if (!equals(values, expected)) return false;
    values.removeShuffle(3);
    expected[3] = expected.back();
    expected.pop_back();
    if (!equals(values, expected)) return false;
    values.shrink_to_fit();
    if (values.capacity() != values.size()) return false;
    values.clear();
    values.shrink_to_fit();
    if (values.data() != nullptr || values.capacity() != 0) return false;
    values.swap(copy);
    if (!equals(values, std::vector<int>(assigned.begin(), assigned.end()))) return false;
    values.reset();
    const bool empty = values.empty() && values.capacity() == 0 && values.data() == nullptr;
    return empty;
}

bool insertion_cases(bool internalSource) {
    for (int length = 1; length <= 18; ++length) {
        std::vector<int> original(static_cast<size_t>(length));
        for (int index = 0; index < length; ++index) original[static_cast<size_t>(index)] = index * 7 - 41;
        for (int position = 0; position <= length; ++position) {
            for (int start = 0; start < length; ++start) {
                for (int count = 1; count <= length - start; ++count) {
                    for (int spare = 0; spare < 2; ++spare) {
                        SkTDArray<int> values(original.data(), length);
                        if (spare != 0) values.reserve(length * 2);
                        std::vector<int> expected(original);
                        expected.insert(expected.begin() + position,
                                        original.begin() + start, original.begin() + start + count);
                        const int* source = original.data() + start;
                        if (internalSource) source = values.data() + start;
                        int* inserted = values.insert(position, count, source);
                        if (inserted != values.data() + position) return false;
                        if (!equals(values, expected)) return false;
                        values.remove(position, count);
                        if (!equals(values, original)) return false;
                    }
                }
            }
        }
    }
    return true;
}

#ifndef STX_SKIA_REFERENCE
template <typename T> concept TemporaryData = requires { T{}.data(); };
template <typename T> concept TemporaryBack = requires { T{}.back(); };
static_assert(!TemporaryData<SkTDArray<int>>);
static_assert(!TemporaryBack<SkTDArray<int>>);

bool corrected_operations() {
    SkTDArray<int> source{1, 2, 3, 4};
    int* allocation = source.data();
    SkTDArray<int> moved(std::move(source));
    if (moved.data() != allocation || !source.empty() || source.capacity() != 0) return false;
    source.push_back(19);
    if (source[0] != 19 || !equals(moved, {1, 2, 3, 4})) return false;
    source = std::move(moved);
    if (source.data() != allocation || !moved.empty() || moved.capacity() != 0) return false;
    moved.push_back(23);
    if (moved[0] != 23) return false;
    SkTDArray<int>& same = source;
    source = std::move(same);
    if (!equals(source, {1, 2, 3, 4})) return false;
    source.shrink_to_fit();
    source.push_back(source[2]);
    if (!equals(source, {1, 2, 3, 4, 3})) return false;
    source.shrink_to_fit();
    source.append(5, source.data());
    if (!equals(source, {1, 2, 3, 4, 3, 1, 2, 3, 4, 3})) return false;
    // The raw boundary also permits sources not aligned to element boundaries.
    const std::array<unsigned char, 12> bytes{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    SkTDStorage raw(bytes.data(), 4, 3);
    const unsigned char* data = static_cast<const unsigned char*>(raw.data());
    raw.insert(2, 2, data + 2);
    const std::array<unsigned char, 18> expected{0, 1, 2, 3, 4, 5, 2, 3, 4, 5, 6, 7, 6, 7, 8, 9, 10, 11};
    const int comparison = memcmp(raw.data(), expected.data(), expected.size());
    const bool equal = comparison == 0;
    return equal;
}
#endif

using Clock = std::chrono::steady_clock;
void benchmark() {
    constexpr int count = 1000000;
    SkTDArray<int> values{};
    values.reserve(count);
    const Clock::time_point start = Clock::now();
    for (int index = 0; index < count; ++index) values.push_back(index);
    const Clock::time_point end = Clock::now();
    const double elapsed = std::chrono::duration<double, std::nano>(end - start).count();
    unsigned long long checksum = 0;
    for (const int value : values) checksum += static_cast<unsigned int>(value);
    std::printf("reserved_push_ns %.4f checksum %llu storage %zu array %zu\n",
                elapsed / count, checksum, sizeof(SkTDStorage), sizeof(SkTDArray<int>));
    SkTDArray<int> small{1, 2, 3, 4, 5, 6, 7, 8};
    small.reserve(64);
    const std::array<int, 4> extra{11, 12, 13, 14};
    const Clock::time_point insertStart = Clock::now();
    for (int index = 0; index < count; ++index) {
        small.insert(3, 4, extra.data());
        small.remove(3, 4);
    }
    const Clock::time_point insertEnd = Clock::now();
    const double insertElapsed = std::chrono::duration<double, std::nano>(insertEnd - insertStart).count();
    std::printf("insert_erase_ns %.4f final %d\n", insertElapsed / count, small[7]);
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) {
        benchmark();
        return 0;
    }
    if (!basic_operations() || !insertion_cases(false)) return 1;
#ifndef STX_SKIA_REFERENCE
    if (!corrected_operations() || !insertion_cases(true)) return 1;
#endif
    std::puts("Raw-array storage and insertion contracts passed");
    return 0;
}
