#include "include/private/SkMalloc.h"
#include "include/private/SkTemplates.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
using skia_private::UniqueVoidPtr;

bool allocations(size_t& cases) {
    const std::array<size_t, 8> counts{0, 1, 3, 8, 17, 64, 257, 4096};
    const std::array<size_t, 5> widths{0, 1, 3, 8, 16};
    for (const size_t count : counts) {
        for (const size_t width : widths) {
            const size_t bytes = count * width;
            for (int mode = 0; mode < 5; ++mode) {
                void* pointer = nullptr;
                switch (mode) {
                    case 0: pointer = sk_malloc_throw(count, width); break;
                    case 1: pointer = sk_calloc_throw(count, width); break;
                    case 2: pointer = sk_malloc_canfail(count, width); break;
                    case 3: pointer = sk_calloc_canfail(bytes); break;
                    case 4: pointer = sk_malloc_flags(bytes, SK_MALLOC_ZERO_INITIALIZE | 0x80U); break;
                    default: return false;
                }
                UniqueVoidPtr storage(pointer);
                if (bytes != 0) {
                    if (pointer == nullptr || reinterpret_cast<uintptr_t>(pointer) % alignof(std::max_align_t) != 0) return false;
                    if (sk_malloc_size(pointer, bytes) < bytes) return false;
                    uint8_t* data = static_cast<uint8_t*>(pointer);
                    if (mode == 1 || mode == 3 || mode == 4) {
                        for (size_t index = 0; index < bytes; ++index) {
                            if (data[index] != 0) return false;
                        }
                    }
                    std::memset(data, 0x5a, bytes);
                }
                ++cases;
            }
        }
    }
    UniqueVoidPtr storage{};
    size_t previousBytes = 0;
    for (const size_t count : std::array<size_t, 9>{0, 1, 5, 257, 8, 1, 0, 1024, 0}) {
        void* previous = storage.release();
        void* replacement = sk_realloc_throw(previous, count, size_t{3});
        storage.reset(replacement);
        const size_t bytes = count * 3;
        if ((replacement == nullptr) != (bytes == 0)) return false;
        const size_t preserved = bytes < previousBytes ? bytes : previousBytes;
        uint8_t* data = static_cast<uint8_t*>(replacement);
        for (size_t index = 0; index < preserved; ++index) {
            if (data[index] != 0x37) return false;
        }
        if (bytes != 0) std::memset(data, 0x37, bytes);
        previousBytes = bytes;
        ++cases;
    }
#ifndef STX_SKIA_REFERENCE
    const size_t maximum = std::numeric_limits<size_t>::max();
    const std::array<size_t, 3> overflowingCounts{maximum, maximum / 3 + 1, maximum};
    const std::array<size_t, 3> elementSizes{2, 3, maximum};
    for (size_t index = 0; index < overflowingCounts.size(); ++index) {
        UniqueVoidPtr rejected(sk_malloc_canfail(overflowingCounts[index], elementSizes[index]));
        if (rejected.get() != nullptr) return false;
    }
#endif
    sk_free(nullptr);
    return true;
}

bool byte_operations(size_t& cases) {
    sk_bzero(nullptr, 0);
    if (sk_careful_memcpy(nullptr, nullptr, 0) != nullptr) return false;
    if (sk_careful_memmove(nullptr, nullptr, 0) != nullptr) return false;
    if (sk_careful_memcmp(nullptr, nullptr, 0) != 0) return false;
    std::array<uint8_t, 64> source{};
    for (size_t index = 0; index < source.size(); ++index) source[index] = static_cast<uint8_t>(index + 1);
    for (size_t from = 0; from <= 12; ++from) {
        for (size_t to = 0; to <= 12; ++to) {
            for (size_t count = 0; count <= 32; ++count) {
                std::array<uint8_t, 64> actual = source;
                std::array<uint8_t, 64> expected = source;
                for (size_t index = 0; index < count; ++index) expected[to + index] = source[from + index];
                if (sk_careful_memmove(actual.data() + to, actual.data() + from, count) != actual.data() + to) return false;
                if (actual != expected) return false;
                actual = source;
                if (sk_careful_memcpy(actual.data() + to, source.data() + from, count) != actual.data() + to) return false;
                if (actual != expected) return false;
                if (sk_careful_memcmp(actual.data(), expected.data(), actual.size()) != 0) return false;
                sk_bzero(actual.data() + to, count);
                for (size_t index = 0; index < count; ++index) expected[to + index] = 0;
                if (actual != expected) return false;
                ++cases;
            }
        }
    }
    std::array<uint8_t, 64> greater = source;
    ++greater[7];
    if (sk_careful_memcmp(source.data(), greater.data(), source.size()) >= 0) return false;
    if (sk_careful_memcmp(greater.data(), source.data(), source.size()) <= 0) return false;
    return true;
}
}

int main() {
    size_t allocationCases = 0;
    size_t byteCases = 0;
    if (!allocations(allocationCases) || !byte_operations(byteCases)) return 1;
    std::printf("allocation primitives: %zu allocation/reallocation cases, %zu overlap/copy/clear cases PASS\n",
                allocationCases, byteCases);
    return 0;
}
