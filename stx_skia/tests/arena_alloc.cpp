#include "src/core/SkArenaAlloc.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
struct Tracked {
    inline static int live = 0;
    inline static int destroyed = 0;
    int value = 31;
    Tracked() { ++live; }
    explicit Tracked(int initial) : value(initial) { ++live; }
    Tracked(const Tracked& source) : value(source.value) { ++live; }
    ~Tracked() {
        --live;
        ++destroyed;
    }
};

#ifndef STX_SKIA_REFERENCE
struct ConstructionFailure {};
struct ThrowingElement {
    inline static int live = 0;
    inline static int attempts = 0;
    inline static int failAt = 0;
    int value = 31;
    ThrowingElement() { finishConstruction(); }
    explicit ThrowingElement(int initial) : value(initial) { finishConstruction(); }
    ThrowingElement(const ThrowingElement& source) : value(source.value) {
        finishConstruction();
    }
    ThrowingElement(ThrowingElement&& source) noexcept : value(source.value) { ++live; }
    ~ThrowingElement() { --live; }
    static void finishConstruction() {
        ++attempts;
        if (attempts == failAt) throw ConstructionFailure{};
        ++live;
    }
};

ThrowingElement make_initialized_element(size_t index) {
    const int value = static_cast<int>(index) + 71;
    ThrowingElement result(value);
    return result;
}

enum class ArrayMode { Default, Value, Initialized, Copy };

bool interrupted_array_case(ArrayMode mode, int failure) {
    ThrowingElement::live = 0;
    ThrowingElement::attempts = 0;
    ThrowingElement::failAt = 0;
    {
        std::array<ThrowingElement, 4> source{};
        {
            SkArenaAlloc arena(64);
            ThrowingElement::failAt = ThrowingElement::attempts + failure;
            bool caught = false;
            try {
                switch (mode) {
                    case ArrayMode::Default:
                        arena.makeArrayDefault<ThrowingElement>(4);
                        break;
                    case ArrayMode::Value:
                        arena.makeArray<ThrowingElement>(4);
                        break;
                    case ArrayMode::Initialized:
                        arena.makeInitializedArray<ThrowingElement>(4, make_initialized_element);
                        break;
                    case ArrayMode::Copy:
                        arena.makeArrayCopy<ThrowingElement>(source);
                        break;
                    default:
                        return false;
                }
            } catch (const ConstructionFailure&) {
                caught = true;
            }
            ThrowingElement::failAt = 0;
            if (!caught || ThrowingElement::live != 4 + failure - 1) return false;
            int* following = arena.make<int>(83);
            if (*following != 83) return false;
        }
        if (ThrowingElement::live != 4) return false;
    }
    return ThrowingElement::live == 0;
}

struct NestedFailure {
    inline static int destroyed = 0;
    explicit NestedFailure(SkArenaAlloc* arena) {
        Tracked* child = (*arena).make<Tracked>(47);
        if ((*child).value != 47) throw ConstructionFailure{};
        throw ConstructionFailure{};
    }
    ~NestedFailure() { ++destroyed; }
};

bool interrupted_object_case() {
    Tracked::live = 0;
    Tracked::destroyed = 0;
    NestedFailure::destroyed = 0;
    {
        SkArenaAlloc arena(64);
        bool caught = false;
        try {
            arena.make<NestedFailure>(&arena);
        } catch (const ConstructionFailure&) {
            caught = true;
        }
        if (!caught || Tracked::live != 1 || NestedFailure::destroyed != 0) return false;
        Tracked* following = arena.make<Tracked>(53);
        if ((*following).value != 53 || Tracked::live != 2) return false;
    }
    return Tracked::live == 0 && Tracked::destroyed == 2 &&
           NestedFailure::destroyed == 0;
}

bool interrupted_construction_case() {
    if (!interrupted_object_case()) return false;
    for (int failure = 1; failure <= 4; ++failure) {
        if (!interrupted_array_case(ArrayMode::Default, failure)) return false;
        if (!interrupted_array_case(ArrayMode::Value, failure)) return false;
        if (!interrupted_array_case(ArrayMode::Initialized, failure)) return false;
        if (!interrupted_array_case(ArrayMode::Copy, failure)) return false;
    }
    return true;
}
#endif

bool heap_only_case() {
    Tracked::live = 0;
    Tracked::destroyed = 0;
    {
        SkArenaAlloc arena(64);
        uint32_t* number = arena.make<uint32_t>(73);
        Tracked* first = arena.make<Tracked>(41);
        Tracked* array = arena.makeArray<Tracked>(5);
        arena.makeArray<Tracked>(0);
        SkSpan<const uint32_t> emptyValues;
        arena.makeArrayCopy<uint32_t>(emptyValues);
        if (*number != 73 || (*first).value != 41 || Tracked::live != 6) return false;
        for (int index = 0; index < 5; ++index) {
            if (array[index].value != 31) return false;
        }
        void* aligned = arena.makeBytesAlignedTo(23, 64);
        const uintptr_t address = reinterpret_cast<uintptr_t>(aligned);
        if ((address & 63U) != 0) return false;
    }
    return Tracked::live == 0 && Tracked::destroyed == 6;
}

bool inline_case() {
    Tracked::live = 0;
    Tracked::destroyed = 0;
    {
        SkSTArenaAlloc<256> arena;
        Tracked* first = arena.make<Tracked>(17);
        Tracked* array = arena.makeArrayDefault<Tracked>(3);
        if ((*first).value != 17 || Tracked::live != 4) return false;
        for (int index = 0; index < 3; ++index) {
            if (array[index].value != 31) return false;
        }
    }
    return Tracked::live == 0 && Tracked::destroyed == 4;
}

bool reset_case() {
    Tracked::live = 0;
    Tracked::destroyed = 0;
    SkArenaAllocWithReset arena(nullptr, 0, 64);
    if (!arena.isEmpty()) return false;
    Tracked* first = arena.make<Tracked>(19);
    if ((*first).value != 19 || arena.isEmpty() || Tracked::live != 1) return false;
    arena.reset();
    if (!arena.isEmpty() || Tracked::live != 0 || Tracked::destroyed != 1) return false;
    Tracked* second = arena.make<Tracked>(29);
    if ((*second).value != 29 || arena.isEmpty() || Tracked::live != 1) return false;
    arena.reset();
    if (!arena.isEmpty() || Tracked::live != 0 || Tracked::destroyed != 2) return false;
    {
        SkSTArenaAllocWithReset<128> inlineArena;
        if (!inlineArena.isEmpty()) return false;
        Tracked* item = inlineArena.make<Tracked>(37);
        if ((*item).value != 37 || inlineArena.isEmpty()) return false;
        inlineArena.reset();
        if (!inlineArena.isEmpty() || Tracked::live != 0) return false;
    }
    return Tracked::destroyed == 3;
}
}

int main(int argc, char** argv) {
#ifndef STX_SKIA_REFERENCE
    if (argc == 2 && std::strcmp(argv[1], "--invalid-alignment") == 0) {
        SkArenaAlloc arena(64);
        arena.makeBytesAlignedTo(8, 3);
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--oversize-initial-block") == 0) {
        const size_t oversized = static_cast<size_t>(std::numeric_limits<uint32_t>::max()) + 1;
        SkArenaAlloc arena(nullptr, oversized, 64);
        return 0;
    }
#endif
    bool valid = heap_only_case() && inline_case() && reset_case();
#ifndef STX_SKIA_REFERENCE
    valid = interrupted_construction_case() && valid;
#endif
    if (!valid) {
        std::fputs("arena allocation contract mismatch\n", stderr);
        return 1;
    }
    std::puts("arena allocation: heap, inline, reset PASS");
    return 0;
}
