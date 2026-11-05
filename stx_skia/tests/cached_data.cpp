#include "include/core/SkRefCnt.h"
#include "include/private/SkMalloc.h"
#include "include/private/chromium/SkDiscardableMemory.h"
#include "src/core/SkCachedData.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

namespace {
struct Events {
    int allocations_destroyed = 0;
    int objects_destroyed = 0;
    int locks = 0;
    int unlocks = 0;
    int changes = 0;
    bool valid = true;
    std::array<bool, 32> present{};
};

class Discardable final : public SkDiscardableMemory {
public:
    explicit Discardable(Events& events) : fEvents(events) {}
    ~Discardable() override {
        fEvents.valid = fEvents.valid && !fLocked;
        ++fEvents.allocations_destroyed;
    }
    bool lock() override {
        ++fEvents.locks;
        fEvents.valid = fEvents.valid && !fLocked;
        if (fPurged) return false;
        fLocked = true;
        return true;
    }
    void* data() override {
        fEvents.valid = fEvents.valid && fLocked && !fPurged;
        return fPixels.data();
    }
    void unlock() override {
        fEvents.valid = fEvents.valid && fLocked;
        fLocked = false;
        ++fEvents.unlocks;
    }
    void purge() {
        fEvents.valid = fEvents.valid && !fLocked;
        fPurged = true;
    }
private:
    Events& fEvents;
    std::array<uint8_t, 64> fPixels{};
    bool fLocked = true;
    bool fPurged = false;
};

class ObservedData final : public SkCachedData {
public:
    ObservedData(void* allocation, Events& events)
        : SkCachedData(allocation, 64), fEvents(events) {}
    ObservedData(Discardable* allocation, Events& events)
        : SkCachedData(64, allocation), fEvents(events) {}
    ~ObservedData() override { ++fEvents.objects_destroyed; }
protected:
#ifdef STX_SKIA_REFERENCE
    void onDataChange(void* previous, void* next) override {
#else
    void onDataChange(void* previous, void* next) const override {
#endif
        fEvents.valid = fEvents.valid && data() == previous && previous != next;
        if (fEvents.changes >= static_cast<int>(fEvents.present.size())) {
            fEvents.valid = false;
            return;
        }
        fEvents.present[static_cast<size_t>(fEvents.changes)] = next != nullptr;
        ++fEvents.changes;
    }
private:
    Events& fEvents;
};

// This owner represents exactly one cache reference, distinct from client refs.
class CacheReference final {
public:
    explicit CacheReference(SkCachedData& data) : fData(&data) { data.attachToCacheAndRef(); }
    ~CacheReference() { reset(); }
    CacheReference(const CacheReference&) = delete;
    CacheReference& operator=(const CacheReference&) = delete;
    void reset() {
        SkCachedData* previous = fData;
        fData = nullptr;
        if (previous != nullptr) (*previous).detachFromCacheAndUnref();
    }
private:
    SkCachedData* fData;
};

bool state(const SkCachedData& data, int references, bool cached, bool locked, bool present) {
    const bool matches = data.testing_only_getRefCnt() == references &&
        data.testing_only_isInCache() == cached && data.testing_only_isLocked() == locked &&
        (data.data() != nullptr) == present;
    return matches;
}

bool transitions(bool discardable, bool cache_last, bool purge) {
    Events events;
    Discardable* memory = discardable ? new Discardable(events) : nullptr;
    sk_sp<ObservedData> client;
    if (discardable) {
        client.reset(new ObservedData(memory, events));
    } else {
        void* allocation = sk_malloc_throw(64);
        client.reset(new ObservedData(allocation, events));
    }
    ObservedData& data = *client;
    if (!state(data, 1, false, true, true) || data.size() != 64) return false;
    const void* pixels = data.data();
    static_cast<uint8_t*>(data.writable_data())[0] = 91;
    {
        sk_sp<ObservedData> other = client;
        if (!state(data, 2, false, true, true)) return false;
    }
    {
        CacheReference cache(data);
        if (!state(data, 2, true, true, true)) return false;
        client.reset();
        if (!state(data, 1, true, false, false) || events.changes != 1 || events.present[0]) return false;
        if (purge) (*memory).purge();
        data.ref();
        client.reset(&data); // Adopt the reference just acquired, no additional ref.
        if (!state(data, 2, true, true, !purge)) return false;
        if (!purge && (data.data() != pixels || static_cast<const uint8_t*>(data.data())[0] != 91)) return false;
        if (cache_last) {
            client.reset();
            if (!state(data, 1, true, false, false)) return false;
        } else {
            cache.reset();
            if (!state(data, 1, false, true, !purge)) return false;
        }
    }
    client.reset();
    if (!events.valid || events.objects_destroyed != 1) return false;
    if (discardable && (events.allocations_destroyed != 1 || events.locks != 1 ||
        events.unlocks != (purge ? 1 : 2))) return false;
    if (events.changes != (purge ? 1 : 3)) return false;
    return true;
}

void share_worker(const SkCachedData* data, std::atomic<bool>* valid) {
    for (int i = 0; i < 20000; ++i) {
        (*data).ref();
        const uint8_t* pixels = static_cast<const uint8_t*>((*data).data());
        if (pixels == nullptr || pixels[0] != 47) (*valid).store(false);
        (*data).unref();
    }
}

bool concurrent_clients() {
    Events events;
    std::atomic<bool> valid{true};
    void* allocation = sk_malloc_throw(64);
    sk_sp<ObservedData> client(new ObservedData(allocation, events));
    static_cast<uint8_t*>((*client).writable_data())[0] = 47;
    std::array<std::thread, 4> workers;
    for (std::thread& worker : workers) worker = std::thread(share_worker, client.get(), &valid);
    for (std::thread& worker : workers) worker.join();
    if (!state(*client, 1, false, true, true) || !valid.load()) return false;
    client.reset();
    const bool success = events.valid && events.changes == 1 && events.objects_destroyed == 1;
    return success;
}

bool repeated_failed_lock() {
    Events events;
    Discardable* memory = new Discardable(events);
    sk_sp<ObservedData> client(new ObservedData(memory, events));
    ObservedData& data = *client;
    {
        CacheReference cache(data);
        client.reset();
        (*memory).purge();
        for (int i = 0; i < 4; ++i) {
            data.ref();
            client.reset(&data);
            if (data.data() != nullptr) return false;
            client.reset();
        }
    }
    const bool success = events.valid && events.locks == 4 && events.unlocks == 1 &&
        events.changes == 1 && events.allocations_destroyed == 1 && events.objects_destroyed == 1;
    return success;
}
}

int main() {
    for (const bool cache_last : {false, true}) {
        if (!transitions(false, cache_last, false) || !transitions(true, cache_last, false) ||
            !transitions(true, cache_last, true)) return 1;
    }
    if (!concurrent_clients() || !repeated_failed_lock()) return 2;
#ifndef STX_SKIA_REFERENCE
    // Reference/cache state is explicitly mutable even for a const heap object.
    Events events;
    void* allocation = sk_malloc_throw(64);
    const ObservedData* data = new const ObservedData(allocation, events);
    (*data).ref();
    (*data).unref();
    (*data).unref();
    if (!events.valid || events.objects_destroyed != 1 || events.changes != 1) return 3;
#endif
    std::puts("cached data: transitions, discard/reacquire, callbacks and shared references PASS");
    return 0;
}
