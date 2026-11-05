#include "src/core/SkResourceCache.h"
#ifdef STX_SKIA_REFERENCE
#include "src/core/SkMessageBus.h"
#include "include/private/SkTArray.h"
struct TestInvalidation { uint64_t sharedID; };
static bool SkShouldPostMessageToBus(const TestInvalidation&, uint32_t) { return true; }
DECLARE_SKMESSAGEBUS_MESSAGE(TestInvalidation, uint32_t, true)
#else
#include "src/core/SkResourceCacheInvalidation.h"
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <vector>

namespace {
#ifdef STX_SKIA_REFERENCE
using ReferenceBus = SkMessageBus<TestInvalidation, uint32_t>;
class Inbox final {
public:
    void poll(std::vector<uint64_t>& output) {
        skia_private::TArray<TestInvalidation> pending{};
        inbox.poll(&pending);
        output.clear();
        for (const TestInvalidation& message : pending) output.push_back(message.sharedID);
    }
    static void Post(uint64_t sharedID) {
        if (sharedID != 0) ReferenceBus::Post(TestInvalidation{sharedID});
    }
private:
    ReferenceBus::Inbox inbox{SK_InvalidUniqueID};
};
#else
using Inbox = SkResourceCacheInvalidationInbox;
static_assert(!std::is_copy_constructible_v<Inbox>);
static_assert(!std::is_move_constructible_v<Inbox>);
#endif

bool delivery() {
    Inbox first{};
    std::vector<uint64_t> output{999};
    first.poll(output);
    if (!output.empty()) return false;
    Inbox::Post(1);
    {
        Inbox second{};
        Inbox::Post(0);
        Inbox::Post(2);
        Inbox::Post(2);
        Inbox::Post(3);
        second.poll(output);
        if (output != std::vector<uint64_t>{2, 2, 3}) return false;
        first.poll(output);
        if (output != std::vector<uint64_t>{1, 2, 2, 3}) return false;
        second.poll(output);
        if (!output.empty()) return false;
        Inbox::Post(4); // Pending data is owned by each inbox when second ends.
    }
    Inbox third{};
    Inbox::Post(5);
    first.poll(output);
    if (output != std::vector<uint64_t>{4, 5}) return false;
    third.poll(output);
    if (output != std::vector<uint64_t>{5}) return false;
    return true;
}

bool buffer_reuse() {
#ifndef STX_SKIA_REFERENCE
    Inbox inbox{};
    std::vector<uint64_t> output{};
    std::array<const uint64_t*, 2> buffers{};
    for (int batch = 0; batch < 12; ++batch) {
        for (uint64_t id = 1; id <= 64; ++id) Inbox::Post(id);
        inbox.poll(output);
        if (output.size() != 64) return false;
        for (size_t index = 0; index < output.size(); ++index) {
            if (output[index] != index + 1) return false;
        }
        const size_t slot = static_cast<size_t>(batch % 2);
        if (batch < 2) buffers[slot] = output.data();
        else if (output.data() != buffers[slot]) return false;
        output.clear();
    }
    if (buffers[0] == nullptr || buffers[1] == nullptr || buffers[0] == buffers[1]) return false;
#endif
    return true;
}

struct Producer {
    uint64_t number;
    std::atomic<bool>* start;
};
void produce(Producer* producer) {
    while (!(*(*producer).start).load(std::memory_order_acquire)) std::this_thread::yield();
    for (uint64_t index = 0; index < 1000; ++index) {
        const uint64_t id = (*producer).number * 1000 + index + 1;
        Inbox::Post(id);
    }
}
void churn(std::atomic<bool>* start) {
    while (!(*start).load(std::memory_order_acquire)) std::this_thread::yield();
    for (int index = 0; index < 300; ++index) {
        Inbox transient{};
        std::vector<uint64_t> output{};
        transient.poll(output);
    }
}
bool concurrent() {
    Inbox first{};
    Inbox second{};
    std::atomic<bool> start{false};
    std::array<Producer, 4> producers{{{0, &start}, {1, &start}, {2, &start}, {3, &start}}};
    std::array<std::thread, 4> threads{};
    for (size_t index = 0; index < threads.size(); ++index) {
        threads[index] = std::thread(produce, &producers[index]);
    }
    std::thread changing(churn, &start);
    start.store(true, std::memory_order_release);
    for (std::thread& thread : threads) thread.join();
    changing.join();
    std::vector<uint64_t> firstOutput{};
    std::vector<uint64_t> secondOutput{};
    first.poll(firstOutput);
    second.poll(secondOutput);
    if (firstOutput != secondOutput || firstOutput.size() != 4000) return false;
    std::array<uint64_t, 4> counts{};
    for (const uint64_t id : firstOutput) {
        if (id == 0 || id > 4000) return false;
        const size_t producer = static_cast<size_t>((id - 1) / 1000);
        const uint64_t sequence = (id - 1) % 1000;
        if (sequence != counts[producer]) return false;
        ++counts[producer];
    }
    for (const uint64_t count : counts) if (count != 1000) return false;
    return true;
}
}
int main() {
    if (!delivery()) return 1;
    if (!buffer_reuse()) return 2;
    if (!concurrent()) return 3;
    std::printf("cache invalidation: delivery, registration, buffer reuse, concurrent ordering PASS; cache=%zu bytes\n",
                sizeof(SkResourceCache));
    return 0;
}
