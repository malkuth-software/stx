#include "include/private/SkAlignedStorage.h"
#include "include/private/SkTArray.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

using skia_private::STArray;
using skia_private::TArray;

static_assert(!std::is_copy_constructible_v<SkAlignedSTStorage<4, int>>);
static_assert(!std::is_move_constructible_v<SkAlignedSTStorage<4, int>>);
#ifndef STX_SKIA_REFERENCE
template <typename T> concept TemporaryRawBorrow = requires(T&& owner) { std::move(owner).get(); };
template <typename T> concept TemporaryByteBorrow = requires(T&& owner) { std::move(owner).data(); };
static_assert(!TemporaryRawBorrow<SkAlignedSTStorage<4, int>>);
static_assert(!TemporaryByteBorrow<SkAlignedSTStorage<4, int>>);
static_assert(!TemporaryRawBorrow<const SkAlignedSTStorage<4, int>>);
static_assert(!TemporaryByteBorrow<const SkAlignedSTStorage<4, int>>);
#endif

namespace {
struct alignas(64) Wide {
    uint64_t value = 0;
};
struct Tracked {
    inline static int live = 0;
    inline static int constructed = 0;
    inline static int destroyed = 0;
    int value = 0;
    Tracked() { ++live; ++constructed; }
    explicit Tracked(int input) : value(input) { ++live; ++constructed; }
    Tracked(const Tracked& source) : value(source.value) { ++live; ++constructed; }
    Tracked(Tracked&& source) noexcept : value(source.value) {
        source.value = -1;
        ++live;
        ++constructed;
    }
    ~Tracked() { --live; ++destroyed; }
};

template <int N, typename T> bool raw_storage(size_t& total) {
    using Storage = SkAlignedSTStorage<N, T>;
    static_assert(sizeof(Storage) == sizeof(T) * N);
    static_assert(alignof(Storage) == alignof(T));
    Storage storage;
    const Storage& observed = storage;
    if (storage.size() != sizeof(T) * N) return false;
    if (storage.get() != storage.data() || observed.get() != storage.get()) return false;
    if (observed.data() != storage.data()) return false;
    const uintptr_t address = reinterpret_cast<uintptr_t>(storage.get());
    if (address % alignof(T) != 0) return false;
    // Each slot is the sizeof(T) bytes at index * sizeof(T), taken from a view
    // that carries the storage's length.
    const std::span<std::byte> bytes(storage.data(), storage.size());
    // Raw storage deliberately performs no construction. Each round reuses it
    // only after explicitly ending every object created in the preceding round.
    for (int pass = 0; pass < 32; ++pass) {
        for (int index = 0; index < N; ++index) {
            std::byte* location = bytes.subspan(static_cast<size_t>(index) * sizeof(T), sizeof(T)).data();
            T* element = new (location) T{};
            if constexpr (std::is_integral_v<T>) {
                *element = static_cast<T>(index + pass);
            } else {
                (*element).value = index + pass;
            }
        }
        bool valid = true;
        for (int index = 0; index < N; ++index) {
            std::byte* location = bytes.subspan(static_cast<size_t>(index) * sizeof(T), sizeof(T)).data();
            T* element = std::launder(reinterpret_cast<T*>(location));
            if constexpr (std::is_integral_v<T>) {
                if (*element != static_cast<T>(index + pass)) valid = false;
            } else {
                if ((*element).value != index + pass) valid = false;
            }
            std::destroy_at(element);
            ++total;
        }
        if (!valid) return false;
    }
    return true;
}

template <int N> bool scalar_transitions(size_t& total) {
    constexpr int inlineCount = static_cast<int>(SkContainerAllocator::RoundUp<uint32_t>(N));
    const std::array<int, 8> counts{0, 1, inlineCount - 1, inlineCount,
                                  inlineCount + 1, inlineCount * 2, 3, 0};
    for (const int firstCount : counts) {
        for (const int secondCount : counts) {
            STArray<N, uint32_t> owner;
            uint32_t* inlineAddress = owner.data();
            owner.reset(firstCount);
            if (firstCount <= inlineCount && owner.data() != inlineAddress) return false;
            for (int index = 0; index < firstCount; ++index) owner[index] = static_cast<uint32_t>(index + 7);
            STArray<N, uint32_t> copied(owner);
            TArray<uint32_t> moved(std::move(owner));
            if (!owner.empty() || moved.size() != firstCount || copied.size() != firstCount) return false;
            for (int index = 0; index < firstCount; ++index) {
                if (moved[index] != static_cast<uint32_t>(index + 7) || copied[index] != moved[index]) return false;
            }
            owner.reset(secondCount);
            for (int index = 0; index < secondCount; ++index) owner[index] = static_cast<uint32_t>(index + 31);
            owner.swap(copied);
            if (owner.size() != firstCount || copied.size() != secondCount) return false;
            for (int index = 0; index < firstCount; ++index) {
                if (owner[index] != static_cast<uint32_t>(index + 7)) return false;
            }
            for (int index = 0; index < secondCount; ++index) {
                if (copied[index] != static_cast<uint32_t>(index + 31)) return false;
            }
            owner.clear();
            owner.shrink_to_fit();
            copied.clear();
            copied.shrink_to_fit();
            if (!owner.empty() || !copied.empty()) return false;
            ++total;
        }
    }
    return true;
}

bool object_transitions(size_t& total) {
    for (int count = 0; count <= 32; ++count) {
        {
            STArray<4, Tracked, false> source;
            for (int index = 0; index < count; ++index) source.emplace_back(index + 11);
            STArray<4, Tracked, false> copied(source);
            STArray<4, Tracked, false> moved(std::move(source));
            if (!source.empty() || Tracked::live != count * 2) return false;
            for (int index = 0; index < count; ++index) {
                if (copied[index].value != index + 11 || moved[index].value != index + 11) return false;
            }
            copied.clear();
            if (Tracked::live != count) return false;
            if (count != 0) moved.pop_back();
            moved.shrink_to_fit();
        }
        if (Tracked::live != 0 || Tracked::constructed != Tracked::destroyed) return false;
        ++total;
    }
    return true;
}
}

int main() {
    size_t objects = 0;
    size_t transitions = 0;
    bool valid = raw_storage<1, uint8_t>(objects);
    valid = raw_storage<7, uint32_t>(objects) && valid;
    valid = raw_storage<16, uint64_t>(objects) && valid;
    valid = raw_storage<4, Wide>(objects) && valid;
    valid = raw_storage<8, Tracked>(objects) && valid;
    valid = scalar_transitions<1>(transitions) && valid;
    valid = scalar_transitions<4>(transitions) && valid;
    valid = scalar_transitions<16>(transitions) && valid;
    valid = scalar_transitions<64>(transitions) && valid;
    valid = object_transitions(transitions) && valid;
    if (Tracked::live != 0 || Tracked::constructed != Tracked::destroyed) valid = false;
    if (!valid) {
        std::fputs("aligned storage: contract mismatch\n", stderr);
        return 1;
    }
    std::printf("aligned storage: %zu object lifetimes, %zu container transitions PASS; "
                "inline arrays=%zu/%zu/%zu bytes\n", objects, transitions,
                sizeof(STArray<1, uint32_t>), sizeof(STArray<4, uint32_t>),
                sizeof(STArray<64, uint32_t>));
    return 0;
}
