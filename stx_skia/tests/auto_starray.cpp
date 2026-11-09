#include "include/private/SkTemplates.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

using skia_private::AutoSTArray;
static_assert(!std::is_copy_constructible_v<AutoSTArray<4, int>>);
static_assert(!std::is_move_assignable_v<AutoSTArray<4, int>>);
#ifndef STX_SKIA_REFERENCE
template <typename T> concept TemporaryConstructedBorrow = requires(T&& owner) { std::move(owner).get(); };
static_assert(!TemporaryConstructedBorrow<AutoSTArray<4, int>>);
using ConstInlineElement = decltype(std::declval<const AutoSTArray<4, int>&>()[0]);
static_assert(std::is_same_v<ConstInlineElement, const int&>);
#endif

namespace {
struct ConstructionFailure {};
struct Element {
    inline static int live = 0;
    inline static int nextIdentity = 0;
    inline static int attempts = 0;
    inline static int moves = 0;
    inline static int failAt = 0;
    inline static int failMoveAt = 0;
    inline static std::array<int, 4096> destruction{};
    inline static size_t destroyed = 0;
    int identity = 0;

    Element() {
        ++attempts;
        if (attempts == failAt) throw ConstructionFailure{};
        identity = ++nextIdentity;
        ++live;
    }
    Element(Element&& source) {
        ++moves;
        if (moves == failMoveAt) throw ConstructionFailure{};
        identity = source.identity;
        source.identity = -source.identity;
        ++live;
    }
    ~Element() {
        destruction[destroyed] = identity;
        ++destroyed;
        --live;
    }
};

template <int Inline> bool scalar_transitions(size_t& total) {
    using Storage = AutoSTArray<Inline, uint32_t>;
    const int capacity = Storage::kCount;
    const std::array<int, 12> counts{0, 1, capacity, capacity, capacity + 1, capacity * 2,
                                    2, 0, capacity + 7, 1, capacity, 0};
    Storage storage{};
    for (int pass = 0; pass < 16; ++pass) {
        for (const int count : counts) {
            uint32_t* previous = storage.get();
            const int previousCount = storage.count();
            storage.reset(count);
            if (storage.count() != count || storage.size() != static_cast<size_t>(count)) return false;
            if ((storage.get() == nullptr) != (count == 0)) return false;
            if (count != 0 && count == previousCount && storage.get() != previous) return false;
            if (storage.begin() != storage.get() || storage.data() != storage.get()) return false;
            const Storage& observation = storage;
            if (observation.begin() != storage.begin() || observation.end() != storage.end()) return false;
            for (int index = 0; index < count; ++index) storage[index] = static_cast<uint32_t>(index + pass);
            int index = 0;
            for (const uint32_t value : observation) {
                if (value != static_cast<uint32_t>(index + pass)) return false;
                ++index;
            }
            if (index != count) return false;
            ++total;
        }
    }
    storage.reset(capacity + 7);
    uint32_t* retained = storage.get();
    for (int index = 0; index < storage.count(); ++index) storage[index] = static_cast<uint32_t>(index);
    storage.trimTo(capacity + 10);
    if (storage.count() != capacity + 7) return false;
    storage.trimTo(2);
    if (storage.count() != 2 || storage.get() != retained || storage[1] != 1) return false;
    storage.trimTo(0);
    if (storage.count() != 0 || storage.begin() != retained || storage.end() != retained) return false;
#ifndef STX_SKIA_REFERENCE
    storage.reset(0);
    if (storage.get() != nullptr) return false;
#else
    // Avoid the original zero-count reset retaining this heap block at destruction.
    storage.reset(1);
#endif
    return true;
}

bool destruction_order() {
    const int count = AutoSTArray<2, Element>::kCount + 3;
    const int first = Element::nextIdentity + 1;
    const size_t start = Element::destroyed;
    {
        AutoSTArray<2, Element> storage(count);
        Element* previous = storage.get();
        storage.reset(count);
        if (storage.get() != previous || Element::live != count) return false;
        for (int index = 0; index < count; ++index) {
            if (Element::destruction[start + static_cast<size_t>(index)] != first + count - index - 1) return false;
        }
        const size_t tailStart = Element::destroyed;
        storage.trimTo(2);
        if (Element::live != 2 || storage.count() != 2) return false;
        for (int index = 0; index < count - 2; ++index) {
            if (Element::destruction[tailStart + static_cast<size_t>(index)] != first + count * 2 - index - 1) return false;
        }
        storage.reset(1);
        if (Element::live != 1) return false;
    }
    return Element::live == 0;
}

bool ownership_moves() {
    using Storage = AutoSTArray<2, Element>;
    const std::array<int, 3> counts{0, 2, Storage::kCount + 4};
    for (const int count : counts) {
        {
            Storage source(count);
            Element* previous = source.get();
            const int movesBefore = Element::moves;
            Storage destination(std::move(source));
            if (destination.count() != count) return false;
            if (count > Storage::kCount) {
                if (destination.get() != previous || Element::moves != movesBefore) return false;
            } else if (count != 0) {
                if (destination.get() == previous || Element::moves != movesBefore + count) return false;
            }
#ifndef STX_SKIA_REFERENCE
            if (source.count() != 0 || source.get() != nullptr || Element::live != count) return false;
#endif
            source.reset(1);
            if (source.count() != 1 || Element::live != count + 1) return false;
        }
        if (Element::live != 0) return false;
    }
#ifndef STX_SKIA_REFERENCE
    // Empty live extent can still own a heap block after trim; move/destruction
    // must release that block without trying to move or destroy absent elements.
    Storage trimmed(Storage::kCount + 4);
    trimmed.trimTo(0);
    Element* allocation = trimmed.get();
    Storage moved(std::move(trimmed));
    if (trimmed.get() != nullptr || moved.get() != allocation || moved.count() != 0) return false;
#endif
    return Element::live == 0;
}

#ifndef STX_SKIA_REFERENCE
bool failed_construction() {
    using Storage = AutoSTArray<2, Element>;
    const std::array<int, 2> counts{2, Storage::kCount + 4};
    for (const int count : counts) {
        Element::failAt = Element::attempts + 2;
        bool failed = false;
        try {
            Storage storage(count);
        } catch (const ConstructionFailure&) {
            failed = true;
        }
        Element::failAt = 0;
        if (!failed || Element::live != 0) return false;
        Storage storage(2);
        Element::failAt = Element::attempts + 2;
        failed = false;
        try {
            storage.reset(count);
        } catch (const ConstructionFailure&) {
            failed = true;
        }
        Element::failAt = 0;
        if (!failed || Element::live != 0 || storage.count() != 0 || storage.get() != nullptr) return false;
        storage.reset(1);
        if (Element::live != 1) return false;
    }
    if (Element::live != 0) return false;
    {
        Storage source(2);
        Element::failMoveAt = Element::moves + 2;
        bool failed = false;
        try {
            Storage destination(std::move(source));
        } catch (const ConstructionFailure&) {
            failed = true;
        }
        Element::failMoveAt = 0;
        if (!failed || Element::live != 2 || source.count() != 2) return false;
    }
    return Element::live == 0;
}
#endif
}

int main() {
    size_t transitions = 0;
    if (!scalar_transitions<1>(transitions) || !scalar_transitions<4>(transitions) ||
        !scalar_transitions<32>(transitions) || !destruction_order() || !ownership_moves()) return 1;
#ifndef STX_SKIA_REFERENCE
    if (!failed_construction()) return 2;
#endif
    std::printf("constructed inline arrays: %zu reset transitions, trimming, reverse destruction and moves PASS; sizes=%zu/%zu/%zu\n",
                transitions, sizeof(AutoSTArray<1, uint32_t>), sizeof(AutoSTArray<4, uint32_t>),
                sizeof(AutoSTArray<32, uint32_t>));
    return 0;
}
