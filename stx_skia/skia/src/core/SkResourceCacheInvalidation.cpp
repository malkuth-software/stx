// Copyright (c) 2026 Joshuah Rainstar
// SPDX-License-Identifier: MIT
#include "src/core/SkResourceCacheInvalidation.h"
#include <cstddef>

class SkResourceCacheInvalidationInbox::Registry final {
public:
    void registerInbox(SkResourceCacheInvalidationInbox& inbox) {
        SkAutoMutexExclusive lock(fMutex);
        fInboxes.push_back(&inbox);
    }

    void unregisterInbox(SkResourceCacheInvalidationInbox& inbox) {
        SkAutoMutexExclusive lock(fMutex);
        const size_t count = fInboxes.size();
        for (size_t index = 0; index < count; ++index) {
            if (fInboxes[index] == &inbox) {
                // Registration order is not an observable delivery guarantee.
                fInboxes[index] = fInboxes.back();
                fInboxes.pop_back();
                return;
            }
        }
        SkASSERT(false); // Every completed constructor registered exactly once.
    }

    void post(uint64_t sharedID) {
        SkAutoMutexExclusive lock(fMutex);
        for (SkResourceCacheInvalidationInbox* address : fInboxes) {
            SkResourceCacheInvalidationInbox& inbox = *address;
            SkAutoMutexExclusive inboxLock(inbox.fMutex);
            inbox.fPending.push_back(sharedID);
        }
    }

private:
    SkMutex fMutex{};
    std::vector<SkResourceCacheInvalidationInbox*> fInboxes{}; // Observers, never owners.
};

SkResourceCacheInvalidationInbox::Registry& SkResourceCacheInvalidationInbox::GetRegistry() {
    // Process lifetime permits invalidation from late pixel/cache destruction.
    // Static initialization publishes the complete registry once across threads.
    static Registry* registry = new Registry();
    return *registry;
}

SkResourceCacheInvalidationInbox::SkResourceCacheInvalidationInbox() {
    Registry& registry = GetRegistry();
    registry.registerInbox(*this);
}

SkResourceCacheInvalidationInbox::~SkResourceCacheInvalidationInbox() {
    Registry& registry = GetRegistry();
    registry.unregisterInbox(*this);
    // No registry traversal can borrow this inbox after unregister returns.
}

void SkResourceCacheInvalidationInbox::Post(uint64_t sharedID) {
    if (sharedID == 0) {
        return;
    }
    Registry& registry = GetRegistry();
    registry.post(sharedID);
}

void SkResourceCacheInvalidationInbox::poll(std::vector<uint64_t>& output) {
    output.clear();
    SkAutoMutexExclusive lock(fMutex);
    fPending.swap(output);
}
