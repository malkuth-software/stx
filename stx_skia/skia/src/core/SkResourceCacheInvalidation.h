// Copyright (c) 2026 Joshuah Rainstar
// SPDX-License-Identifier: MIT
#ifndef SkResourceCacheInvalidation_DEFINED
#define SkResourceCacheInvalidation_DEFINED

#include "include/private/SkMutex.h"
#include <cstdint>
#include <vector>

// Broadcast-only invalidation for live resource caches. The registry observes
// inboxes under its mutex; each inbox owns its pending shared IDs. Post locks
// registry then inbox. Poll takes only the inbox lock. Destruction unregisters
// under the registry lock, waiting for any delivery already in progress.
// The owner must separately finish its own poll calls before destroying an inbox.
class SkResourceCacheInvalidationInbox final {
public:
    SkResourceCacheInvalidationInbox();
    ~SkResourceCacheInvalidationInbox();
    SkResourceCacheInvalidationInbox(const SkResourceCacheInvalidationInbox&) = delete;
    SkResourceCacheInvalidationInbox& operator=(const SkResourceCacheInvalidationInbox&) = delete;

    // Deliver once to every inbox registered at the registry lock's acquisition.
    // Concurrent posts have one serialized order, preserved by every recipient.
    // Zero is ignored; duplicate IDs remain distinct messages. No history replay
    // occurs for inboxes registered after a post. No caller callback runs here.
    static void Post(uint64_t sharedID);

    // Replace output with this inbox's pending IDs in delivery order, leaving the
    // inbox empty. Swapping transfers allocation ownership and recycles output's
    // former capacity for future posts. Output is exclusively owned by the caller.
    // References into its old contents expire on this call.
    void poll(std::vector<uint64_t>& output);

private:
    SkMutex fMutex{};
    std::vector<uint64_t> fPending{};
    class Registry;
    static Registry& GetRegistry();
};
#endif
