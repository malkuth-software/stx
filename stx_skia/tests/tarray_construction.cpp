#include "include/private/SkTArray.h"
#include "checked_size.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>

using skia_private::STArray;
using skia_private::TArray;

#ifndef STX_SKIA_REFERENCE
template <typename Array> concept BorrowFromTemporary = requires(Array&& owner) {
    std::move(owner).data();
};
template <typename Array> concept ReferenceFromTemporary = requires(Array&& owner) {
    std::move(owner).back();
};
static_assert(!BorrowFromTemporary<TArray<uint32_t>>);
static_assert(!ReferenceFromTemporary<TArray<uint32_t>>);
#endif

namespace {
struct ConstructionFailure {};
struct TrivialNonCopy {
    uint32_t value = 0;
    TrivialNonCopy() = default;
    TrivialNonCopy(const TrivialNonCopy&) = delete;
};
static_assert(std::is_trivially_copyable_v<TrivialNonCopy>);

bool trivial_copy_without_constructor() {
    std::array<TrivialNonCopy, 9> input{};
    for (size_t index = 0; index < input.size(); ++index) input[index].value = static_cast<uint32_t>(index + 21);
    TArray<TrivialNonCopy> owner(input.data(), test_sizes::to_int(input.size()));
    STArray<4, TrivialNonCopy> inlineOwner(input.data(), test_sizes::to_int(input.size()));
    for (int index = 0; index < owner.size(); ++index) {
        if (owner[index].value != input[index].value || inlineOwner[index].value != input[index].value) return false;
    }
    return true;
}
#ifndef STX_SKIA_REFERENCE
struct EqualityOnly {
    int value = 0;
    bool operator==(const EqualityOnly& right) const { return value == right.value; }
    bool operator!=(const EqualityOnly&) const = delete;
};

bool equality_only_contract() {
    TArray<EqualityOnly> left;
    TArray<EqualityOnly> right;
    left.emplace_back().value = 7;
    right.emplace_back().value = 7;
    if (!(left == right) || left != right) return false;
    right[0].value = 9;
    if (left == right || !(left != right)) return false;
    return true;
}

bool empty_end_contract() {
    TArray<uint32_t> empty;
    const TArray<uint32_t>& readOnly = empty;
    if (empty.end() != nullptr || readOnly.end() != nullptr) return false;
    if (empty.push_back_n(0) != nullptr) return false;
    if (empty.push_back_n(0, uint32_t{7}) != nullptr) return false;
    if (empty.size() != 0) return false;
    return true;
}
#endif
struct Element {
    inline static int live = 0;
    inline static int attempts = 0;
    inline static int failAt = 0;
    int value = 17;
    Element() {
        ++attempts;
        if (attempts == failAt) throw ConstructionFailure{};
        ++live;
    }
    Element(const Element& source) : value(source.value) {
        ++attempts;
        if (attempts == failAt) throw ConstructionFailure{};
        ++live;
    }
    Element(Element&& source) noexcept : value(source.value) { ++live; }
    ~Element() { --live; }
};

template <typename Array> bool successful_construction(size_t& total) {
    for (int count = 0; count <= 96; ++count) {
        std::array<uint32_t, 96> input{};
        for (int index = 0; index < count; ++index) input[index] = static_cast<uint32_t>(index * 19 + count);
        Array owner(input.data(), count);
        Array copied(owner);
        Array spanCopy(SkSpan<const uint32_t>(input.data(), count));
        if (owner.size() != count || copied.size() != count || spanCopy.size() != count) return false;
        for (int index = 0; index < count; ++index) {
            if (owner[index] != input[index] || copied[index] != input[index] || spanCopy[index] != input[index]) return false;
        }
        Array assigned;
        assigned = copied;
        assigned.reset(SkSpan<const uint32_t>(input.data(), count));
        for (int index = 0; index < count; ++index) {
            if (assigned[index] != input[index]) return false;
        }
        assigned.reset(count);
        for (int index = 0; index < count; ++index) assigned[index] = static_cast<uint32_t>(index + 9);
        uint32_t* unchangedEnd = assigned.end();
        if (assigned.push_back_n(0) != unchangedEnd) return false;
        uint32_t* appended = assigned.push_back_n(count);
        for (int index = 0; index < count; ++index) appended[index] = static_cast<uint32_t>(index + 33);
        if (assigned.size() != count * 2) return false;
        for (int index = 0; index < count; ++index) {
            if (assigned[index] != static_cast<uint32_t>(index + 9)) return false;
            if (assigned[index + count] != static_cast<uint32_t>(index + 33)) return false;
        }
        assigned.resize(static_cast<size_t>(count));
        if (assigned.size() != count) return false;
        for (int index = 0; index < count; ++index) {
            if (assigned[index] != static_cast<uint32_t>(index + 9)) return false;
            if (assigned.fromBack(count - 1 - index) != static_cast<uint32_t>(index + 9)) {
                return false;
            }
        }
        Array listed{2, 3, 5, 7};
        if (listed.size() != 4 || listed[2] != 5) return false;
        ++total;
    }
    return true;
}

bool successful_objects(size_t& total) {
    for (int count = 0; count <= 32; ++count) {
        {
            TArray<Element> source;
            source.reset(count);
            for (int index = 0; index < count; ++index) source[index].value = index + 23;
            STArray<4, Element> copied(source.data(), count);
            STArray<4, Element> assigned;
            assigned = copied;
            if (Element::live != count * 3) return false;
            for (int index = 0; index < count; ++index) {
                if (copied[index].value != index + 23 || assigned[index].value != index + 23) return false;
            }
            copied.reset(3);
            if (Element::live != count * 2 + 3) return false;
            assigned.push_back();
            if (assigned.back().value != 17) return false;
        }
        if (Element::live != 0) return false;
        ++total;
    }
    return true;
}

bool successful_relocation(size_t& total) {
    {
        TArray<Element, false> owner;
        owner.reset(6);
        for (int index = 0; index < owner.size(); ++index) owner[index].value = index + 41;
        owner.reserve_exact(64);
        owner.shrink_to_fit();
        TArray<Element, false> moved(std::move(owner));
        if (!owner.empty() || moved.size() != 6) return false;
        for (int index = 0; index < moved.size(); ++index) {
            if (moved[index].value != index + 41) return false;
        }
        ++total;
    }
    if (Element::live != 0) return false;
    {
        STArray<4, Element, false> inlineOwner;
        inlineOwner.reset(4);
        for (int index = 0; index < inlineOwner.size(); ++index) {
            inlineOwner[index].value = index + 61;
        }
        TArray<Element, false>& source = inlineOwner;
        TArray<Element, false> moved(std::move(source));
        if (!inlineOwner.empty() || moved.size() != 4) return false;
        for (int index = 0; index < moved.size(); ++index) {
            if (moved[index].value != index + 61) return false;
        }
        ++total;
    }
    if (Element::live != 0) return false;
    {
        TArray<Element, false> target;
        target.reset(2);
        STArray<4, Element, false> inlineSource;
        inlineSource.reset(4);
        for (int index = 0; index < inlineSource.size(); ++index) {
            inlineSource[index].value = index + 81;
        }
        TArray<Element, false>& source = inlineSource;
        target = std::move(source);
        if (!inlineSource.empty() || target.size() != 4) return false;
        for (int index = 0; index < target.size(); ++index) {
            if (target[index].value != index + 81) return false;
        }
        TArray<Element, false> tail;
        tail.reset(7);
        for (int index = 0; index < tail.size(); ++index) tail[index].value = index + 101;
        target.move_back(tail);
        if (!tail.empty() || target.size() != 11) return false;
        for (int index = 0; index < 4; ++index) {
            if (target[index].value != index + 81) return false;
        }
        for (int index = 0; index < 7; ++index) {
            if (target[index + 4].value != index + 101) return false;
        }
        target.removeShuffle(3);
        if (target.size() != 10 || target[3].value != 107) return false;
        ++total;
    }
    if (Element::live != 0) return false;
    {
        TArray<std::unique_ptr<int>> owner;
        owner.push_back(std::make_unique<int>(19));
        owner.push_back(std::make_unique<int>(23));
        owner.reserve_exact(64);
        owner.shrink_to_fit();
        TArray<std::unique_ptr<int>> moved(std::move(owner));
        if (!owner.empty() || moved.size() != 2) return false;
        if (*moved[0] != 19 || *moved[1] != 23) return false;
        TArray<std::unique_ptr<int>> tail;
        tail.push_back(std::make_unique<int>(29));
        moved.move_back(tail);
        if (!tail.empty() || moved.size() != 3 || *moved[2] != 29) return false;
        ++total;
    }
    return true;
}

#ifndef STX_SKIA_REFERENCE
// The original publishes live counts before construction; do not ask it to
// clean up failed construction. These cases exercise only the corrected paths.
template <typename Array> bool unused_capacity_guarded(const Array& owner) {
#ifdef SK_SANITIZE_ADDRESS
    if (owner.capacity() > owner.size()) {
        using Item = typename Array::value_type;
        const std::byte* bytes = reinterpret_cast<const std::byte*>(owner.data());
        const std::byte* firstUnused = bytes + sizeof(Item) * static_cast<size_t>(owner.size());
        if (sk_asan_address_is_poisoned(firstUnused) == 0) return false;
    }
#endif
    return true;
}

template <typename Array> bool failed_construction(size_t& total) {
    for (const int count : {3, 19}) {
        for (int failure = 1; failure <= count; ++failure) {
            TArray<Element> source;
            source.reset(count);
            const int sourceLive = Element::live;
            Element::failAt = Element::attempts + failure;
            bool caught = false;
            try {
                Array copy(source.data(), count);
            } catch (const ConstructionFailure&) {
                caught = true;
            }
            Element::failAt = 0;
            if (!caught || Element::live != sourceLive) return false;
            Array target;
            target.reset(2);
            Element::failAt = Element::attempts + failure;
            caught = false;
            try {
                target.reset(count);
            } catch (const ConstructionFailure&) {
                caught = true;
            }
            Element::failAt = 0;
            if (!caught || !target.empty() || Element::live != sourceLive ||
                !unused_capacity_guarded(target)) return false;
            target.reset(2);
            Element::failAt = Element::attempts + failure;
            caught = false;
            try {
                target.reset(SkSpan<const Element>(source.data(), count));
            } catch (const ConstructionFailure&) {
                caught = true;
            }
            Element::failAt = 0;
            if (!caught || !target.empty() || Element::live != sourceLive) return false;
            target.reset(2);
            const int beforeAppend = Element::live;
            Element::failAt = Element::attempts + failure;
            caught = false;
            try {
                target.push_back_n(count);
            } catch (const ConstructionFailure&) {
                caught = true;
            }
            Element::failAt = 0;
            if (!caught || target.size() != 2 || Element::live != beforeAppend ||
                !unused_capacity_guarded(target)) return false;
            if (target[0].value != 17 || target[1].value != 17) return false;
            target.clear();
            Array copiedSource(source.data(), count);
            Element::failAt = Element::attempts + failure;
            caught = false;
            try {
                target = copiedSource;
            } catch (const ConstructionFailure&) {
                caught = true;
            }
            Element::failAt = 0;
            if (!caught || !target.empty() || Element::live != sourceLive + count) return false;
            target.reset(1);
            Element::failAt = Element::attempts + 1;
            caught = false;
            try {
                target.push_back();
            } catch (const ConstructionFailure&) {
                caught = true;
            }
            Element::failAt = 0;
            if (!caught || target.size() != 1) return false;
            total += 6;
        }
    }
    return true;
}

struct MovingElement {
    inline static int live = 0;
    inline static int moves = 0;
    inline static int failAt = 0;
    int value = 23;
    MovingElement() { ++live; }
    MovingElement(const MovingElement&) = delete;
    MovingElement(MovingElement&& source) : value(source.value) {
        ++moves;
        if (moves == failAt) throw ConstructionFailure{};
        source.value = -1;
        ++live;
    }
    ~MovingElement() { --live; }
};

bool failed_move_relocation(size_t& total) {
    for (int failure = 1; failure <= 4; ++failure) {
        {
            TArray<MovingElement, false> owner;
            owner.reset(4);
            MovingElement::failAt = MovingElement::moves + failure;
            bool caught = false;
            try { owner.reserve_exact(64); }
            catch (const ConstructionFailure&) { caught = true; }
            MovingElement::failAt = 0;
            if (!caught || owner.size() != 4 || MovingElement::live != 4) return false;
            ++total;
        }
        if (MovingElement::live != 0) return false;
        {
            STArray<4, MovingElement, false> source;
            source.reset(4);
            TArray<MovingElement, false>& sourceBase = source;
            MovingElement::failAt = MovingElement::moves + failure;
            bool caught = false;
            try { TArray<MovingElement, false> moved(std::move(sourceBase)); }
            catch (const ConstructionFailure&) { caught = true; }
            MovingElement::failAt = 0;
            if (!caught || source.size() != 4 || MovingElement::live != 4) return false;
            ++total;
        }
        if (MovingElement::live != 0) return false;
        {
            STArray<4, MovingElement, false> source;
            source.reset(4);
            TArray<MovingElement, false> target;
            target.reset(2);
            TArray<MovingElement, false>& sourceBase = source;
            MovingElement::failAt = MovingElement::moves + failure;
            bool caught = false;
            try { target = std::move(sourceBase); }
            catch (const ConstructionFailure&) { caught = true; }
            MovingElement::failAt = 0;
            if (!caught || !target.empty() || source.size() != 4 ||
                MovingElement::live != 4) return false;
            ++total;
        }
        if (MovingElement::live != 0) return false;
        {
            TArray<MovingElement, false> owner;
            owner.reserve_exact(64);
            owner.reset(4);
            MovingElement::failAt = MovingElement::moves + failure;
            bool caught = false;
            try { owner.shrink_to_fit(); }
            catch (const ConstructionFailure&) { caught = true; }
            MovingElement::failAt = 0;
            if (!caught || owner.size() != 4 || MovingElement::live != 4) return false;
            ++total;
        }
        if (MovingElement::live != 0) return false;
        {
            TArray<MovingElement, false> owner;
            owner.reset(8);
            const int fullCount = owner.capacity();
            owner.resize(static_cast<size_t>(fullCount));
            MovingElement::failAt = MovingElement::moves + failure;
            bool caught = false;
            try { owner.emplace_back(); }
            catch (const ConstructionFailure&) { caught = true; }
            MovingElement::failAt = 0;
            if (!caught || owner.size() != fullCount ||
                MovingElement::live != fullCount || !unused_capacity_guarded(owner)) return false;
            ++total;
        }
        if (MovingElement::live != 0) return false;
        {
            TArray<MovingElement, false> target;
            target.reset(2);
            TArray<MovingElement, false> source;
            source.reset(4);
            MovingElement::failAt = MovingElement::moves + failure;
            bool caught = false;
            try { target.move_back(source); }
            catch (const ConstructionFailure&) { caught = true; }
            MovingElement::failAt = 0;
            if (!caught || target.size() != 2 || source.size() != 4 ||
                MovingElement::live != 6) return false;
            ++total;
        }
        if (MovingElement::live != 0) return false;
    }
    return true;
}

bool append_construction_contract(size_t& total) {
    for (int failure = 1; failure <= 4; ++failure) {
        TArray<Element> source;
        source.reset(4);
        TArray<Element> target;
        target.reset(2);
        const int liveBefore = Element::live;
        Element::failAt = Element::attempts + failure;
        bool caught = false;
        try { target.push_back_n(4, source[0]); }
        catch (const ConstructionFailure&) { caught = true; }
        Element::failAt = 0;
        if (!caught || target.size() != 2 || Element::live != liveBefore ||
            !unused_capacity_guarded(target)) return false;
        ++total;

        Element::failAt = Element::attempts + failure;
        caught = false;
        try { target.push_back_n(4, source.data()); }
        catch (const ConstructionFailure&) { caught = true; }
        Element::failAt = 0;
        if (!caught || target.size() != 2 || Element::live != liveBefore ||
            !unused_capacity_guarded(target)) return false;
        ++total;
    }
    if (Element::live != 0) return false;
    for (int failure = 1; failure <= 4; ++failure) {
        TArray<MovingElement, false> source;
        source.reset(4);
        TArray<MovingElement, false> target;
        target.reset(2);
        MovingElement::failAt = MovingElement::moves + failure;
        bool caught = false;
        try { target.move_back_n(4, source.data()); }
        catch (const ConstructionFailure&) { caught = true; }
        MovingElement::failAt = 0;
        if (!caught || target.size() != 2 || source.size() != 4 ||
            MovingElement::live != 6) return false;
        ++total;
    }
    if (MovingElement::live != 0) return false;
    {
        TArray<Element> owner;
        owner.reset(8);
        for (int index = 0; index < 8; ++index) owner[index].value = index + 51;
        owner.push_back_n(4, owner[3]);
        if (owner.size() != 12) return false;
        for (int index = 0; index < 4; ++index) {
            if (owner[index + 8].value != 54) return false;
        }
        ++total;
    }
    {
        TArray<Element> owner;
        owner.reset(8);
        for (int index = 0; index < 8; ++index) owner[index].value = index + 71;
        owner.push_back_n(4, owner.data() + 2);
        if (owner.size() != 12) return false;
        for (int index = 0; index < 4; ++index) {
            if (owner[index + 8].value != index + 73) return false;
        }
        ++total;
    }
    {
        TArray<Element> owner;
        owner.reserve_exact(16);
        owner.reset(8);
        const int beforeCopies = Element::attempts;
        owner.push_back_n(4, owner[3]);
        if (owner.size() != 12 || Element::attempts - beforeCopies != 4) return false;
        for (int index = 8; index < 12; ++index) {
            if (owner[index].value != 17) return false;
        }
        ++total;
    }
    {
        TArray<Element> owner;
        owner.reserve_exact(16);
        owner.reset(8);
        const int beforeCopies = Element::attempts;
        owner.push_back_n(4, owner.data() + 2);
        if (owner.size() != 12 || Element::attempts - beforeCopies != 4) return false;
        for (int index = 8; index < 12; ++index) {
            if (owner[index].value != 17) return false;
        }
        ++total;
    }
    const bool valid = Element::live == 0 && MovingElement::live == 0;
    return valid;
}

bool move_and_swap_contract() {
    {
        TArray<uint32_t> source;
        source.reserve_exact(64);
        source.reset(4);
        const int reserved = source.capacity();
        TArray<uint32_t> moved(std::move(source));
        if (!source.empty() || moved.size() != 4 || moved.capacity() != reserved) return false;
        if (!unused_capacity_guarded(moved)) return false;
    }
    {
        TArray<Element> spare;
        spare.reset(1);
        spare.reserve_exact(32);
        TArray<Element> packed;
        packed.reset(8);
        spare.swap(packed);
        if (spare.size() != 8 || packed.size() != 1) return false;
        packed.push_back();
        if (packed.size() != 2 || Element::live != 10) return false;
        if (!unused_capacity_guarded(packed)) return false;
    }
    const bool valid = Element::live == 0;
    return valid;
}

bool inline_swap_contract() {
    Element::failAt = 0;
    {
        STArray<4, Element, false> left;
        STArray<4, Element, false> right;
        left.reset(2);
        right.reset(3);
        left[0].value = 11;
        left[1].value = 13;
        right[0].value = 17;
        right[1].value = 19;
        right[2].value = 23;
        left.swap(right);
        if (left.size() != 3 || right.size() != 2 || Element::live != 5) return false;
        if (left[0].value != 17 || left[1].value != 19 || left[2].value != 23) return false;
        if (right[0].value != 11 || right[1].value != 13) return false;
        left.swap(left);
        if (left.size() != 3 || left[2].value != 23 || Element::live != 5) return false;
    }
    if (Element::live != 0) return false;
    {
        TArray<Element, false> heap;
        STArray<4, Element, false> inlineOwner;
        heap.reset(7);
        inlineOwner.reset(2);
        for (int index = 0; index < 7; ++index) heap[index].value = index + 31;
        inlineOwner[0].value = 47;
        inlineOwner[1].value = 53;
        heap.swap(inlineOwner);
        if (heap.size() != 2 || inlineOwner.size() != 7 || Element::live != 9) return false;
        if (heap[0].value != 47 || heap[1].value != 53) return false;
        for (int index = 0; index < 7; ++index) {
            if (inlineOwner[index].value != index + 31) return false;
        }
        heap.push_back();
        inlineOwner.push_back();
        if (Element::live != 11) return false;
    }
    return Element::live == 0;
}

template <typename Array> bool alias_reset(size_t& total) {
    for (int count = 1; count <= 32; ++count) {
        for (int offset = 0; offset < count; ++offset) {
            Array owner;
            owner.reset(count);
            for (int index = 0; index < count; ++index) owner[index].value = index + 51;
            const int retained = count - offset;
            owner.reset(SkSpan<const Element>(owner.data() + offset, retained));
            if (owner.size() != retained) return false;
            for (int index = 0; index < retained; ++index) {
                if (owner[index].value != index + offset + 51) return false;
            }
            ++total;
        }
    }
    return true;
}
template <typename Array> bool trivial_alias_reset(size_t& total) {
    for (int count = 1; count <= 32; ++count) {
        for (int offset = 0; offset < count; ++offset) {
            Array owner;
            owner.reset(count);
            for (int index = 0; index < count; ++index) owner[index] = static_cast<uint32_t>(index + 51);
            const int retained = count - offset;
            uint32_t* originalStorage = owner.data();
            owner.reset(SkSpan<const uint32_t>(owner.data() + offset, retained));
            if (owner.data() != originalStorage || owner.size() != retained) return false;
            for (int index = 0; index < retained; ++index) {
                if (owner[index] != static_cast<uint32_t>(index + offset + 51)) return false;
            }
            ++total;
        }
    }
    return true;
}

#endif

uint64_t benchmark_sink = 0;
void benchmark() {
    for (const int count : {16, 256, 4096}) {
        TArray<uint32_t> input;
        input.reset(count);
        for (int index = 0; index < count; ++index) input[index] = static_cast<uint32_t>(index + 1);
        TArray<uint32_t> target(count);
        const int iterations = 64000000 / count;
        for (int mode = 0; mode < 2; ++mode) {
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            uint64_t checksum = 0;
            for (int iteration = 0; iteration < iterations; ++iteration) {
                input[0] = static_cast<uint32_t>(iteration);
                if (mode == 0) {
                    target = input;
                } else {
                    target.reset(SkSpan<const uint32_t>(input.data(), count));
                }
                checksum += target[iteration % count];
            }
            const std::chrono::steady_clock::time_point finish = std::chrono::steady_clock::now();
            const std::chrono::duration<double> elapsed = finish - start;
            benchmark_sink += checksum;
            std::printf("%d,%d,%d,%.9f,%llu\n", count, mode, iterations, elapsed.count(),
                        static_cast<unsigned long long>(checksum));
        }
    }
}
}

int main(int argc, char** argv) {
    size_t ordinary = 0;
    bool valid = trivial_copy_without_constructor();
#ifndef STX_SKIA_REFERENCE
    valid = empty_end_contract() && valid;
    valid = equality_only_contract() && valid;
#endif
    valid = successful_construction<TArray<uint32_t>>(ordinary) && valid;
    valid = successful_construction<STArray<1, uint32_t>>(ordinary) && valid;
    valid = successful_construction<STArray<4, uint32_t>>(ordinary) && valid;
    valid = successful_construction<STArray<64, uint32_t>>(ordinary) && valid;
    valid = successful_objects(ordinary) && valid;
    valid = successful_relocation(ordinary) && valid;
#ifndef STX_SKIA_REFERENCE
    size_t failures = 0;
    size_t aliases = 0;
    size_t interruptedMoves = 0;
    size_t appendCases = 0;
    valid = failed_construction<TArray<Element>>(failures) && valid;
    valid = failed_construction<STArray<4, Element>>(failures) && valid;
    valid = failed_move_relocation(interruptedMoves) && valid;
    valid = append_construction_contract(appendCases) && valid;
    valid = move_and_swap_contract() && valid;
    valid = inline_swap_contract() && valid;
    valid = alias_reset<TArray<Element>>(aliases) && valid;
    valid = alias_reset<STArray<4, Element>>(aliases) && valid;
    valid = trivial_alias_reset<TArray<uint32_t>>(aliases) && valid;
    valid = trivial_alias_reset<STArray<4, uint32_t>>(aliases) && valid;
    if (valid) std::printf("candidate: %zu construction failures, %zu interrupted moves, %zu append cases, %zu alias resets PASS\n",
                           failures, interruptedMoves, appendCases, aliases);
#endif
    if (!valid || Element::live != 0) {
        std::fputs("TArray construction: contract mismatch\n", stderr);
        return 1;
    }
    std::printf("TArray construction: %zu ordinary schedules PASS; heap=%zu inline=%zu bytes\n",
                ordinary, sizeof(TArray<uint32_t>), sizeof(STArray<4, uint32_t>));
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) benchmark();
    return 0;
}
