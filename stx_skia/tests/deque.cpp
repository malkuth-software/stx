#include "include/private/SkDeque.h"
#include "checked_size.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <type_traits>
#include <utility>

class DequeUnitTestHelper {
public:
    static int blocks(const SkDeque& deque) {
        const int count = deque.numBlocksAllocated();
        return count;
    }
};

#ifndef STX_SKIA_REFERENCE
template <typename T> concept TemporarySlot = requires(T&& value) { std::move(value).front(); };
static_assert(!TemporarySlot<SkDeque>);
static_assert(!std::is_constructible_v<SkDeque::Iter, SkDeque&&, SkDeque::Iter::IterStart>);
#endif

namespace {
uint64_t trace = 1469598103934665603ULL;
size_t steps = 0;

template <size_t N> using Record = std::array<uint8_t, N>;

template <size_t N> bool verify(SkDeque& actual, const std::deque<Record<N>>& expected, bool record) {
    if (actual.count() != test_sizes::to_int(expected.size()) || actual.empty() != expected.empty()) return false;
    if (actual.elemSize() != N) return false;
    if (expected.empty()) {
        if (actual.front() != nullptr || actual.back() != nullptr) return false;
    } else {
        if (std::memcmp(actual.front(), expected.front().data(), N) != 0) return false;
        if (std::memcmp(actual.back(), expected.back().data(), N) != 0) return false;
    }
    SkDeque::Iter forward(actual, SkDeque::Iter::kFront_IterStart);
    SkDeque::Iter backward(actual, SkDeque::Iter::kBack_IterStart);
    for (size_t index = 0; index < expected.size(); ++index) {
        void* front = forward.next();
        void* back = backward.prev();
        if (front == nullptr || back == nullptr) return false;
        if (std::memcmp(front, expected[index].data(), N) != 0) return false;
        if (std::memcmp(back, expected[expected.size() - index - 1].data(), N) != 0) return false;
    }
    if (forward.next() != nullptr || forward.prev() != nullptr || backward.prev() != nullptr) return false;
    if (record) {
        trace ^= static_cast<uint64_t>(actual.count());
        trace *= 1099511628211ULL;
        trace ^= static_cast<uint64_t>(DequeUnitTestHelper::blocks(actual));
        trace *= 1099511628211ULL;
    }
    ++steps;
    return true;
}

template <size_t N> bool push(SkDeque& actual, std::deque<Record<N>>& expected, bool front, uint32_t identity) {
    Record<N> value{};
    for (size_t index = 0; index < N; ++index) value[index] = static_cast<uint8_t>(identity * 37 + index * 11);
    void* slot = front ? actual.push_front() : actual.push_back();
    std::memcpy(slot, value.data(), N);
    if (front) expected.push_front(value);
    else expected.push_back(value);
    return true;
}

template <size_t N> bool ordinary() {
    for (const int allocation : {1, 2, 7, 32}) {
        SkDeque actual(N, allocation);
        std::deque<Record<N>> expected;
        if (!verify(actual, expected, true)) return false;
        for (int cycle = 0; cycle < 8; ++cycle) {
            for (int direction = 0; direction < 2; ++direction) {
                const bool front = direction == 0;
                for (int index = 0; index < 71; ++index) {
                    push(actual, expected, front, static_cast<uint32_t>(index + cycle));
                    if (!verify(actual, expected, true)) return false;
                }
                for (int index = 0; index < 71; ++index) {
                    if (front) { actual.pop_front(); expected.pop_front(); }
                    else { actual.pop_back(); expected.pop_back(); }
                    if (!verify(actual, expected, true)) return false;
                }
            }
        }
    }
    return true;
}

#ifndef STX_SKIA_REFERENCE
template <size_t N> bool mixed(bool borrowed) {
    for (const int allocation : {1, 7, 32}) {
        alignas(std::max_align_t) std::array<std::byte, 519> storage{};
        void* initial = borrowed ? storage.data() : nullptr;
        const size_t bytes = borrowed ? storage.size() : 0;
        SkDeque actual(N, initial, bytes, allocation);
        std::deque<Record<N>> expected;
        uint32_t selector = 0x713ac49b;
        for (int index = 0; index < 4096; ++index) {
            selector = selector * 1664525U + 1013904223U;
            const bool front = (selector & 0x40000000U) != 0;
            const bool adding = expected.empty() || (expected.size() < 73 && (selector & 0x80000000U) != 0);
            if (adding) push(actual, expected, front, static_cast<uint32_t>(index));
            else if (front) { actual.pop_front(); expected.pop_front(); }
            else { actual.pop_back(); expected.pop_back(); }
            if (!verify(actual, expected, false)) return false;
        }
        while (!expected.empty()) {
            actual.pop_front();
            expected.pop_front();
            if (!verify(actual, expected, false)) return false;
        }
        SkDeque::Iter unbound;
        if (unbound.next() != nullptr || unbound.prev() != nullptr) return false;
    }
    return true;
}
#endif

void benchmark() {
    for (const int allocation : {1, 8, 32}) {
        for (const int depth : {16, 256}) {
            SkDeque deque(sizeof(uint64_t), allocation);
            const int cycles = 2000000 / depth;
            uint64_t checksum = 0;
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            for (int cycle = 0; cycle < cycles; ++cycle) {
                for (int index = 0; index < depth; ++index) {
                    const uint64_t value = static_cast<uint64_t>(index + cycle);
                    std::memcpy(deque.push_back(), &value, sizeof(value));
                }
                for (int index = 0; index < depth; ++index) {
                    uint64_t value = 0;
                    std::memcpy(&value, deque.back(), sizeof(value));
                    checksum += value;
                    deque.pop_back();
                }
            }
            const std::chrono::steady_clock::time_point finish = std::chrono::steady_clock::now();
            const std::chrono::duration<double> elapsed = finish - start;
            std::printf("%d,%d,%.9f,%llu\n", allocation, depth, elapsed.count(),
                        static_cast<unsigned long long>(checksum));
        }
    }
}
}

int main(int argc, char** argv) {
    bool valid = ordinary<1>();
    valid = ordinary<3>() && valid;
    valid = ordinary<8>() && valid;
    valid = ordinary<64>() && valid;
    const size_t ordinarySteps = steps;
#ifndef STX_SKIA_REFERENCE
    valid = mixed<1>(false) && valid;
    valid = mixed<3>(false) && valid;
    valid = mixed<8>(false) && valid;
    valid = mixed<64>(false) && valid;
    valid = mixed<1>(true) && valid;
    valid = mixed<3>(true) && valid;
    valid = mixed<8>(true) && valid;
    valid = mixed<64>(true) && valid;
    if (valid) std::printf("candidate mixed operations: %zu PASS\n", steps - ordinarySteps);
#endif
    if (!valid) { std::fputs("deque contract mismatch\n", stderr); return 1; }
    std::printf("deque: %zu ordinary steps, trace=%llu PASS; owner=%zu iterator=%zu bytes\n",
                ordinarySteps, static_cast<unsigned long long>(trace), sizeof(SkDeque), sizeof(SkDeque::Iter));
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) benchmark();
    return 0;
}
