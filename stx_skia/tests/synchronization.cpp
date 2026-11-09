#include "include/private/SkMutex.h"
#include "include/private/SkOnce.h"
#include "include/private/SkSemaphore.h"
#include "include/private/SkThreadID.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <thread>

namespace {
bool permits() {
    SkSemaphore empty{};
    if (empty.try_wait()) return false;
    empty.signal(0);
    if (empty.try_wait()) return false;
    empty.signal(9);
    for (int index = 0; index < 9; ++index) empty.wait();
    if (empty.try_wait()) return false;
    SkSemaphore maximum(std::numeric_limits<int>::max());
    maximum.wait();
    maximum.signal();
    // A weak CAS may fail spuriously, so retry without treating one miss as loss.
    for (int attempt = 0; attempt < 1024; ++attempt) {
        if (maximum.try_wait()) return true;
    }
    return false;
}

struct Exchange {
    SkSemaphore ready{};
    SkSemaphore consumed{};
    std::array<int, 32> payload{};
    bool passed = false;
};
void consume(Exchange& exchange, int rounds) {
    bool matched = true;
    for (int round = 1; round <= rounds; ++round) {
        exchange.ready.wait();
        for (size_t index = 0; index < exchange.payload.size(); ++index) {
            if (exchange.payload[index] != round + static_cast<int>(index)) matched = false;
        }
        exchange.consumed.signal();
    }
    exchange.passed = matched;
}
bool publication() {
    Exchange exchange{};
    constexpr int rounds = 2000;
    std::thread reader(consume, std::ref(exchange), rounds);
    for (int round = 1; round <= rounds; ++round) {
        for (size_t index = 0; index < exchange.payload.size(); ++index) {
            exchange.payload[index] = round + static_cast<int>(index);
        }
        exchange.ready.signal();
        exchange.consumed.wait();
    }
    reader.join();
    return exchange.passed;
}

struct BatchWorker {
    SkSemaphore* permits = nullptr;
    SkSemaphore* started = nullptr;
    int completed = 0;
};
void drain(BatchWorker& worker) {
    (*worker.started).signal();
    for (int index = 0; index < 1000; ++index) {
        (*worker.permits).wait();
        ++worker.completed;
    }
}
bool batches() {
    SkSemaphore permits{};
    SkSemaphore started{};
    std::array<BatchWorker, 8> workers{};
    std::array<std::thread, 8> threads{};
    for (size_t index = 0; index < workers.size(); ++index) {
        workers[index] = BatchWorker{&permits, &started, 0};
        threads[index] = std::thread(drain, std::ref(workers[index]));
    }
    for (size_t index = 0; index < workers.size(); ++index) started.wait();
    constexpr std::array<int, 4> sizes{1, 2, 7, 64};
    int remaining = 8000;
    size_t batch = 0;
    while (remaining != 0) {
        const int proposed = sizes[batch % sizes.size()];
        const int count = proposed < remaining ? proposed : remaining;
        permits.signal(count);
        remaining -= count;
        ++batch;
        std::this_thread::yield();
    }
    for (std::thread& thread : threads) thread.join();
    for (const BatchWorker& worker : workers) if (worker.completed != 1000) return false;
    const bool exhausted = !permits.try_wait();
    return exhausted;
}

struct ProtectedState {
    SkMutex mutex{};
    uint64_t count = 0;
    uint64_t complement = ~UINT64_C(0);
};
struct LockWorker {
    ProtectedState* state = nullptr;
    SkSemaphore* ready = nullptr;
    SkSemaphore* start = nullptr;
    SkThreadID identity = 0;
    bool passed = false;
};
void increment(LockWorker& worker) {
    worker.identity = SkGetThreadID();
    (*worker.ready).signal();
    (*worker.start).wait();
    ProtectedState& state = *worker.state;
    bool consistent = true;
    for (int index = 0; index < 10000; ++index) {
        SkAutoMutexExclusive guard(state.mutex);
        state.mutex.assertHeld();
        if (state.complement != ~state.count) consistent = false;
        ++state.count;
        state.complement = ~state.count;
    }
    worker.passed = consistent;
}
bool exclusion() {
    ProtectedState state{};
    SkSemaphore ready{};
    SkSemaphore start{};
    std::array<LockWorker, 8> workers{};
    std::array<std::thread, 8> threads{};
    for (size_t index = 0; index < workers.size(); ++index) {
        workers[index] = LockWorker{&state, &ready, &start, 0, false};
        threads[index] = std::thread(increment, std::ref(workers[index]));
    }
    for (size_t index = 0; index < workers.size(); ++index) ready.wait();
    // All worker threads are alive together, so native IDs must be distinct.
    bool distinct = true;
    for (size_t first = 0; first < workers.size(); ++first) {
        if (workers[first].identity == kIllegalThreadID) distinct = false;
        for (size_t second = first + 1; second < workers.size(); ++second) {
            if (workers[first].identity == workers[second].identity) distinct = false;
        }
    }
    start.signal(8);
    for (std::thread& thread : threads) thread.join();
    for (const LockWorker& worker : workers) if (!worker.passed) return false;
    state.mutex.assertNotHeld();
    const bool passed = distinct && state.count == 80000 && state.complement == ~state.count;
    return passed;
}

struct OnceState {
    SkOnce once{};
    SkSemaphore start{};
    std::array<int, 64> values{};
    int calls = 0;
};
void initialize(OnceState& state) {
    ++state.calls;
    for (size_t index = 0; index < state.values.size(); ++index) {
        state.values[index] = static_cast<int>(index * 7);
    }
}
struct OnceWorker { OnceState* state = nullptr; bool passed = false; };
void initialize_and_read(OnceWorker& worker) {
    OnceState& state = *worker.state;
    state.start.wait();
    for (int iteration = 0; iteration < 1000; ++iteration) {
        state.once(initialize, state);
        if (state.calls != 1) return;
        for (size_t index = 0; index < state.values.size(); ++index) {
            if (state.values[index] != static_cast<int>(index * 7)) return;
        }
    }
    worker.passed = true;
}
bool once_publication() {
    OnceState state{};
    std::array<OnceWorker, 8> workers{};
    std::array<std::thread, 8> threads{};
    for (size_t index = 0; index < workers.size(); ++index) {
        workers[index].state = &state;
        threads[index] = std::thread(initialize_and_read, std::ref(workers[index]));
    }
    state.start.signal(8);
    for (std::thread& thread : threads) thread.join();
    for (const OnceWorker& worker : workers) if (!worker.passed) return false;
    const bool once = state.calls == 1;
    return once;
}

enum class Mode { Semaphore, Mutex, Once };
template <Mode Selected>
void measure(const char* name) {
    SkSemaphore semaphore{1};
    SkMutex mutex{};
    OnceState state{};
    state.once(initialize, state);
    constexpr int iterations = 1000000;
    uint64_t count = 0;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (int index = 0; index < iterations; ++index) {
        if constexpr (Selected == Mode::Semaphore) {
            semaphore.wait();
            ++count;
            semaphore.signal();
        } else if constexpr (Selected == Mode::Mutex) {
            SkAutoMutexExclusive guard(mutex);
            ++count;
        } else {
            state.once(initialize, state);
            count += static_cast<uint64_t>(state.calls);
        }
    }
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;
    std::printf("%s,%d,%.9f,%llu\n", name, iterations, elapsed.count(),
                static_cast<unsigned long long>(count));
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) {
        measure<Mode::Semaphore>("semaphore_permit");
        measure<Mode::Mutex>("mutex_scope");
        measure<Mode::Once>("once_published");
        return 0;
    }
    if (!permits()) return 1;
    if (!publication()) return 2;
    if (!batches()) return 3;
    if (!exclusion()) return 4;
    if (!once_publication()) return 5;
    std::printf("synchronization: permit accounting, publication, batch release, exclusion, once and thread identity PASS; semaphore=%zu mutex=%zu once=%zu\n",
                sizeof(SkSemaphore), sizeof(SkMutex), sizeof(SkOnce));
    return 0;
}
