#include "src/core/SkTBlockList.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

namespace {
bool ordinary_case() {
    SkTBlockList<int, 2> left;
    left.push_back(11);
    left.push_back(13);
    left.push_back(17);
    if (left.count() != 3 || left.item(0) != 11 || left.item(2) != 17) return false;
    left.pop_back();
    if (left.count() != 2 || left.back() != 13) return false;

    SkTBlockList<int, 3> right;
    right.push_back(19);
    right.push_back(23);
    right.push_back(29);
    right.push_back(31);
    left.concat(std::move(right));
    if (!right.empty() || left.count() != 6) return false;
    const int expected[6] = {11, 13, 19, 23, 29, 31};
    for (int index = 0; index < 6; ++index) {
        if (left.item(index) != expected[index]) return false;
    }
    left.reset();
    return left.empty();
}

struct ConstructionFailure {};
struct Element {
    inline static int live = 0;
    inline static int attempts = 0;
    inline static int failAt = 0;
    int value = 0;
    explicit Element(int initial) : value(initial) { finishConstruction(); }
    Element(const Element& source) : value(source.value) { finishConstruction(); }
    Element(Element&& source) : value(source.value) {
        finishConstruction();
        source.value = -1;
    }
    ~Element() { --live; }
    static void finishConstruction() {
        ++attempts;
        if (attempts == failAt) throw ConstructionFailure{};
        ++live;
    }
};

bool nontrivial_case() {
    Element::live = 0;
    Element::attempts = 0;
    Element::failAt = 0;
    {
        SkTBlockList<Element, 2> left;
        left.emplace_back(10);
        SkTBlockList<Element, 4> right;
        right.emplace_back(20);
        right.emplace_back(30);
        right.emplace_back(40);
        left.concat(std::move(right));
        if (!right.empty() || left.count() != 4 || Element::live != 4) return false;
        const int expected[4] = {10, 20, 30, 40};
        for (int index = 0; index < 4; ++index) {
            if (left.item(index).value != expected[index]) return false;
        }
    }
    return Element::live == 0;
}

#ifndef STX_SKIA_REFERENCE
bool move_only_case() {
    SkTBlockList<std::unique_ptr<int>, 2> target;
    SkTBlockList<std::unique_ptr<int>, 2> source;
    source.push_back(std::make_unique<int>(37));
    target.concat(std::move(source));
    if (!source.empty() || target.count() != 1) return false;
    return *target.front() == 37;
}

#ifdef SK_SANITIZE_ADDRESS
bool shrunk_range_is_poisoned() {
    SkSBlockAllocator<256> allocator;
    SkBlockAllocator::ByteRange range = (*allocator.allocator()).allocate<8>(64);
    // The retained and released ranges meet at an eight-byte ASan shadow boundary.
    const int shortenedEnd = range.fEnd - 32;
    void* retainedByte = (*range.fBlock).ptr(shortenedEnd - 1);
    void* releasedByte = (*range.fBlock).ptr(shortenedEnd);
    if (sk_asan_address_is_poisoned(releasedByte) != 0) return false;
    const bool shrunk = (*range.fBlock).resize(range.fStart, range.fEnd, -32);
    if (!shrunk) return false;
    return sk_asan_address_is_poisoned(retainedByte) == 0 &&
           sk_asan_address_is_poisoned(releasedByte) != 0;
}
#endif

bool interrupted_append_case() {
    Element::live = 0;
    Element::attempts = 0;
    Element::failAt = 0;
    {
        SkTBlockList<Element, 2> owner;
        Element::failAt = Element::attempts + 1;
        bool caught = false;
        try { owner.emplace_back(7); }
        catch (const ConstructionFailure&) { caught = true; }
        Element::failAt = 0;
        if (!caught || !owner.empty() || Element::live != 0) return false;
        owner.emplace_back(11);
        owner.emplace_back(13);
        Element::failAt = Element::attempts + 1;
        caught = false;
        try { owner.emplace_back(17); }
        catch (const ConstructionFailure&) { caught = true; }
        Element::failAt = 0;
        if (!caught || owner.count() != 2 || Element::live != 2) return false;
        if (owner.item(0).value != 11 || owner.item(1).value != 13) return false;
        owner.emplace_back(19);
        if (owner.count() != 3 || owner.back().value != 19) return false;
    }
    return Element::live == 0;
}

bool interrupted_concat_case() {
    Element::live = 0;
    Element::attempts = 0;
    Element::failAt = 0;
    {
        SkTBlockList<Element, 2> target;
        target.emplace_back(10);
        SkTBlockList<Element, 4> source;
        source.emplace_back(20);
        source.emplace_back(30);
        source.emplace_back(40);
        Element::failAt = Element::attempts + 2;
        bool caught = false;
        try { target.concat(std::move(source)); }
        catch (const ConstructionFailure&) { caught = true; }
        Element::failAt = 0;
        if (!caught || target.count() != 2 || source.count() != 3 || Element::live != 5) {
            return false;
        }
        if (target.item(0).value != 10 || target.item(1).value != 20) return false;
        target.emplace_back(50);
        source.emplace_back(60);
        if (target.count() != 3 || source.count() != 4 || Element::live != 7) return false;
    }
    return Element::live == 0;
}
#endif
}

int main(int argc, char** argv) {
#ifndef STX_SKIA_REFERENCE
    if (argc == 2 && std::strcmp(argv[1], "--invalid-block") == 0) {
        SkTBlockList<int, 2> invalid(0);
        return invalid.count();
    }
    if (argc == 2 && std::strcmp(argv[1], "--negative-reserve") == 0) {
        SkTBlockList<int, 2> invalid;
        invalid.reserve(-1);
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--empty-front") == 0) {
        SkTBlockList<int, 2> invalid;
        return invalid.front();
    }
    if (argc == 2 && std::strcmp(argv[1], "--zero-increment") == 0) {
        SkBlockAllocator invalid(SkBlockAllocator::GrowthPolicy::kFixed, 0);
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--huge-increment") == 0) {
        const size_t huge = std::numeric_limits<size_t>::max();
        SkBlockAllocator invalid(SkBlockAllocator::GrowthPolicy::kFixed, huge);
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--huge-prealloc") == 0) {
        const size_t huge = std::numeric_limits<size_t>::max();
        SkBlockAllocator invalid(SkBlockAllocator::GrowthPolicy::kFixed, 64, huge);
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--invalid-policy") == 0) {
        const SkBlockAllocator::GrowthPolicy invalidPolicy =
                static_cast<SkBlockAllocator::GrowthPolicy>(-1);
        SkBlockAllocator invalid(invalidPolicy, 64);
        return 0;
    }
#endif
    bool valid = ordinary_case() && nontrivial_case();
#ifndef STX_SKIA_REFERENCE
    valid = move_only_case() && valid;
#ifdef SK_SANITIZE_ADDRESS
    valid = shrunk_range_is_poisoned() && valid;
#endif
    valid = interrupted_append_case() && valid;
    valid = interrupted_concat_case() && valid;
#endif
    if (!valid) {
        std::fputs("block list contract mismatch\n", stderr);
        return 1;
    }
    std::puts("block list: append, pop, concat PASS");
    return 0;
}
