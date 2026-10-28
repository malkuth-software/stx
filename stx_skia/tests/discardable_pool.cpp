#include "src/lazy/SkDiscardableMemoryPool.h"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <thread>

namespace {
// Test owner balances the interface's lock-before-use/unlock-before-delete rule,
// including early failure returns. A failed reacquisition leaves it unlocked.
class Block {
public:
    Block(SkDiscardableMemoryPool& pool, size_t bytes)
        : memory(pool.create(bytes)), locked(memory != nullptr) {}
    ~Block() { unlock(); }
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    bool exists() const {
        const bool present = memory != nullptr;
        return present;
    }
    bool lock() {
        assert(memory != nullptr && !locked);
        locked = (*memory).lock();
        return locked;
    }
    void unlock() {
        if (locked) {
            (*memory).unlock();
            locked = false;
        }
    }
    uint8_t* data() {
        assert(locked);
        uint8_t* bytes = static_cast<uint8_t*>((*memory).data());
        return bytes;
    }
private:
    std::unique_ptr<SkDiscardableMemory> memory{};
    bool locked = false;
};

bool policy() {
    sk_sp<SkDiscardableMemoryPool> pool = SkDiscardableMemoryPool::Make(24);
#ifndef STX_SKIA_REFERENCE
    Block zero(*pool, 0);
    if (zero.exists() || (*pool).getRAMUsed() != 0) return false;
#endif
    {
        Block first(*pool, 8);
        Block second(*pool, 8);
        Block third(*pool, 8);
        if (!first.exists() || !second.exists() || !third.exists()) return false;
        first.data()[0] = 41; second.data()[0] = 42; third.data()[0] = 43;
        if ((*pool).getRAMUsed() != 24 || (*pool).getRAMBudget() != 24) return false;
        first.unlock(); second.unlock(); third.unlock();
        if (!first.lock() || first.data()[0] != 41) return false;
        first.unlock();
        (*pool).setRAMBudget(16);
        if (second.lock() || (*pool).getRAMUsed() != 16) return false;
        if (!third.lock() || third.data()[0] != 43) return false;
        third.unlock();
        (*pool).setRAMBudget(8);
        if (first.lock() || (*pool).getRAMUsed() != 8) return false;
        if (!third.lock()) return false;
        (*pool).dumpPool();
        if ((*pool).getRAMUsed() != 8) return false;
        (*pool).setRAMBudget(0);
        if ((*pool).getRAMUsed() != 8) return false;
        third.unlock();
        if ((*pool).getRAMUsed() != 0 || third.lock()) return false;
    }
    if ((*pool).getRAMUsed() != 0) return false;
#if SK_LAZY_CACHE_STATS
    if ((*pool).getCacheHits() != 3 || (*pool).getCacheMisses() != 3) return false;
    (*pool).resetCacheHitsAndMisses();
    if ((*pool).getCacheHits() != 0 || (*pool).getCacheMisses() != 0) return false;
#endif
    sk_sp<SkDiscardableMemoryPool> zeroBudget = SkDiscardableMemoryPool::Make(0);
    {
        Block initiallyLocked(*zeroBudget, 8);
        if (!initiallyLocked.exists() || (*zeroBudget).getRAMUsed() != 8) return false;
        initiallyLocked.unlock();
        if ((*zeroBudget).getRAMUsed() != 0 || initiallyLocked.lock()) return false;
    }
    // The block's reference keeps the pool alive after the caller drops its owner.
    sk_sp<SkDiscardableMemoryPool> transient = SkDiscardableMemoryPool::Make(8);
    Block survivor(*transient, 8);
    if (!survivor.exists()) return false;
    survivor.data()[0] = 99;
    transient.reset();
    survivor.unlock();
    if (!survivor.lock() || survivor.data()[0] != 99) return false;
    return true;
}

struct Worker {
    SkDiscardableMemoryPool* pool = nullptr;
    std::atomic<bool>* start = nullptr;
    int id = 0;
    bool passed = false;
};
void work(Worker& worker) {
    while (!(*worker.start).load(std::memory_order_acquire)) std::this_thread::yield();
    SkDiscardableMemoryPool& pool = *worker.pool;
    for (int index = 0; index < 1000; ++index) {
        const size_t bytes = static_cast<size_t>(8 + index % 7 * 8);
        const uint8_t marker = static_cast<uint8_t>((index + worker.id) % 251);
        Block block(pool, bytes);
        if (!block.exists()) return;
        std::memset(block.data(), marker, bytes);
        block.unlock();
        if (block.lock()) {
            const uint8_t* data = block.data();
            for (size_t offset = 0; offset < bytes; ++offset) {
                if (data[offset] != marker) return;
            }
            block.unlock();
        }
    }
    worker.passed = true;
}

struct Observer {
    SkDiscardableMemoryPool* pool = nullptr;
    std::atomic<bool>* start = nullptr;
    bool passed = false;
};
void observe(Observer& observer) {
    while (!(*observer.start).load(std::memory_order_acquire)) std::this_thread::yield();
    SkDiscardableMemoryPool& pool = *observer.pool;
    for (int index = 0; index < 3000; ++index) {
        const size_t budget = index % 2 == 0 ? 0 : 512;
        pool.setRAMBudget(budget);
        if (index % 7 == 0) pool.dumpPool();
#ifndef STX_SKIA_REFERENCE
        // Original getters do not synchronize. Exercise concurrent snapshots only
        // in the candidate, without using the original data race as a test oracle.
        const size_t used = pool.getRAMUsed();
        if (used > 4 * 56 || pool.getRAMBudget() != budget) return;
#if SK_LAZY_CACHE_STATS
        if (pool.getCacheHits() < 0 || pool.getCacheMisses() < 0) return;
#endif
#endif
    }
    observer.passed = true;
}

bool concurrent() {
    sk_sp<SkDiscardableMemoryPool> pool = SkDiscardableMemoryPool::Make(512);
    std::atomic<bool> start{false};
    std::array<Worker, 4> workers{};
    Observer observer{pool.get(), &start, false};
    std::array<std::thread, 4> threads{};
    for (size_t index = 0; index < workers.size(); ++index) {
        workers[index] = Worker{pool.get(), &start, static_cast<int>(index), false};
        threads[index] = std::thread(work, std::ref(workers[index]));
    }
    std::thread observerThread(observe, std::ref(observer));
    start.store(true, std::memory_order_release);
    for (std::thread& thread : threads) thread.join();
    observerThread.join();
    for (const Worker& worker : workers) if (!worker.passed) return false;
    if (!observer.passed || (*pool).getRAMUsed() != 0) return false;
#if SK_LAZY_CACHE_STATS
    const int total = (*pool).getCacheHits() + (*pool).getCacheMisses();
    if (total != 4000) return false;
#endif
    return true;
}

enum class Workload { Allocate, Head, Alternating, Snapshots };
template <Workload Mode>
bool measure(const char* name) {
    sk_sp<SkDiscardableMemoryPool> pool = SkDiscardableMemoryPool::Make(4096);
    Block first(*pool, 64);
    Block second(*pool, 64);
    if (!first.exists() || !second.exists()) return false;
    first.unlock(); second.unlock();
    constexpr int iterations = 1000000;
    uint64_t digest = 0;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (int index = 0; index < iterations; ++index) {
        if constexpr (Mode == Workload::Allocate) {
            Block block(*pool, 64);
            if (!block.exists()) return false;
            ++digest;
        } else if constexpr (Mode == Workload::Head || Mode == Workload::Alternating) {
            Block& block = Mode == Workload::Head || index % 2 == 0 ? first : second;
            if (!block.lock()) return false;
            block.unlock();
            ++digest;
        } else {
            digest += (*pool).getRAMUsed();
            digest += (*pool).getRAMBudget();
        }
    }
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;
    std::printf("%s,%d,%.9f,%llu\n", name, iterations, elapsed.count(),
                static_cast<unsigned long long>(digest));
    return true;
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) {
        if (!measure<Workload::Allocate>("allocate_unlock_destroy")) return 10;
        if (!measure<Workload::Head>("reacquire_head")) return 11;
        if (!measure<Workload::Alternating>("reacquire_alternating")) return 12;
        if (!measure<Workload::Snapshots>("usage_and_budget")) return 13;
        return 0;
    }
    if (!policy()) return 1;
    if (!concurrent()) return 2;
    std::puts("discardable pool: budgets, order, payload retention, lifetime and 4000 concurrent block cycles PASS");
    return 0;
}
