#include "include/private/SkTemplates.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

using skia_private::AutoTMalloc;
static_assert(!std::is_copy_constructible_v<AutoTMalloc<uint32_t>>);
static_assert(std::is_nothrow_move_constructible_v<AutoTMalloc<uint32_t>>);
static_assert(sizeof(AutoTMalloc<uint32_t>) == sizeof(uint32_t*));
#ifndef STX_SKIA_REFERENCE
template <typename T> concept TemporaryTypedBorrow = requires(T&& owner) { std::move(owner).get(); };
static_assert(!TemporaryTypedBorrow<AutoTMalloc<uint32_t>>);
static_assert(!std::is_convertible_v<AutoTMalloc<uint32_t>&&, uint32_t*>);
using TypedConstBorrow = decltype(std::declval<const AutoTMalloc<uint32_t>&>().get());
static_assert(std::is_same_v<TypedConstBorrow, const uint32_t*>);
#endif

namespace {
template <typename T> bool resizing(size_t& transitions) {
    const std::array<size_t, 18> counts{0, 1, 2, 31, 3, 0, 64, 64, 7, 257, 1024, 1, 0, 0, 9, 65, 8, 0};
    AutoTMalloc<T> storage{};
    size_t previousCount = 0;
    for (size_t pass = 0; pass < 16; ++pass) {
        for (const size_t count : counts) {
            storage.realloc(count);
            if ((storage.get() == nullptr) != (count == 0)) return false;
            const size_t preserved = count < previousCount ? count : previousCount;
            for (size_t index = 0; index < preserved; ++index) {
                const T expected = static_cast<T>((index % 97) + 1);
                if (storage[static_cast<int>(index)] != expected) return false;
            }
            T* writable = storage;
            const AutoTMalloc<T>& observation = storage;
            const T* readable = observation;
            if (readable != writable || readable != observation.data() || writable != storage.data()) return false;
            for (size_t index = 0; index < count; ++index) {
                writable[index] = static_cast<T>((index % 97) + 1);
            }
            previousCount = count;
            ++transitions;
        }
    }
    return true;
}

bool ownership() {
    AutoTMalloc<uint32_t> source(size_t{32});
    if (source.get() == nullptr) return false;
    for (int index = 0; index < 32; ++index) source[index] = static_cast<uint32_t>(index + 3);
    uint32_t* allocation = source.get();
    AutoTMalloc<uint32_t> moved(std::move(source));
    if (source.get() != nullptr || moved.get() != allocation) return false;
    source.realloc(32);
    if (source.get() == nullptr || source.get() == moved.get()) return false;
    AutoTMalloc<uint32_t> destination(size_t{4});
    destination = std::move(moved);
    if (moved.get() != nullptr || destination.get() != allocation) return false;
    AutoTMalloc<uint32_t>& same = destination;
    destination = std::move(same);
    if (destination.get() != allocation) return false;
    const AutoTMalloc<uint32_t>& observation = destination;
    for (int index = 0; index < 32; ++index) {
        if (observation[index] != static_cast<uint32_t>(index + 3)) return false;
    }
    uint32_t* released = destination.release();
    AutoTMalloc<uint32_t> adopted(released);
    if (destination.get() != nullptr || adopted.get() != allocation) return false;
    const uintptr_t previousAddress = reinterpret_cast<uintptr_t>(adopted.get());
    uint32_t* replacement = adopted.reset(32);
    if (replacement == nullptr || reinterpret_cast<uintptr_t>(replacement) == previousAddress) return false;
    if (adopted.reset() != nullptr || adopted.get() != nullptr) return false;
    if (moved.reset(1) == nullptr) return false;
    moved[0] = 99;
    moved.realloc(0);
    return moved.get() == nullptr;
}
}

int main() {
    size_t transitions = 0;
    if (!resizing<uint8_t>(transitions) || !resizing<uint32_t>(transitions) ||
        !resizing<double>(transitions) || !ownership()) return 1;
    std::printf("typed allocation: %zu resize transitions, preservation, transfer, reset PASS; owner=%zu bytes\n",
                transitions, sizeof(AutoTMalloc<uint32_t>));
    return 0;
}
