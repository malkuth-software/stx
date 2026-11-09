#include "include/private/SkTemplates.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

using skia_private::AutoSTMalloc;
static_assert(!std::is_copy_constructible_v<AutoSTMalloc<4, uint32_t>>);
static_assert(!std::is_move_assignable_v<AutoSTMalloc<4, uint32_t>>);
#ifndef STX_SKIA_REFERENCE
template <typename T> concept TemporaryInlineBorrow = requires(T&& owner) { std::move(owner).get(); };
static_assert(!TemporaryInlineBorrow<AutoSTMalloc<4, uint32_t>>);
static_assert(!std::is_convertible_v<AutoSTMalloc<4, uint32_t>&&, uint32_t*>);
using InlineObservation = decltype(std::declval<const AutoSTMalloc<4, uint32_t>&>().get());
static_assert(std::is_same_v<InlineObservation, const uint32_t*>);
#endif

namespace {
struct alignas(std::max_align_t) AlignedRecord {
    uint64_t first;
    uint64_t second;
};

bool has_pattern(const void* storage, size_t bytes, uint8_t pattern) {
    const uint8_t* data = static_cast<const uint8_t*>(storage);
    for (size_t index = 0; index < bytes; ++index) {
        if (data[index] != pattern) return false;
    }
    return true;
}

template <size_t Inline, typename T> bool transitions(size_t& total) {
    using Storage = AutoSTMalloc<Inline, T>;
    const size_t capacity = Storage::kCount;
    const std::array<size_t, 16> counts{
        1, capacity, capacity + 1, capacity * 2, 1, 0, 0, capacity,
        capacity * 4, capacity, 1, capacity + 7, 2, capacity, 0, 0};
    Storage storage{};
    T* inlineAddress = storage.get();
    if (inlineAddress == nullptr || reinterpret_cast<uintptr_t>(inlineAddress) % alignof(T) != 0) return false;
    for (size_t pass = 0; pass < 8; ++pass) {
        // Re-establish inline state at each pass; realloc shrink alone keeps heap storage.
        if (storage.reset(capacity) != inlineAddress) return false;
        size_t oldCount = capacity;
        bool isInline = true;
        std::memset(storage.get(), 0x5a, capacity * sizeof(T));
        for (const size_t count : counts) {
            storage.realloc(count);
            if (count == 0) isInline = false;
            else if (count > capacity) isInline = false;
            T* current = storage.get();
            if ((current == nullptr) != (count == 0)) return false;
            if ((current == inlineAddress) != isInline) return false;
            const size_t preserved = count < oldCount ? count : oldCount;
            if (!has_pattern(current, preserved * sizeof(T), 0x5a)) return false;
            if (count != 0) {
                if (reinterpret_cast<uintptr_t>(current) % alignof(T) != 0) return false;
                std::memset(current, 0x5a, count * sizeof(T));
                T* writable = storage;
                const Storage& observation = storage;
                const T* readable = observation;
                if (writable != current || readable != observation.data()) return false;
                if (&storage[static_cast<int>(count - 1)] != current + count - 1) return false;
            }
            oldCount = count;
            ++total;
        }
        for (const size_t count : counts) {
            T* current = storage.reset(count);
            if ((current == nullptr) != (count == 0)) return false;
            if ((current == inlineAddress) != (count != 0 && count <= capacity)) return false;
            if (count != 0) std::memset(current, 0x37, count * sizeof(T));
            ++total;
        }
    }
    Storage empty(size_t{0});
    if (empty.get() != nullptr) return false;
    return true;
}

template <size_t Inline, typename T> bool moves() {
    using Storage = AutoSTMalloc<Inline, T>;
    const size_t capacity = Storage::kCount;
    const std::array<size_t, 3> counts{0, capacity, capacity + 7};
    for (const size_t count : counts) {
        Storage source(count);
        T* previous = source.get();
        if (count != 0) std::memset(previous, 0x69, count * sizeof(T));
        Storage destination(std::move(source));
        if (count == 0 && destination.get() != nullptr) return false;
        if (count == capacity && destination.get() == previous) return false;
        if (count > capacity && destination.get() != previous) return false;
        if (!has_pattern(destination.get(), count * sizeof(T), 0x69)) return false;
#ifndef STX_SKIA_REFERENCE
        if (source.get() != nullptr) return false;
#endif
        if (source.reset(capacity) == nullptr) return false;
        std::memset(source.get(), 0x21, capacity * sizeof(T));
        if (!has_pattern(destination.get(), count * sizeof(T), 0x69)) return false;
    }
    // Only initialized bytes are observed after moving partially filled inline storage.
    Storage partial{};
    std::memset(partial.get(), 0x17, sizeof(T));
    Storage moved(std::move(partial));
    return has_pattern(moved.get(), sizeof(T), 0x17);
}
}

int main() {
    size_t total = 0;
    if (!transitions<1, uint8_t>(total) || !transitions<3, uint32_t>(total) ||
        !transitions<4, double>(total) || !transitions<2, AlignedRecord>(total)) return 1;
    if (!moves<1, uint8_t>() || !moves<3, uint32_t>() || !moves<4, double>() ||
        !moves<2, AlignedRecord>()) return 2;
    std::printf("inline scratch: %zu transitions, copy/transfer, alignment and preservation PASS; sizes=%zu/%zu/%zu/%zu\n",
                total, sizeof(AutoSTMalloc<1, uint8_t>), sizeof(AutoSTMalloc<3, uint32_t>),
                sizeof(AutoSTMalloc<4, double>), sizeof(AutoSTMalloc<2, AlignedRecord>));
    return 0;
}
