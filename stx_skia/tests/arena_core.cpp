// Differential corpus for SkArenaAlloc, SkSTArenaAlloc, SkArenaAllocWithReset and
// SkSTArenaAllocWithReset over their whole public interfaces. Seeded sequences make trivial and
// nontrivial objects (by arguments, by default and through a constructor callable, including
// objects that allocate from the same arena while they are built), arrays of every kind, and
// aligned bytes, on arenas with no, small, inline and caller-provided storage; the resetting
// arenas also reset at random points.
//
// After every operation the corpus records the bytes left in the current block, the offset of
// the new allocation when it lies in caller-provided storage, every value, and whether the live
// allocations stay aligned and disjoint. When an arena ends or resets, it records the order in
// which destructors ran. Addresses are never recorded, and alignments stay at 8 or below so that
// block layouts do not depend on where blocks are placed. Both builds must produce identical
// bytes (arena-core.raw).
//
// The candidate stores, with each array of nontrivial elements, how many have been constructed
// (7f5301cd), so that a failed constructor ends only those; that is 4 more bytes per such array.
// The corpus therefore runs twice. The first pass makes every kind of allocation and writes the
// remaining bytes and the offsets in caller storage to a second artifact
// (arena-core-layout.raw), which differs on purpose. The second pass makes no nontrivial arrays
// and records both in the shared artifact, so the block layouts must match exactly there.
//
// The candidate alone checks destructible objects and arrays at the largest alignment a footer
// can record (256), and trivially destructible objects beyond it. Destructible types aligned
// beyond 256 bytes no longer compile; the intake code crashed ending them.

#include "differential_corpus.h"

#include "include/core/SkSpan.h"
#include "src/core/SkArenaAlloc.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <new>
#include <utility>
#include <vector>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

std::vector<int> gDestroyed{};
bool gNontrivialArrays = true;
Output gLayout{};

// Trivially destructible, standard layout and trivial: make<Plain>() takes the byte path.
struct Plain {
    int32_t a;
    int32_t b;
};

// Trivially destructible but not trivial.
struct Pair {
    int32_t first = 0;
    int16_t second = 0;
    Pair() = default;
    Pair(int32_t f, int16_t s) : first(f), second(s) {}
};

// Nontrivial; its destructor records its identity.
struct Tracked {
    int id = 0;
    Tracked() = default;
    explicit Tracked(int i) : id(i) {}
    Tracked(const Tracked& other) : id(other.id + 1000) {}
    ~Tracked() { gDestroyed.push_back(id); }
};

// Allocates two more objects from the arena while it is being built.
struct Nested {
    Tracked* inner = nullptr;
    Pair* pod = nullptr;
    int id = 0;
    Nested(SkArenaAlloc& arena, int i) : id(i) {
        inner = arena.make<Tracked>(i + 500);
        pod = arena.make<Pair>(i, static_cast<int16_t>(i % 100));
    }
    ~Nested() { gDestroyed.push_back(-id); }
};

// A constructor callable for make(Ctor).
struct MakeTracked {
    int id = 0;
    Tracked* operator()(void* storage) const { return new (storage) Tracked(id); }
};

struct MakePair {
    int value = 0;
    Pair* operator()(void* storage) const {
        return new (storage) Pair(value, static_cast<int16_t>(value & 0x7FFF));
    }
};

// An initializer for makeInitializedArray.
struct Initialize {
    int base = 0;
    Tracked operator()(size_t index) const { return Tracked(base + static_cast<int>(index)); }
};

struct Span {
    const std::byte* begin = nullptr;
    size_t size = 0;
    size_t alignment = 1;
};

// Exposes the current block's remaining bytes, which the arena keeps protected.
template <typename Base>
class Probed final : public Base {
public:
    using Base::Base;
    size_t remaining() {
        // Qualified: the inline-storage arenas also inherit std::array's end().
        if ((*this).SkArenaAlloc::cursor() == nullptr) return 0;
        const size_t left = static_cast<size_t>((*this).SkArenaAlloc::end() -
                                                (*this).SkArenaAlloc::cursor());
        return left;
    }
};

bool aligned_and_disjoint(const std::vector<Span>& spans) {
    const std::less<const std::byte*> before{};
    for (size_t i = 0; i < spans.size(); ++i) {
        const Span& a = spans[i];
        if (reinterpret_cast<uintptr_t>(a.begin) % a.alignment != 0) return false;
        for (size_t j = i + 1; j < spans.size(); ++j) {
            const Span& b = spans[j];
            if (a.size == 0 || b.size == 0) continue;
            const bool separate = !before(b.begin, a.begin + a.size) ||
                                  !before(a.begin, b.begin + b.size);
            if (!separate) return false;
        }
    }
    return true;
}

void record_destroyed(Output& output) {
    append(output, static_cast<int32_t>(gDestroyed.size()));
    for (const int id : gDestroyed) append(output, static_cast<int32_t>(id));
    gDestroyed.clear();
    ++output.cases;
}

template <typename T>
void note(std::vector<Span>& spans, const T* object, size_t count) {
    Span span{};
    span.begin = reinterpret_cast<const std::byte*>(object);
    span.size = sizeof(T) * count;
    span.alignment = alignof(T);
    spans.push_back(span);
}

// Runs one seeded sequence. 'storage' and 'storageSize' describe caller-provided storage, if any,
// so that offsets inside it can be recorded. 'reset' resets a resetting arena.
template <typename Arena>
void arena_steps(Output& output, Arena& arena, const std::byte* storage, size_t storageSize,
                 uint64_t seed, int steps, bool (*reset)(Arena&)) {
    Sequence sequence{};
    sequence.state = seed;
    std::vector<Span> spans{};
    int next = 1;
    for (int step = 0; step < steps; ++step) {
        int operation = static_cast<int>(sequence.pick(17));
        const bool nontrivialArray = operation == 9 || operation == 10 || operation == 12;
        if (nontrivialArray && !gNontrivialArrays) operation = 16;
        append(output, static_cast<uint8_t>(operation));
        const std::byte* made = nullptr;
        switch (operation) {
            case 0: {
                Pair* pair = arena.template make<Pair>(next, static_cast<int16_t>(next % 300));
                ++next;
                append(output, (*pair).first);
                append(output, (*pair).second);
                note(spans, pair, 1);
                made = reinterpret_cast<const std::byte*>(pair);
                break;
            }
            case 1: {
                Tracked* tracked = arena.template make<Tracked>(next++);
                append(output, static_cast<int32_t>((*tracked).id));
                note(spans, tracked, 1);
                made = reinterpret_cast<const std::byte*>(tracked);
                break;
            }
            case 2: {
                Plain* plain = arena.template make<Plain>();
                (*plain).a = next++;
                (*plain).b = next++;
                note(spans, plain, 1);
                made = reinterpret_cast<const std::byte*>(plain);
                break;
            }
            case 3: {
                Tracked* tracked = arena.template make<Tracked>();
                append(output, static_cast<int32_t>((*tracked).id));
                note(spans, tracked, 1);
                made = reinterpret_cast<const std::byte*>(tracked);
                break;
            }
            case 4: {
                MakeTracked maker{};
                maker.id = next++;
                Tracked* tracked = arena.make(maker);
                append(output, static_cast<int32_t>((*tracked).id));
                note(spans, tracked, 1);
                made = reinterpret_cast<const std::byte*>(tracked);
                break;
            }
            case 5: {
                MakePair maker{};
                maker.value = next++;
                Pair* pair = arena.make(maker);
                append(output, (*pair).first);
                note(spans, pair, 1);
                made = reinterpret_cast<const std::byte*>(pair);
                break;
            }
            case 6: {
                Nested* nested = arena.template make<Nested>(arena, next++);
                append(output, static_cast<int32_t>((*(*nested).inner).id));
                append(output, (*(*nested).pod).first);
                note(spans, nested, 1);
                note(spans, (*nested).inner, 1);
                note(spans, (*nested).pod, 1);
                made = reinterpret_cast<const std::byte*>(nested);
                break;
            }
            case 7: {
                const size_t count = sequence.pick(40);
                int32_t* values = arena.template makeArrayDefault<int32_t>(count);
                for (size_t index = 0; index < count; ++index) values[index] = next++;
                note(spans, values, count);
                made = reinterpret_cast<const std::byte*>(values);
                break;
            }
            case 8: {
                const size_t count = sequence.pick(40);
                int32_t* values = arena.template makeArray<int32_t>(count);
                int32_t sum = 0;
                for (size_t index = 0; index < count; ++index) sum += values[index];
                append(output, sum);
                note(spans, values, count);
                made = reinterpret_cast<const std::byte*>(values);
                break;
            }
            case 9: {
                const size_t count = sequence.pick(12);
                Tracked* values = arena.template makeArray<Tracked>(count);
                for (size_t index = 0; index < count; ++index) values[index].id = next++;
                note(spans, values, count);
                made = reinterpret_cast<const std::byte*>(values);
                break;
            }
            case 10: {
                const size_t count = sequence.pick(12);
                Initialize initializer{};
                initializer.base = next;
                next += static_cast<int>(count);
                Tracked* values = arena.template makeInitializedArray<Tracked>(count, initializer);
                for (size_t index = 0; index < count; ++index) {
                    append(output, static_cast<int32_t>(values[index].id));
                }
                note(spans, values, count);
                made = reinterpret_cast<const std::byte*>(values);
                break;
            }
            case 11: {
                std::array<int32_t, 30> source{};
                for (int32_t& value : source) value = next++;
                const size_t count = sequence.pick(31);
                int32_t* values = arena.template makeArrayCopy<int32_t>(
                        SkSpan<const int32_t>(source.data(), count));
                for (size_t index = 0; index < count; ++index) append(output, values[index]);
                note(spans, values, count);
                made = reinterpret_cast<const std::byte*>(values);
                break;
            }
            case 12: {
                std::array<Tracked, 8> source{};
                for (Tracked& value : source) value.id = next++;
                const size_t count = sequence.pick(9);
                Tracked* values = arena.template makeArrayCopy<Tracked>(
                        SkSpan<const Tracked>(source.data(), count));
                for (size_t index = 0; index < count; ++index) {
                    append(output, static_cast<int32_t>(values[index].id));
                }
                note(spans, values, count);
                made = reinterpret_cast<const std::byte*>(values);
                break;
            }
            case 13: {
                const size_t size = sequence.pick(300);
                const size_t alignment = size_t{1} << sequence.pick(4);
                void* bytes = arena.makeBytesAlignedTo(size, alignment);
                if (size > 0) std::memset(bytes, 0xA5, size);
                Span span{};
                span.begin = static_cast<const std::byte*>(bytes);
                span.size = size;
                span.alignment = alignment;
                spans.push_back(span);
                made = span.begin;
                break;
            }
            case 14: {
                // A larger trivial array, which often needs a new block.
                const size_t count = 200 + sequence.pick(2000);
                uint8_t* bytes = arena.template makeArrayDefault<uint8_t>(count);
                std::memset(bytes, 0x3C, count);
                note(spans, bytes, count);
                made = reinterpret_cast<const std::byte*>(bytes);
                break;
            }
            case 15:
                if (reset != nullptr && sequence.pick(4) == 0) {
                    append_bool(output, reset(arena));
                    spans.clear();
                    record_destroyed(output);
                }
                break;
            default:
                append_bool(output, aligned_and_disjoint(spans));
                break;
        }
        if (gNontrivialArrays) {
            append(gLayout, static_cast<uint64_t>(arena.remaining()));
            ++gLayout.cases;
        } else {
            append(output, static_cast<uint64_t>(arena.remaining()));
        }
        const bool inStorage = made != nullptr && storage != nullptr &&
                               std::less_equal<const std::byte*>{}(storage, made) &&
                               std::less<const std::byte*>{}(made, storage + storageSize);
        append_bool(output, inStorage);
        if (inStorage) {
            Output& layout = gNontrivialArrays ? gLayout : output;
            append(layout, static_cast<uint64_t>(made - storage));
        }
        ++output.cases;
    }
    append_bool(output, aligned_and_disjoint(spans));
}

template <typename Arena>
bool reset_arena(Arena& arena) {
    arena.reset();
    const bool empty = arena.isEmpty();
    return empty;
}

void arena_cases(Output& output) {
    uint64_t seed = 1;
    for (const size_t first : {size_t{0}, size_t{1}, size_t{64}, size_t{1000}}) {
        {
            Probed<SkArenaAlloc> arena(first);
            arena_steps<Probed<SkArenaAlloc>>(output, arena, nullptr, 0, seed++, 400, nullptr);
        }
        record_destroyed(output);
    }
    for (const size_t size : {size_t{0}, size_t{4}, size_t{9}, size_t{64}, size_t{1000}}) {
        alignas(16) std::array<std::byte, 1000> storage{};
        {
            Probed<SkArenaAlloc> arena(reinterpret_cast<char*>(storage.data()), size, 100);
            arena_steps<Probed<SkArenaAlloc>>(output, arena, storage.data(), size, seed++, 400,
                                              nullptr);
        }
        record_destroyed(output);
    }
    {
        Probed<SkSTArenaAlloc<256>> arena{};
        arena_steps<Probed<SkSTArenaAlloc<256>>>(output, arena, nullptr, 0, seed++, 400, nullptr);
    }
    record_destroyed(output);
    {
        Probed<SkSTArenaAlloc<8>> arena(48);
        arena_steps<Probed<SkSTArenaAlloc<8>>>(output, arena, nullptr, 0, seed++, 400, nullptr);
    }
    record_destroyed(output);
    {
        Probed<SkArenaAllocWithReset> arena(nullptr, 0, 64);
        append_bool(output, arena.isEmpty());
        arena_steps<Probed<SkArenaAllocWithReset>>(output, arena, nullptr, 0, seed++, 600,
                                                   &reset_arena<Probed<SkArenaAllocWithReset>>);
    }
    record_destroyed(output);
    {
        alignas(16) std::array<std::byte, 512> storage{};
        Probed<SkArenaAllocWithReset> arena(reinterpret_cast<char*>(storage.data()), 512, 0);
        append_bool(output, arena.isEmpty());
        arena_steps<Probed<SkArenaAllocWithReset>>(output, arena, storage.data(), 512, seed++,
                                                   600,
                                                   &reset_arena<Probed<SkArenaAllocWithReset>>);
    }
    record_destroyed(output);
    {
        Probed<SkSTArenaAllocWithReset<128>> arena{};
        append_bool(output, arena.isEmpty());
        arena_steps<Probed<SkSTArenaAllocWithReset<128>>>(
                output, arena, nullptr, 0, seed++, 600,
                &reset_arena<Probed<SkSTArenaAllocWithReset<128>>>);
    }
    record_destroyed(output);
}

#ifndef STX_SKIA_REFERENCE
int gAlignedEnded = 0;

struct alignas(256) WideAligned {
    int id = 0;
    WideAligned() = default;
    explicit WideAligned(int i) : id(i) {}
    ~WideAligned() { ++gAlignedEnded; }
};

struct alignas(512) WiderPlain {
    int id = 0;
};

struct Narrow {
    int id = 0;
    explicit Narrow(int i) : id(i) {}
    ~Narrow() { ++gAlignedEnded; }
};

// Interleaves narrow and 256-aligned destructible objects and arrays, so that footers carry the
// largest paddings, and 512-aligned trivially destructible objects; every object is aligned and
// every destructor runs once.
bool wide_alignments() {
    gAlignedEnded = 0;
    int made = 0;
    for (int round = 0; round < 40; ++round) {
        SkArenaAlloc arena(static_cast<size_t>(round) * 16);
        for (int index = 0; index < 6; ++index) {
            arena.make<Narrow>(index);
            WideAligned* wide = arena.make<WideAligned>(index);
            WideAligned* wideArray = arena.makeArray<WideAligned>(2);
            WiderPlain* plain = arena.make<WiderPlain>();
            made += 4;
            if (reinterpret_cast<uintptr_t>(wide) % 256 != 0) return false;
            if (reinterpret_cast<uintptr_t>(wideArray) % 256 != 0) return false;
            if (reinterpret_cast<uintptr_t>(plain) % 512 != 0) return false;
            (*plain).id = index;
        }
    }
    return gAlignedEnded == made;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 1 || argc > 3) return 2;
#ifndef STX_SKIA_REFERENCE
    if (!wide_alignments()) {
        std::puts("arena core: a widely aligned allocation was misplaced or not ended");
        return 1;
    }
#endif
    Output output{};
    gNontrivialArrays = true;
    arena_cases(output);
    gNontrivialArrays = false;
    arena_cases(output);
    if (argc >= 2 && !corpus::write_file(argv[1], output)) return 2;
    if (argc == 3 && !corpus::write_file(argv[2], gLayout)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    const unsigned long long layoutHash = corpus::fnv1a(gLayout.bytes);
    std::printf("arena core: %zu cases, %zu bytes, hash=%llu; layout hash=%llu PASS\n",
                output.cases, output.bytes.size(), hash, layoutHash);
    return 0;
}
