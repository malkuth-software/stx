#include "src/core/SkTInternalLList.h"
#include "src/lazy/SkDiscardableMemoryPool.h"
#include "checked_size.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace {
struct Node {
    int value = 0;
    int* destructions = nullptr;
    ~Node() { if (destructions != nullptr) ++*destructions; }
    SK_DECLARE_INTERNAL_LLIST_INTERFACE(Node);
};
using List = SkTInternalLList<Node>;

bool verify(const List& list, const std::vector<int>& expected) {
    if (list.isEmpty() != expected.empty()) return false;
    if (expected.empty()) {
        if (list.head() != nullptr || list.tail() != nullptr) return false;
    } else {
        if (list.head() == nullptr || list.tail() == nullptr) return false;
        if ((*list.head()).value != expected.front() || (*list.tail()).value != expected.back()) return false;
    }
    List::Iter forward{};
    Node* node = forward.init(list, List::Iter::kHead_IterStart);
    for (const int value : expected) {
        if (node == nullptr || (*node).value != value) return false;
        node = forward.next();
    }
    if (node != nullptr || forward.next() != nullptr || forward.prev() != nullptr) return false;
    List::Iter reverse{};
    node = reverse.init(list, List::Iter::kTail_IterStart);
    for (size_t index = expected.size(); index != 0; --index) {
        if (node == nullptr || (*node).value != expected[index - 1]) return false;
        node = reverse.prev();
    }
    if (node != nullptr || reverse.prev() != nullptr || reverse.next() != nullptr) return false;
    size_t index = 0;
    for (const Node* current : list) {
        if (index >= expected.size() || (*current).value != expected[index]) return false;
        ++index;
    }
    if (index != expected.size()) return false;
#ifdef SK_DEBUG
    list.validate();
    if (list.countEntries() != test_sizes::to_int(expected.size())) return false;
#endif
    return true;
}

bool insertion_and_removal() {
    for (int count = 0; count <= 6; ++count) {
        std::vector<int> order(static_cast<size_t>(count));
        for (int index = 0; index < count; ++index) order[static_cast<size_t>(index)] = index;
        do {
            for (int victim = 0; victim < count; ++victim) {
                std::array<Node, 6> nodes{};
                List list{};
                for (const int index : order) {
                    nodes[static_cast<size_t>(index)].value = index;
                    list.addToTail(&nodes[static_cast<size_t>(index)]);
                }
                if (!verify(list, order)) return false;
                list.remove(&nodes[static_cast<size_t>(victim)]);
                std::vector<int> expected(order);
                expected.erase(std::find(expected.begin(), expected.end(), victim));
                if (!verify(list, expected)) return false;
                list.addToHead(&nodes[static_cast<size_t>(victim)]);
                expected.insert(expected.begin(), victim);
                if (!verify(list, expected)) return false;
                // Advance while the current node is still linked, then remove it.
                List::Iter iterator{};
                Node* node = iterator.init(list, List::Iter::kHead_IterStart);
                while (node != nullptr) {
                    Node* next = iterator.next();
                    list.remove(node);
                    node = next;
                }
                if (!verify(list, {})) return false;
            }
        } while (std::next_permutation(order.begin(), order.end()));
    }
    return true;
}

bool relative_insertions() {
    for (int count = 0; count <= 6; ++count) {
        for (int position = 0; position <= count; ++position) {
            for (const bool before : std::array<bool, 2>{false, true}) {
                std::array<Node, 7> nodes{};
                List list{};
                std::vector<int> expected{};
                for (int index = 0; index < count; ++index) {
                    nodes[static_cast<size_t>(index)].value = index;
                    list.addToTail(&nodes[static_cast<size_t>(index)]);
                    expected.push_back(index);
                }
                Node& inserted = nodes[6];
                inserted.value = 99;
                if (before) {
                    Node* existing = position == count ? nullptr : &nodes[static_cast<size_t>(position)];
                    list.addBefore(&inserted, existing);
                } else {
                    Node* existing = position == 0 ? nullptr : &nodes[static_cast<size_t>(position - 1)];
                    list.addAfter(&inserted, existing);
                }
                expected.insert(expected.begin() + position, 99);
                if (!verify(list, expected)) return false;
                list.remove(&inserted);
                expected.erase(expected.begin() + position);
                if (!verify(list, expected)) return false;
            }
        }
    }
    return true;
}

bool concatenation_and_lifetime() {
    int destructions = 0;
    for (int split = 0; split <= 8; ++split) {
        const int before = destructions;
        {
            std::array<Node, 8> nodes{};
            {
                List first{};
                List second{};
                first.concat(std::move(second));
                if (!verify(first, {}) || !verify(second, {})) return false;
                std::vector<int> expected{};
                for (int index = 0; index < 8; ++index) {
                    Node& node = nodes[static_cast<size_t>(index)];
                    node.value = index;
                    node.destructions = &destructions;
                    if (index < split) first.addToTail(&node);
                    else second.addToTail(&node);
                    expected.push_back(index);
                }
                first.concat(std::move(second));
                if (!verify(first, expected) || !verify(second, {})) return false;
                first.concat(std::move(second));
                if (!verify(first, expected)) return false;
                List::Iter original = first.begin();
                List::Iter copied(original);
                List::Iter assigned{};
                assigned = copied;
                if (assigned.get() != original.get()) return false;
                // Reset abandons links; these nodes are destroyed next, not reinserted.
                first.reset();
                if (!verify(first, {})) return false;
                if (destructions != before) return false;
            }
            if (destructions != before) return false;
        }
        if (destructions != before + 8) return false;
    }
#ifndef STX_SKIA_REFERENCE
    List empty{};
    List::Iter invalid{};
    if (invalid.init(empty, static_cast<List::Iter::IterStart>(2)) != nullptr) return false;
#endif
    return true;
}

// Probe an unlocked block and leave it unlocked, including on unexpected success.
bool discarded(SkDiscardableMemory& memory) {
    const bool acquired = memory.lock();
    if (acquired) memory.unlock();
    const bool lost = !acquired;
    return lost;
}

bool discardable_pool() {
    sk_sp<SkDiscardableMemoryPool> pool = SkDiscardableMemoryPool::Make(16);
    std::unique_ptr<SkDiscardableMemory> first((*pool).create(8));
    std::unique_ptr<SkDiscardableMemory> second((*pool).create(8));
    if (!first || !second) {
        if (first) (*first).unlock();
        if (second) (*second).unlock();
        return false;
    }
    (*first).unlock();
    std::unique_ptr<SkDiscardableMemory> third((*pool).create(8));
    if (!third) {
        (*second).unlock();
        return false;
    }
    const bool firstDiscarded = discarded(*first);
    (*second).unlock();
    (*third).unlock();
    if (!firstDiscarded || (*pool).getRAMUsed() != 16) return false;
    if (!(*second).lock()) return false;
    (*second).unlock();
    (*pool).setRAMBudget(8);
    if (!discarded(*third) || (*pool).getRAMUsed() != 8) return false;
    if (!(*second).lock()) return false;
    (*pool).setRAMBudget(0);
    const bool retainedWhileLocked = (*pool).getRAMUsed() == 8;
    (*second).unlock();
    if (!retainedWhileLocked || (*pool).getRAMUsed() != 0) return false;
    first.reset(); second.reset(); third.reset();
    return true;
}

#ifndef STX_SKIA_REFERENCE
template <typename T> concept TemporaryBegin = requires { T{}.begin(); };
template <typename T> concept TemporaryInit = requires(typename T::Iter& iterator) {
    iterator.init(T{}, T::Iter::kHead_IterStart);
};
static_assert(!TemporaryBegin<List>);
static_assert(!TemporaryInit<List>);
#endif
static_assert(sizeof(List) == 2 * sizeof(Node*));
static_assert(sizeof(List::Iter) == sizeof(Node*));
}

int main() {
    if (!insertion_and_removal()) return 1;
    if (!relative_insertions()) return 2;
    if (!concatenation_and_lifetime()) return 3;
    if (!discardable_pool()) return 5;
    std::printf("intrusive list: links, traversal, transfer, lifetime PASS; list=%zu iterator=%zu\n",
                sizeof(List), sizeof(List::Iter));
    return 0;
}
