#include "include/private/SkTemplates.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

using skia_private::AutoTArray;
static_assert(!std::is_copy_constructible_v<AutoTArray<int>>);
#ifndef STX_SKIA_REFERENCE
template <typename T> concept TemporaryArrayBorrow = requires(T&& owner) { std::move(owner).get(); };
static_assert(!TemporaryArrayBorrow<AutoTArray<int>>);
static_assert(std::is_nothrow_move_constructible_v<AutoTArray<int>>);
using ConstArrayElement = decltype(std::declval<const AutoTArray<int>&>()[0]);
static_assert(std::is_same_v<ConstArrayElement, const int&>);
#endif

namespace {
struct ConstructionFailure {};
struct Element {
    inline static int attempts = 0;
    inline static int live = 0;
    inline static int destroyed = 0;
    inline static int failAt = 0;
    inline static int peak = 0;
    int identity = 0;

    Element() {
        ++attempts;
        if (attempts == failAt) throw ConstructionFailure{};
        identity = attempts;
        ++live;
        if (live > peak) peak = live;
    }
    ~Element() {
        --live;
        ++destroyed;
    }
};

bool constructed_lifetimes() {
    {
        AutoTArray<Element> values(size_t{4});
        if (Element::live != 4 || values.size() != 4) return false;
        values.reset(4);
        if (Element::live != 4 || Element::destroyed != 4 || Element::peak != 8) return false;
        if (values[0].identity != 5 || values[3].identity != 8) return false;
        Element* preserved = values.data();
        Element::failAt = Element::attempts + 4;
        bool failed = false;
        try {
            values.reset(9);
        } catch (const ConstructionFailure&) {
            failed = true;
        }
        Element::failAt = 0;
        if (!failed || values.data() != preserved || values.size() != 4) return false;
        if (Element::live != 4 || Element::destroyed != 7 || values[0].identity != 5) return false;
        AutoTArray<Element> moved(std::move(values));
        if (!values.empty() || values.data() != nullptr || moved.data() != preserved) return false;
        AutoTArray<Element> destination(2);
        destination = std::move(moved);
        if (!moved.empty() || moved.data() != nullptr || destination.data() != preserved) return false;
        if (Element::live != 4 || Element::destroyed != 9) return false;
        AutoTArray<Element>& same = destination;
        destination = std::move(same);
        if (destination.data() != preserved || Element::live != 4) return false;
        destination.reset();
        if (!destination.empty() || destination.get() != nullptr || Element::live != 0) return false;
        values.reset(3);
        if (values.size() != 3 || Element::live != 3) return false;
    }
    return Element::live == 0 && Element::destroyed == 16;
}

bool scalar_arrays(size_t& transitions) {
    const std::array<size_t, 12> counts{0, 1, 31, 32, 33, 4, 4, 0, 257, 1024, 7, 0};
    AutoTArray<uint32_t> values{};
    for (size_t pass = 0; pass < 16; ++pass) {
        for (const size_t count : counts) {
            values.reset(count);
            if (values.size() != count || values.size_bytes() != count * sizeof(uint32_t)) return false;
            if (values.empty() != (count == 0)) return false;
            if ((values.data() == nullptr) != (count == 0)) return false;
            const AutoTArray<uint32_t>& observation = values;
            if (values.get() != values.data() || values.begin() != values.data()) return false;
            if (observation.get() != observation.data() || observation.begin() != values.begin()) return false;
            if (count == 0) {
                if (values.end() != nullptr || observation.end() != nullptr) return false;
            } else {
                if (values.end() - values.begin() != static_cast<ptrdiff_t>(count)) return false;
                if (observation.end() != values.end()) return false;
                for (size_t index = 0; index < count; ++index) {
                    values[index] = static_cast<uint32_t>(index * 17 + pass);
                }
                size_t index = 0;
                for (const uint32_t value : observation) {
                    if (value != static_cast<uint32_t>(index * 17 + pass)) return false;
                    ++index;
                }
                if (index != count) return false;
            }
            ++transitions;
        }
    }
    return true;
}
}

int main() {
    size_t transitions = 0;
    if (!constructed_lifetimes() || !scalar_arrays(transitions)) return 1;
    std::printf("constructed arrays: %zu reset transitions, element cleanup, failed replacement, moves PASS; owner=%zu bytes\n",
                transitions, sizeof(AutoTArray<uint32_t>));
    return 0;
}
