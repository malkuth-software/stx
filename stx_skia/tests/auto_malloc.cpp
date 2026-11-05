#include "src/core/SkAutoMalloc.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<SkAutoMalloc>);
static_assert(std::is_move_constructible_v<SkAutoMalloc>);
static_assert(!std::is_move_constructible_v<SkAutoSMalloc<64>>);
#ifndef STX_SKIA_REFERENCE
template <typename T> concept TemporaryBorrow = requires(T&& owner) { std::move(owner).get(); };
static_assert(!TemporaryBorrow<SkAutoMalloc>);
static_assert(!TemporaryBorrow<SkAutoSMalloc<64>>);
using InlineConstBorrow = decltype(std::declval<const SkAutoSMalloc<64>&>().get());
static_assert(std::is_same_v<InlineConstBorrow, const void*>);
#endif

namespace {
bool heap_owner() {
    SkAutoMalloc storage{};
    if (storage.get() != nullptr || storage.reset() != nullptr) return false;
    void* original = storage.reset(64);
    if (original == nullptr) return false;
    std::memset(original, 0x5a, 64);
    if (storage.reset(16, SkAutoMalloc::kReuse_OnShrink) != original) return false;
    if (storage.reset(64) != original) return false;
    const uint8_t* retained = static_cast<const uint8_t*>(storage.get());
    for (size_t index = 0; index < 64; ++index) {
        if (retained[index] != 0x5a) return false;
    }
    void* transferred = storage.release();
    const bool released = transferred == original && storage.get() == nullptr;
    sk_free(transferred);
    if (!released || storage.reset(64) == nullptr) return false;
    storage.reset();
    if (storage.get() != nullptr) return false;

    SkAutoMalloc source(96);
    void* sourcePointer = source.get();
    std::memset(sourcePointer, 0x37, 96);
    SkAutoMalloc moved(std::move(source));
    if (moved.get() != sourcePointer || source.get() != nullptr) return false;
    SkAutoMalloc destination(32);
    destination = std::move(moved);
    if (destination.get() != sourcePointer || moved.get() != nullptr) return false;
    SkAutoMalloc& same = destination;
    destination = std::move(same);
    if (destination.get() != sourcePointer) return false;
    const SkAutoMalloc& observation = destination;
    const uint8_t* bytes = static_cast<const uint8_t*>(observation.get());
    for (size_t index = 0; index < 96; ++index) {
        if (bytes[index] != 0x37) return false;
    }
#ifndef STX_SKIA_REFERENCE
    // Reusing the old requested size must allocate after ownership moved away.
    if (source.reset(96) == nullptr || moved.reset(96, SkAutoMalloc::kReuse_OnShrink) == nullptr) return false;
#endif
    return true;
}

template <size_t Inline> bool inline_owner(size_t& transitions) {
    const size_t minimum = SkAlign4(Inline);
    const std::array<size_t, 15> requests{
        0, 1, minimum, minimum + 1, minimum * 2, minimum / 2, minimum * 2,
        0, minimum * 4, minimum * 4, minimum, 0, minimum * 8, 1, 0};
    for (int policy = 0; policy < 2; ++policy) {
        SkAutoSMalloc<Inline> storage{};
        void* initial = storage.get();
        if (initial == nullptr || reinterpret_cast<uintptr_t>(initial) % alignof(uint32_t) != 0) return false;
        size_t capacity = minimum;
        void* previous = initial;
        uint8_t pattern = 0x12;
        std::memset(previous, pattern, capacity);
        for (const size_t requested : requests) {
            const SkAutoMalloc::OnShrink shrink = static_cast<SkAutoMalloc::OnShrink>(policy);
            const size_t normalized = requested < minimum ? minimum : requested;
            const bool expectedChange = normalized != capacity &&
                                        (shrink == SkAutoMalloc::kAlloc_OnShrink || normalized > capacity);
            bool changed = !expectedChange;
            void* current = storage.reset(requested, shrink, &changed);
            if (current == nullptr || current != storage.get() || changed != expectedChange) return false;
            if (!changed) {
                if (current != previous) return false;
                const uint8_t* bytes = static_cast<const uint8_t*>(current);
                for (size_t index = 0; index < capacity; ++index) {
                    if (bytes[index] != pattern) return false;
                }
            } else {
                capacity = normalized;
            }
            ++pattern;
            std::memset(current, pattern, capacity);
            previous = current;
            ++transitions;
        }
        bool changed = false;
        if (storage.reset(0, SkAutoMalloc::kAlloc_OnShrink, &changed) != initial) return false;
    }
    return true;
}
}

int main() {
    size_t transitions = 0;
    if (!heap_owner()) return 1;
    if (!inline_owner<1>(transitions) || !inline_owner<64>(transitions) ||
        !inline_owner<65>(transitions) || !inline_owner<1024>(transitions)) return 2;
    std::printf("allocation owners: release, move, reuse, %zu inline transitions PASS; sizes=%zu/%zu/%zu\n",
                transitions, sizeof(SkAutoMalloc), sizeof(SkAutoSMalloc<64>), sizeof(SkAutoSMalloc<65>));
    return 0;
}
