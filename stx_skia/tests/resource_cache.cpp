#include "src/core/SkResourceCache.h"
#include "src/core/SkSynchronizedResourceCache.h"
#include "src/core/SkCachedData.h"

#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <thread>
#include <vector>

namespace {
unsigned keyNamespace = 0;
unsigned otherNamespace = 0;

struct TestKey final : SkResourceCache::Key {
    explicit TestKey(uint32_t value, uint64_t shared = 0, void* space = &keyNamespace)
        : value(value) { init(space, shared, sizeof(value)); }
    uint32_t value;
};
struct LongKey final : SkResourceCache::Key {
    LongKey() { init(&keyNamespace, 0, sizeof(values)); }
    std::array<uint32_t, 2> values{1, 2};
};
struct Statistics {
    std::atomic<int> installed{0};
    std::atomic<int> destroyed{0};
};
class Record final : public SkResourceCache::Rec {
public:
    Record(uint32_t id, size_t charge, Statistics& stats, bool pinned = false, uint64_t shared = 0)
        : key(id, shared), charge(charge), stats(stats), pinned(pinned) {}
    ~Record() override { stats.destroyed.fetch_add(1, std::memory_order_relaxed); }
    const SkResourceCache::Key& getKey() const override { return key; }
    size_t bytesUsed() const override { return charge; }
    bool canBePurged() override { return !pinned; }
    void postAddInstall(void* payload) override {
        stats.installed.fetch_add(1, std::memory_order_relaxed);
        if (payload != nullptr) *static_cast<uint32_t*>(payload) = key.value;
    }
    const char* getCategory() const override { return "resource-contract"; }
    const TestKey key;
    const size_t charge;
    Statistics& stats;  // Test owner precedes cache and survives record destruction.
    bool pinned;
};
struct Lookup {
    uint32_t value = 0;
    bool valid = true;
    int calls = 0;
};
bool lookup(const SkResourceCache::Rec& base, void* context) {
    const Record& record = static_cast<const Record&>(base);
    Lookup& output = *static_cast<Lookup*>(context);
    output.value = record.key.value;
    ++output.calls;
    return output.valid;
}
void collect(const SkResourceCache::Rec& base, void* context) {
    const Record& record = static_cast<const Record&>(base);
    std::vector<uint32_t>& values = *static_cast<std::vector<uint32_t>*>(context);
    values.push_back(record.key.value);
}
bool has(SkResourceCache& cache, uint32_t id, uint64_t shared = 0) {
    const TestKey key(id, shared);
    Lookup found{};
    const bool present = cache.find(key, lookup, &found);
    const bool correct = present && found.value == id && found.calls == 1;
    return correct;
}
bool keys() {
    const TestKey first(1, 0x123456789abcdef0ULL);
    const TestKey equal(1, 0x123456789abcdef0ULL);
    const TestKey different(2, 0x123456789abcdef0ULL);
    const TestKey group(1, 0x123456789abcdef1ULL);
    const TestKey space(1, 0x123456789abcdef0ULL, &otherNamespace);
    const LongKey longer{};
    if (!(first == equal) || first.hash() != equal.hash()) return false;
    if (first == different || first == group || first == space || first == longer) return false;
    if (longer == first) return false;
    if (first.getSharedID() != 0x123456789abcdef0ULL) return false;
    if (first.getNamespace() != &keyNamespace) return false;
    if (first.size() != sizeof(SkResourceCache::Key) + sizeof(uint32_t)) return false;
    return true;
}
bool eviction() {
    Statistics stats{};
    SkResourceCache cache(static_cast<size_t>(31));
    cache.add(new Record(1, 10, stats));
    cache.add(new Record(2, 10, stats));
    cache.add(new Record(3, 10, stats));
    if (cache.getTotalBytesUsed() != 30 || !has(cache, 1)) return false;
    cache.add(new Record(4, 10, stats));
    if (has(cache, 2) || cache.getTotalBytesUsed() != 30) return false;
    std::vector<uint32_t> order{};
    order.reserve(4);
    cache.visitAll(collect, &order);
    if (order != std::vector<uint32_t>{3, 1, 4}) return false;
    // Reaching the limit, not only exceeding it, starts eviction.
    if (cache.setTotalByteLimit(30) != 31 || cache.getTotalBytesUsed() != 20) return false;
    if (has(cache, 3)) return false;
    const TestKey key(1);
    Lookup stale{0, false, 0};
    if (cache.find(key, lookup, &stale) || stale.calls != 1 || has(cache, 1)) return false;
    Lookup untouched{91, true, 0};
    if (cache.find(key, lookup, &untouched) || untouched.value != 91 || untouched.calls != 0) return false;
    cache.purgeAll();
    if (cache.getTotalBytesUsed() != 0 || stats.destroyed != 4 || stats.installed != 4) return false;
    return true;
}
bool duplicates() {
    Statistics original{};
    Statistics duplicate{};
    SkResourceCache cache(static_cast<size_t>(1));
    Record* pinned = new Record(7, 20, original, true, 99);
    uint32_t installed = 0;
    cache.add(pinned, &installed);
    if (installed != 7 || cache.getTotalBytesUsed() != 20) return false;
    cache.add(new Record(7, 10, duplicate, false, 99), &installed);
    if (original.installed != 2 || duplicate.installed != 0 || duplicate.destroyed != 1) return false;
    cache.purgeAll();
    cache.purgeSharedID(99);
    if (!has(cache, 7, 99) || original.destroyed != 0) return false;
    (*pinned).pinned = false; // Exclusive local cache, no concurrent users.
    cache.setTotalByteLimit(100);
    cache.add(new Record(7, 10, duplicate, false, 99));
    if (original.destroyed != 1 || duplicate.installed != 1 || cache.getTotalBytesUsed() != 10) return false;
    cache.purgeSharedID(0);
    if (!has(cache, 7, 99)) return false;
    cache.purgeSharedID(99);
    if (has(cache, 7, 99) || duplicate.destroyed != 2) return false;
    return true;
}
bool messages() {
    Statistics stats{};
    SkResourceCache first(static_cast<size_t>(100));
    SkResourceCache second(static_cast<size_t>(100));
    first.add(new Record(1, 10, stats, false, 512));
    second.add(new Record(1, 10, stats, false, 512));
    SkResourceCache::PostPurgeSharedID(512);
    if (has(first, 1, 512) || has(second, 1, 512) || stats.destroyed != 2) return false;
    first.add(new Record(2, 10, stats, false, 513));
    SkResourceCache::PostPurgeSharedID(513);
    SkCachedData* data = first.newCachedData(64); // Allocation also drains messages.
    if (data == nullptr || (*data).size() != 64 || (*data).data() == nullptr) return false;
    (*data).unref();
    if (first.getTotalBytesUsed() != 0 || stats.destroyed != 3) return false;
    return true;
}
int factoryCalls = 0;
SkDiscardableMemory* failed_factory(size_t) {
    ++factoryCalls;
    return nullptr;
}
bool budgets() {
    Statistics stats{};
    SkResourceCache cache(failed_factory);
    if (cache.getEffectiveSingleAllocationByteLimit() != 0) return false;
    if (cache.newCachedData(100) != nullptr || factoryCalls != 1) return false;
    cache.setSingleAllocationByteLimit(20);
    cache.setTotalByteLimit(1);
    if (cache.getEffectiveSingleAllocationByteLimit() != 20) return false;
    for (uint32_t id = 0; id < 1025; ++id) cache.add(new Record(id, 1, stats));
    if (cache.getTotalBytesUsed() != 1023 || stats.destroyed != 2) return false;
    cache.purgeAll();
    if (stats.destroyed != 1025) return false;
    SkResourceCache bytes(static_cast<size_t>(100));
    if (bytes.getEffectiveSingleAllocationByteLimit() != 100) return false;
    if (bytes.setSingleAllocationByteLimit(40) != 0) return false;
    if (bytes.getEffectiveSingleAllocationByteLimit() != 40) return false;
    if (bytes.setSingleAllocationByteLimit(200) != 40) return false;
    if (bytes.getEffectiveSingleAllocationByteLimit() != 100) return false;
#ifndef STX_SKIA_REFERENCE
    Statistics oversized{};
    SkResourceCache huge(std::numeric_limits<size_t>::max());
    huge.add(new Record(1, std::numeric_limits<size_t>::max(), oversized, true));
    huge.add(new Record(2, 1, oversized));
    if (oversized.destroyed != 1 || oversized.installed != 1) return false;
    if (huge.getTotalBytesUsed() != std::numeric_limits<size_t>::max()) return false;
    huge.add(nullptr);
#endif
    return true;
}
struct WorkerState {
    SkSynchronizedResourceCache* cache;
    Statistics* stats;
    uint32_t first;
    bool passed = true;
};
void worker(WorkerState* state) {
    SkSynchronizedResourceCache& cache = *(*state).cache;
    Statistics& stats = *(*state).stats;
    for (uint32_t index = 0; index < 500; ++index) {
        const uint32_t id = (*state).first + index;
        const uint64_t shared = id + 10000;
        cache.add(new Record(id, 1, stats, false, shared));
        if (!has(cache, id, shared)) (*state).passed = false;
#ifndef STX_SKIA_REFERENCE
        // These entry points now participate in the wrapper's lock contract.
        if (index % 2 == 0) {
            SkResourceCache& base = cache;
            base.purgeSharedID(shared);
        } else {
            SkResourceCache::PostPurgeSharedID(shared);
            cache.checkMessages();
        }
        if (has(cache, id, shared)) (*state).passed = false;
#endif
    }
}
void global_worker(WorkerState* state) {
    Statistics& stats = *(*state).stats;
    for (uint32_t index = 0; index < 500; ++index) {
        const uint32_t id = (*state).first + index;
        const uint64_t shared = id + 20000;
        const TestKey key(id, shared);
        SkResourceCache::Add(new Record(id, 1, stats, false, shared));
        Lookup found{};
        if (!SkResourceCache::Find(key, lookup, &found) || found.value != id) {
            (*state).passed = false;
        }
#ifndef STX_SKIA_REFERENCE
        SkResourceCache::PostPurgeSharedID(shared);
        SkResourceCache::CheckMessages();
        if (SkResourceCache::Find(key, lookup, &found)) (*state).passed = false;
#endif
    }
}

bool synchronized() {
    Statistics stats{};
    SkSynchronizedResourceCache cache(static_cast<size_t>(100000));
    std::array<WorkerState, 4> states{{{&cache, &stats, 0}, {&cache, &stats, 500},
                                    {&cache, &stats, 1000}, {&cache, &stats, 1500}}};
    std::array<std::thread, 4> threads{};
    for (size_t index = 0; index < threads.size(); ++index) {
        threads[index] = std::thread(worker, &states[index]);
    }
    for (std::thread& thread : threads) thread.join();
    for (const WorkerState& state : states) if (!state.passed) return false;
    cache.purgeAll();
    if (cache.getTotalBytesUsed() != 0 || stats.installed != 2000 || stats.destroyed != 2000) return false;
    SkResourceCache::PurgeAll();
    const size_t previousLimit = SkResourceCache::SetTotalByteLimit(100000);
    for (size_t index = 0; index < threads.size(); ++index) {
        threads[index] = std::thread(global_worker, &states[index]);
    }
    for (std::thread& thread : threads) thread.join();
    SkResourceCache::PurgeAll();
    SkResourceCache::SetTotalByteLimit(previousLimit);
    for (const WorkerState& state : states) if (!state.passed) return false;
    if (stats.installed != 4000 || stats.destroyed != 4000) return false;
    return true;
}
bool measure() {
    struct Case { int caches; int messages; int iterations; };
    constexpr std::array<Case, 6> cases{{{1, 0, 500000}, {1, 1, 100000},
        {1, 16, 10000}, {1, 256, 1000}, {4, 1, 50000}, {4, 16, 5000}}};
    for (const Case& work : cases) {
        std::vector<std::unique_ptr<SkResourceCache>> caches{};
        for (int index = 0; index < work.caches; ++index) {
            caches.push_back(std::make_unique<SkResourceCache>(static_cast<size_t>(1024)));
        }
        const TestKey missing(1);
        Lookup found{};
        // Warm allocation capacity before timing; messages target an empty cache
        // so this measures invalidation administration, not record destruction.
        for (int warm = 0; warm < 20; ++warm) {
            for (int message = 0; message < work.messages; ++message) {
                SkResourceCache::PostPurgeSharedID(static_cast<uint64_t>(message + 1));
            }
            for (const std::unique_ptr<SkResourceCache>& cache : caches) {
                if ((*cache).find(missing, lookup, &found)) return false;
            }
        }
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        for (int iteration = 0; iteration < work.iterations; ++iteration) {
            for (int message = 0; message < work.messages; ++message) {
                SkResourceCache::PostPurgeSharedID(static_cast<uint64_t>(message + 1));
            }
            for (const std::unique_ptr<SkResourceCache>& cache : caches) {
                if ((*cache).find(missing, lookup, &found)) return false;
            }
        }
        const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        const std::chrono::duration<double> elapsed = end - start;
        std::printf("%d,%d,%d,%.9f,%zu\n", work.caches, work.messages,
                    work.iterations, elapsed.count(), sizeof(SkResourceCache));
    }
    return true;
}

}
int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) {
        const bool passed = measure();
        return passed ? 0 : 7;
    }
    if (!keys()) return 1;
    if (!eviction()) return 2;
    if (!duplicates()) return 3;
    if (!messages()) return 4;
    if (!budgets()) return 5;
    if (!synchronized()) return 6;
    std::puts("resource cache: keys, eviction, pins, messages, budgets, concurrent operations PASS");
    return 0;
}
