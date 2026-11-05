// Differential corpus for SkBlockAllocator and SkTBlockList over their whole public interfaces.
//
// Allocator: seeded operation sequences for every growth policy and several increments, with
// inline (SkSBlockAllocator) and plain head blocks. Operations are allocate at several
// alignments and paddings, release, resize, reserve with each flag combination, releaseBlock,
// stealHeapBlocks, reset, resetScratchSpace, metadata, owningBlock and findOwningBlock. After
// each step the corpus records the offsets returned, the block count and every block's
// available bytes and metadata, the current block's index, and the size totals. Addresses are
// never recorded. Alignments stay at 8 or below, within the allocator's address alignment on
// every target (alignof(std::max_align_t) is 8 on Apple arm64, MSVC and wasm), so that offsets
// do not depend on where the stack or heap places a block.
//
// Block list: seeded sequences of push_back (default, copy, move), emplace_back, pop_back,
// reserve, reset, item, front, back and concat (with equal and different starting sizes), on
// trivial and nontrivial items, recording every item in forward and reverse order and the
// number of live nontrivial items.
//
// Both builds must produce identical bytes (block-list-core.raw).
//
// The candidate alone checks what the intake code gets wrong, over-aligned allocations that
// overrun the block made for them, and guards concatenating items with a trivial copy but a
// nontrivial destructor. "--steal-self" selects a Release-check stop.

#include "differential_corpus.h"

#include "src/core/SkBlockAllocator.h"
#include "src/core/SkTBlockList.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;
using GrowthPolicy = SkBlockAllocator::GrowthPolicy;
using Block = SkBlockAllocator::Block;

// A reservation still held by the sequence, with the alignment and padding it was made with.
struct Held {
    Block* block = nullptr;
    int start = 0;
    int aligned = 0;
    int end = 0;
    int alignment = 1;
    int padding = 0;
};

int block_index(const SkBlockAllocator& allocator, const Block* block) {
    int index = 0;
    for (const Block* candidate : allocator.blocks()) {
        if (candidate == block) return index;
        ++index;
    }
    return -1;
}

void record_allocator(Output& output, const SkBlockAllocator& allocator) {
    int blocks = 0;
    for (const Block* block : allocator.blocks()) {
        append(output, static_cast<int32_t>((*block).avail()));
        append(output, static_cast<int32_t>((*block).avail<8>()));
        append(output, static_cast<int32_t>((*block).metadata()));
        append(output, static_cast<int32_t>((*block).firstAlignedOffset<8, 4>()));
        ++blocks;
    }
    int reverse = 0;
    for (const Block* block : allocator.rblocks()) {
        append(output, static_cast<int32_t>(block_index(allocator, block)));
        ++reverse;
    }
    append(output, static_cast<int32_t>(blocks));
    append(output, static_cast<int32_t>(reverse));
    append(output, static_cast<int32_t>(block_index(allocator, allocator.currentBlock())));
    append_bool(output, allocator.headBlock() == allocator.currentBlock());
    append(output, static_cast<uint64_t>(allocator.totalSize()));
    append(output, static_cast<uint64_t>(allocator.totalUsableSpace()));
    append(output, static_cast<uint64_t>(allocator.totalSpaceInUse()));
    append(output, static_cast<uint64_t>(allocator.preallocSize()));
    append(output, static_cast<uint64_t>(allocator.preallocUsableSpace()));
    append(output, static_cast<int32_t>(allocator.metadata()));
    ++output.cases;
}

// Writes a pattern over a reservation's usable bytes, so that the instrumented build checks that
// every byte handed out is addressable.
void touch(Held& held, uint8_t pattern) {
    if (held.end > held.aligned) {
        std::memset((*held.block).ptr(held.aligned), pattern,
                    static_cast<size_t>(held.end - held.aligned));
    }
}

template <size_t Align, size_t Padding>
Held allocate_with(SkBlockAllocator& allocator, size_t size) {
    const SkBlockAllocator::ByteRange range = allocator.allocate<Align, Padding>(size);
    Held held{};
    held.block = range.fBlock;
    held.start = range.fStart;
    held.aligned = range.fAlignedOffset;
    held.end = range.fEnd;
    held.alignment = static_cast<int>(Align);
    held.padding = static_cast<int>(Padding);
    return held;
}

Held allocate_kind(SkBlockAllocator& allocator, int kind, size_t size) {
    switch (kind) {
        case 0: return allocate_with<1, 0>(allocator, size);
        case 1: return allocate_with<4, 0>(allocator, size);
        case 2: return allocate_with<8, 0>(allocator, size);
        case 3: return allocate_with<2, 0>(allocator, size);
        case 4: return allocate_with<4, 4>(allocator, size);
        default: return allocate_with<8, 8>(allocator, size);
    }
}

template <size_t Align, size_t Padding>
const Block* owner_with(SkBlockAllocator& allocator, const Held& held) {
    const void* pointer = (*held.block).ptr(held.aligned);
    const Block* owner = allocator.owningBlock<Align, Padding>(pointer, held.start);
    return owner;
}

const Block* owner_of(SkBlockAllocator& allocator, const Held& held) {
    if (held.alignment == 1) return owner_with<1, 0>(allocator, held);
    if (held.alignment == 2) return owner_with<2, 0>(allocator, held);
    if (held.alignment == 4) {
        if (held.padding == 4) return owner_with<4, 4>(allocator, held);
        return owner_with<4, 0>(allocator, held);
    }
    if (held.padding == 8) return owner_with<8, 8>(allocator, held);
    return owner_with<8, 0>(allocator, held);
}

void reserve_kind(SkBlockAllocator& allocator, int kind, size_t size,
                  SkBlockAllocator::ReserveFlags flags) {
    switch (kind) {
        case 0: allocator.reserve<1>(size, flags); break;
        case 1: allocator.reserve<8>(size, flags); break;
        default: allocator.reserve<8, 8>(size, flags); break;
    }
}

size_t pick_size(Sequence& sequence) {
    const size_t choice = sequence.pick(10);
    if (choice < 6) return sequence.pick(48) + 1;
    if (choice < 9) return sequence.pick(600) + 1;
    return sequence.pick(70000) + 1;
}

void forget_block(std::vector<Held>& held, const Block* block) {
    std::vector<Held> kept{};
    for (const Held& reservation : held) {
        if (reservation.block != block) kept.push_back(reservation);
    }
    held.swap(kept);
}

void allocator_steps(Output& output, SkBlockAllocator& allocator, SkBlockAllocator& other,
                     uint64_t seed, int steps) {
    Sequence sequence{};
    sequence.state = seed;
    std::vector<Held> held{};
    std::vector<Held> otherHeld{};
    uint8_t pattern = 1;
    for (int step = 0; step < steps; ++step) {
        const int operation = static_cast<int>(sequence.pick(16));
        append(output, static_cast<uint8_t>(operation));
        switch (operation) {
            case 0:
            case 1:
            case 2: {
                const int kind = static_cast<int>(sequence.pick(6));
                Held reservation = allocate_kind(allocator, kind, pick_size(sequence));
                touch(reservation, pattern++);
                append(output, static_cast<int32_t>(block_index(allocator, reservation.block)));
                append(output, static_cast<int32_t>(reservation.start));
                append(output, static_cast<int32_t>(reservation.aligned));
                append(output, static_cast<int32_t>(reservation.end));
                held.push_back(reservation);
                break;
            }
            case 3:
                // Release the newest reservation, which the block can reclaim.
                if (!held.empty()) {
                    const Held reservation = held.back();
                    held.pop_back();
                    append_bool(output, (*reservation.block).release(reservation.start,
                                                                    reservation.end));
                }
                break;
            case 4:
                // Release some other reservation, which the block usually cannot reclaim.
                if (!held.empty()) {
                    const size_t index = sequence.pick(held.size());
                    const Held reservation = held[index];
                    held.erase(held.begin() + static_cast<std::ptrdiff_t>(index));
                    append_bool(output, (*reservation.block).release(reservation.start,
                                                                    reservation.end));
                }
                break;
            case 5:
                if (!held.empty()) {
                    const size_t index = sequence.pick(held.size());
                    Held& reservation = held[index];
                    const int length = reservation.end - reservation.aligned;
                    // Shrinks keep at least one byte; growth asks for up to 300 more.
                    const int delta = static_cast<int>(sequence.pick(
                                              static_cast<size_t>(length) + 300)) -
                                      (length - 1);
                    Block& block = *reservation.block;
                    const bool resized = block.resize(reservation.start, reservation.end, delta);
                    append_bool(output, resized);
                    if (resized) {
                        reservation.end += delta;
                        touch(reservation, pattern++);
                    }
                }
                break;
            case 6: {
                const int kind = static_cast<int>(sequence.pick(3));
                const int flags = static_cast<int>(sequence.pick(4));
                reserve_kind(allocator, kind, pick_size(sequence),
                             static_cast<SkBlockAllocator::ReserveFlags>(flags));
                break;
            }
            case 7: {
                // Release a whole block; its reservations end with it.
                int blocks = 0;
                for (const Block* block : allocator.blocks()) {
                    (void)block;
                    ++blocks;
                }
                const size_t choices = static_cast<size_t>(blocks);
                const int target = static_cast<int>(sequence.pick(choices));
                Block* chosen = nullptr;
                int index = 0;
                for (Block* block : allocator.blocks()) {
                    if (index == target) chosen = block;
                    ++index;
                }
                forget_block(held, chosen);
                allocator.releaseBlock(chosen);
                append(output, static_cast<int32_t>(target));
                break;
            }
            case 8: {
                Held reservation = allocate_kind(other, static_cast<int>(sequence.pick(6)),
                                                 pick_size(sequence));
                touch(reservation, pattern++);
                otherHeld.push_back(reservation);
                record_allocator(output, other);
                break;
            }
            case 9: {
                // Steal the other allocator's heap blocks; reservations in its head block stay.
                const Block* otherHead = other.headBlock();
                allocator.stealHeapBlocks(&other);
                std::vector<Held> kept{};
                for (const Held& reservation : otherHeld) {
                    if (reservation.block == otherHead) {
                        kept.push_back(reservation);
                    } else {
                        held.push_back(reservation);
                    }
                }
                otherHeld.swap(kept);
                record_allocator(output, other);
                break;
            }
            case 10:
                if (sequence.pick(4) == 0) {
                    allocator.reset();
                    held.clear();
                }
                break;
            case 11:
                allocator.resetScratchSpace();
                break;
            case 12:
                allocator.setMetadata(static_cast<int>(sequence.next() >> 8));
                (*allocator.currentBlock()).setMetadata(static_cast<int>(sequence.next() >> 8));
                break;
            case 13:
                if (!held.empty()) {
                    const Held& reservation = held[sequence.pick(held.size())];
                    if (reservation.end > reservation.aligned) {
                        const Block* owner = owner_of(allocator, reservation);
                        append_bool(output, owner == reservation.block);
                        const void* pointer = (*reservation.block).ptr(reservation.aligned);
                        const Block* found = allocator.findOwningBlock(pointer);
                        append_bool(output, found == reservation.block);
                    }
                }
                break;
            case 14: {
                const SkBlockAllocator& constant = allocator;
                const int current = block_index(constant, constant.currentBlock());
                append(output, static_cast<int32_t>(current));
                break;
            }
            default:
                if (sequence.pick(8) == 0) {
                    other.reset();
                    otherHeld.clear();
                }
                break;
        }
        record_allocator(output, allocator);
    }
}

void allocator_cases(Output& output) {
    const GrowthPolicy policies[4] = {GrowthPolicy::kFixed, GrowthPolicy::kLinear,
                                      GrowthPolicy::kFibonacci, GrowthPolicy::kExponential};
    const size_t increments[3] = {1, 200, 4096};
    uint64_t seed = 101;
    for (const GrowthPolicy policy : policies) {
        for (const size_t increment : increments) {
            SkBlockAllocator allocator(policy, increment);
            SkBlockAllocator other(policy, increment);
            record_allocator(output, allocator);
            allocator_steps(output, allocator, other, seed++, 500);
        }
        SkSBlockAllocator<256> inlineHead(policy);
        SkSBlockAllocator<1024> inlineOther(policy, 64);
        record_allocator(output, *inlineHead.allocator());
        allocator_steps(output, *inlineHead.allocator(), *inlineOther.allocator(), seed++, 500);
    }
    // The overhead helpers are compile-time sizes that both builds must agree on.
    append(output, static_cast<uint64_t>(SkBlockAllocator::BlockOverhead<1, 0>()));
    append(output, static_cast<uint64_t>(SkBlockAllocator::BlockOverhead<8, 8>()));
    append(output, static_cast<uint64_t>(SkBlockAllocator::Overhead<1, 0>()));
    append(output, static_cast<uint64_t>(SkBlockAllocator::Overhead<8, 8>()));
    ++output.cases;
}

int gLive = 0;

// A nontrivial item; a moved-from item reads -1.
struct Item {
    int value = 0;
    Item() { ++gLive; }
    explicit Item(int v) : value(v) { ++gLive; }
    Item(const Item& other) : value(other.value) { ++gLive; }
    Item(Item&& other) noexcept : value(other.value) {
        other.value = -1;
        ++gLive;
    }
    ~Item() { --gLive; }
};

int value_of(int item) { return item; }
int value_of(const Item& item) { return item.value; }

template <typename T>
T make(int value) {
    if constexpr (std::is_same_v<T, int>) {
        return value;
    } else {
        return T(value);
    }
}

template <typename T>
void set_value(T& item, int value) {
    if constexpr (std::is_same_v<T, int>) {
        item = value;
    } else {
        item.value = value;
    }
}

template <typename T, typename List>
void record_list(Output& output, const List& list) {
    append(output, static_cast<int32_t>(list.count()));
    append_bool(output, list.empty());
    for (const T& item : list.items()) {
        append(output, static_cast<int32_t>(value_of(item)));
    }
    for (const T& item : list.ritems()) {
        append(output, static_cast<int32_t>(value_of(item)));
    }
    if (!list.empty()) {
        append(output, static_cast<int32_t>(value_of(list.front())));
        append(output, static_cast<int32_t>(value_of(list.back())));
        append(output, static_cast<int32_t>(value_of(list.item(list.count() / 2))));
    }
    append(output, static_cast<int32_t>(gLive));
    ++output.cases;
}

template <typename T, typename List, typename Other>
void list_steps(Output& output, uint64_t seed, int steps) {
    Sequence sequence{};
    sequence.state = seed;
    List list{};
    Other other{};
    List same{};
    int next = 1;
    for (int step = 0; step < steps; ++step) {
        const int operation = static_cast<int>(sequence.pick(13));
        append(output, static_cast<uint8_t>(operation));
        const bool room = list.count() < 400;
        switch (operation) {
            case 0:
                if (room) set_value(list.push_back(), next++);
                break;
            case 1:
                if (room) {
                    const T value = make<T>(next++);
                    list.push_back(value);
                }
                break;
            case 2:
                if (room) {
                    T value = make<T>(next++);
                    list.push_back(std::move(value));
                }
                break;
            case 3:
                if (room) list.emplace_back(make<T>(next++));
                break;
            case 4:
            case 5:
                if (!list.empty()) list.pop_back();
                break;
            case 6:
                list.reserve(static_cast<int>(sequence.pick(40)));
                break;
            case 7:
                if (sequence.pick(5) == 0) list.reset();
                break;
            case 8: {
                const int count = static_cast<int>(sequence.pick(30));
                for (int index = 0; index < count; ++index) {
                    other.emplace_back(make<T>(next++));
                }
                break;
            }
            case 9:
                if (list.count() + other.count() <= 600) list.concat(std::move(other));
                record_list<T>(output, other);
                break;
            case 10: {
                const int count = static_cast<int>(sequence.pick(4));
                for (int index = 0; index < count; ++index) {
                    same.emplace_back(make<T>(next++));
                }
                if (list.count() + same.count() <= 600) list.concat(std::move(same));
                record_list<T>(output, same);
                break;
            }
            case 11:
                if (!list.empty()) {
                    const size_t choices = static_cast<size_t>(list.count());
                    const int index = static_cast<int>(sequence.pick(choices));
                    set_value(list.item(index), next++);
                }
                break;
            default:
                if (!list.empty()) {
                    set_value(list.front(), next++);
                    set_value(list.back(), next++);
                }
                break;
        }
        record_list<T>(output, list);
    }
}

void list_cases(Output& output) {
    for (uint64_t seed = 1; seed <= 3; ++seed) {
        list_steps<int, SkTBlockList<int, 1>, SkTBlockList<int, 5>>(output, seed, 600);
        list_steps<int, SkTBlockList<int, 7>, SkTBlockList<int, 2>>(output, seed + 10, 600);
        list_steps<Item, SkTBlockList<Item, 4>, SkTBlockList<Item, 1>>(output, seed + 20, 600);
        list_steps<Item, SkTBlockList<Item, 16>, SkTBlockList<Item, 16>>(output, seed + 30, 600);
    }
    // Lists with each growth policy and an explicit block size.
    const GrowthPolicy policies[4] = {GrowthPolicy::kFixed, GrowthPolicy::kLinear,
                                      GrowthPolicy::kFibonacci, GrowthPolicy::kExponential};
    for (const GrowthPolicy policy : policies) {
        SkTBlockList<int, 2> grown(policy);
        SkTBlockList<int, 2> sized(3, policy);
        for (int value = 0; value < 50; ++value) {
            grown.push_back(value);
            sized.push_back(value * 2);
        }
        record_list<int>(output, grown);
        record_list<int>(output, sized);
    }
}

#ifndef STX_SKIA_REFERENCE
// Allocations aligned beyond the address alignment a block is guaranteed fit the block made for
// them, wherever the heap places it, through allocate and through reserve. A fresh allocator's
// only heap block is the one made for the request, so its size is the growth in total size.
template <size_t Align>
bool over_aligned_fit() {
    std::vector<std::unique_ptr<SkBlockAllocator>> kept{};
    for (size_t size = 1; size < 400; size += 3) {
        for (int round = 0; round < 8; ++round) {
            for (const bool reserved : {false, true}) {
                kept.push_back(std::make_unique<SkBlockAllocator>(GrowthPolicy::kFixed, 1));
                SkBlockAllocator& allocator = *kept.back();
                if (reserved) allocator.reserve<Align>(size);
                const SkBlockAllocator::ByteRange range = allocator.allocate<Align>(size);
                if (range.fBlock == allocator.headBlock()) continue;
                const size_t blockSize = allocator.totalSize() - allocator.preallocSize();
                if (static_cast<size_t>(range.fEnd) > blockSize) return false;
                const uintptr_t address =
                        reinterpret_cast<uintptr_t>((*range.fBlock).ptr(range.fAlignedOffset));
                if (address % Align != 0) return false;
                std::memset((*range.fBlock).ptr(range.fAlignedOffset), 0x5A, size);
            }
        }
    }
    return true;
}

int gCopyDestroyed = 0;

// A trivial copy with a nontrivial destructor: copying it as bytes would leave the source's
// destructor unrun. (Clang's is_trivially_copy_constructible already counts the destructor, so
// the intake code moves these too; this guards the condition, now is_trivially_copyable.)
struct TrivialCopy {
    int value = 0;
    explicit TrivialCopy(int v) : value(v) {}
    TrivialCopy(const TrivialCopy&) = default;
    ~TrivialCopy() { ++gCopyDestroyed; }
};
static_assert(!std::is_trivially_copyable_v<TrivialCopy>);

// Ten items, four of them in the source's head block. Concatenation copies those four and ends
// the originals, so fourteen objects end in all, and the values arrive in order.
bool trivial_copy_concat() {
    gCopyDestroyed = 0;
    {
        SkTBlockList<TrivialCopy, 2> target{};
        SkTBlockList<TrivialCopy, 4> source{};
        for (int value = 0; value < 10; ++value) source.emplace_back(value);
        target.concat(std::move(source));
        if (target.count() != 10 || !source.empty()) return false;
        for (int index = 0; index < 10; ++index) {
            if (target.item(index).value != index) return false;
        }
    }
    return gCopyDestroyed == 14;
}

// Each mode violates one Release-checked precondition and must stop the program.
int stop_mode(const char* mode) {
    if (std::strcmp(mode, "--steal-self") == 0) {
        SkBlockAllocator allocator(GrowthPolicy::kFixed, 64);
        allocator.allocate<4>(500);
        allocator.stealHeapBlocks(&allocator);
        return 0;
    }
    return 2;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 1 || argc > 2) return 2;
#ifndef STX_SKIA_REFERENCE
    if (argc == 2 && std::strncmp(argv[1], "--", 2) == 0) return stop_mode(argv[1]);
    if (!over_aligned_fit<16>() || !over_aligned_fit<32>() || !over_aligned_fit<64>() ||
        !over_aligned_fit<128>()) {
        std::puts("block list core: an over-aligned allocation overran its block");
        return 1;
    }
    if (!trivial_copy_concat()) {
        std::puts("block list core: concatenating trivially copied items left them unended");
        return 1;
    }
#endif
    Output output{};
    allocator_cases(output);
    list_cases(output);
    if (gLive != 0) {
        std::printf("block list core: %d items outlived their lists\n", gLive);
        return 1;
    }
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("block list core: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
