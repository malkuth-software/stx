#include "include/private/SkAlign.h"
#include "include/private/SkContainers.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkTemplates.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

struct SkContainerAllocatorTestingPeer {
    static size_t round(const SkContainerAllocator& owner, int64_t capacity) {
        const size_t rounded = owner.roundUpCapacity(capacity);
        return rounded;
    }
    static size_t grow(const SkContainerAllocator& owner, int capacity, double factor) {
        const size_t grown = owner.growthFactorCapacity(capacity, factor);
        return grown;
    }
};

namespace {
struct Triple { uint8_t values[3]; };
static_assert(SkContainerAllocator::RoundUp<Triple>(1) == 2);
static_assert(SkContainerAllocator::RoundUp<uint32_t>(1) == 2);
static_assert(SkContainerAllocator::RoundUp<uint32_t>(0) == 0);
static_assert(SkAlign4(5) == 8);

bool alignment_cases(size_t& cases) {
    for (size_t value = 0; value < 4096; ++value) {
        const std::array<size_t, 4> expected{
            ((value + 1) / 2) * 2, ((value + 3) / 4) * 4,
            ((value + 7) / 8) * 8, ((value + 15) / 16) * 16};
        if (SkAlign2(value) != expected[0] || SkAlign4(value) != expected[1] ||
            SkAlign8(value) != expected[2] || SkAlign16(value) != expected[3]) return false;
        if (SkIsAlign2(value) != (value % 2 == 0) || SkIsAlign4(value) != (value % 4 == 0) ||
            SkIsAlign8(value) != (value % 8 == 0) || SkIsAlign16(value) != (value % 16 == 0)) return false;
        const size_t pointerRounded = ((value + sizeof(void*) - 1) / sizeof(void*)) * sizeof(void*);
        if (SkAlignPtr(value) != pointerRounded || SkIsAlignPtr(value) != (value % sizeof(void*) == 0)) return false;
        for (size_t alignment = 1; alignment <= 64; alignment *= 2) {
            const size_t expectedRounded = ((value + alignment - 1) / alignment) * alignment;
            if (SkAlignTo(value, alignment) != expectedRounded) return false;
        }
        for (size_t alignment = 3; alignment < 18; alignment += 2) {
            const size_t expectedRounded = ((value + alignment - 1) / alignment) * alignment;
            if (SkAlignNonPow2(value, alignment) != expectedRounded) return false;
        }
        const size_t roundedTriples = ((value * sizeof(Triple) + 7) / 8 * 8) / sizeof(Triple);
        if (SkContainerAllocator::RoundUp<Triple>(value) != roundedTriples) return false;
        ++cases;
    }
    const size_t maximum = std::numeric_limits<size_t>::max();
    for (size_t distance = 0; distance <= 64; ++distance) {
        const size_t value = maximum - distance;
        for (size_t alignment = 1; alignment <= 64; alignment *= 2) {
            const size_t blocks = value / alignment + (value % alignment != 0 ? 1 : 0);
            const size_t expected = blocks * alignment;
            if (SkAlignTo(value, alignment) != expected) return false;
        }
        const size_t rounded4 = (value / 4 + (value % 4 != 0 ? 1 : 0)) * 4;
        if (SkAlign4(value) != rounded4) return false;
        ++cases;
    }
    return true;
}

bool growth_cases(size_t& cases, uint64_t& fingerprint, std::vector<uint64_t>& trace) {
    const std::array<int, 10> maxima{0, 1, 7, 8, 9, 16, 63, 64, 65, std::numeric_limits<int>::max()};
    const std::array<double, 6> factors{1.0, 1.01, 1.5, 2.0, 8.0, 1000000.0};
    trace.reserve(maxima.size() * factors.size() * 513);
    for (const int maximum : maxima) {
        SkContainerAllocator allocator(1, maximum);
        for (const double factor : factors) {
            size_t previous = 0;
            for (int capacity = 0; capacity <= 512; ++capacity) {
                const size_t grown = SkContainerAllocatorTestingPeer::grow(allocator, capacity, factor);
                if (grown > static_cast<size_t>(maximum) || grown < previous) return false;
                if (grown != static_cast<size_t>(maximum) && grown % 8 != 0) return false;
                const double minimum = static_cast<double>(capacity) * factor;
                if (minimum < maximum && grown < static_cast<size_t>(minimum)) return false;
                if (minimum >= maximum && grown != static_cast<size_t>(maximum)) return false;
                trace.push_back(static_cast<uint64_t>(grown));
                fingerprint ^= static_cast<uint64_t>(grown);
                fingerprint *= 1099511628211ULL;
                previous = grown;
                ++cases;
            }
            const size_t saturated = SkContainerAllocatorTestingPeer::grow(allocator, std::numeric_limits<int>::max(), factor);
            if (saturated != static_cast<size_t>(maximum)) return false;
        }
        if (SkContainerAllocatorTestingPeer::round(allocator, std::numeric_limits<int64_t>::max()) != static_cast<size_t>(maximum)) return false;
#ifndef STX_SKIA_REFERENCE
        const size_t enormous = SkContainerAllocatorTestingPeer::grow(allocator, 16, std::numeric_limits<double>::max());
        if (enormous != static_cast<size_t>(maximum)) return false;
#endif
    }
    return true;
}

bool allocation_cases(size_t& cases) {
    for (const size_t elementSize : std::array<size_t, 3>{1, 3, 16}) {
        SkContainerAllocator allocator(elementSize, 64);
        for (const double factor : std::array<double, 3>{1.0, 1.5, 2.0}) {
            for (int capacity = 0; capacity <= 64; ++capacity) {
                skia_private::UniqueVoidPtr owner{};
                const SkSpan<std::byte> allocation = allocator.allocate(capacity, factor);
                owner.reset(allocation.data());
                if (capacity == 0) {
                    if (!allocation.empty() || allocation.data() != nullptr) return false;
                } else {
                    size_t minimum = static_cast<size_t>(capacity);
                    if (factor > 1.0) minimum = SkContainerAllocatorTestingPeer::grow(allocator, capacity, factor);
                    if (allocation.data() == nullptr || allocation.size() < minimum * elementSize) return false;
                    if (reinterpret_cast<uintptr_t>(allocation.data()) % alignof(std::max_align_t) != 0) return false;
                    allocation[0] = std::byte{0x5a};
                    allocation[allocation.size() - 1] = std::byte{0x37};
                }
                ++cases;
            }
        }
    }
    for (const size_t size : std::array<size_t, 5>{0, 1, 7, 64, 257}) {
        skia_private::UniqueVoidPtr owner{};
        const SkSpan<std::byte> allocation = sk_allocate_canfail(size);
        owner.reset(allocation.data());
        if (allocation.data() == nullptr || allocation.size() < size || allocation.size() < alignof(std::max_align_t)) return false;
        ++cases;
    }
    return true;
}
}

int main(int argc, char** argv) {
    size_t alignments = 0;
    size_t growth = 0;
    size_t allocations = 0;
    uint64_t fingerprint = 14695981039346656037ULL;
    std::vector<uint64_t> trace{};
    if (!alignment_cases(alignments) || !growth_cases(growth, fingerprint, trace) || !allocation_cases(allocations)) return 1;
    if (argc > 1) {
        FILE* output = std::fopen(argv[1], "wb");
        if (output == nullptr) return 2;
        const size_t written = std::fwrite(trace.data(), sizeof(uint64_t), trace.size(), output);
        const int closed = std::fclose(output);
        if (written != trace.size() || closed != 0) return 3;
    }
    std::printf("container capacity: %zu alignment cases, %zu growth cases, %zu allocations PASS; allocator=%zu bytes; growth=%llu\n",
                alignments, growth, allocations, sizeof(SkContainerAllocator),
                static_cast<unsigned long long>(fingerprint));
    return 0;
}
