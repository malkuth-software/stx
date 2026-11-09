/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkOnce_DEFINED
#define SkOnce_DEFINED

#include "include/private/SkThreadAnnotations.h"

#include <atomic>
#include <cstdint>
#include <utility>

// SkOnce provides call-once guarantees for Skia, much like std::once_flag/std::call_once().
//
// The winning call runs its target synchronously. Other callers spin until its
// release publication, then observe its writes through acquire. The target and
// arguments are borrowed for the call only; the target must not throw or reenter
// this same flag. Finish all calls before destroying the flag or published state.

class SkOnce {
public:
    constexpr SkOnce() = default;

    template <typename Fn, typename... Args>
    void operator()(Fn&& fn, Args&&... args) {
        uint8_t state = fState.load(std::memory_order_acquire);

        if (state == Done) {
            return;
        }

        // If it looks like no one has started calling fn(), try to claim that job.
        if (state == NotStarted && fState.compare_exchange_strong(state, Claimed,
                                                                  std::memory_order_relaxed,
                                                                  std::memory_order_relaxed)) {
            // Great!  We'll run fn() then notify the other threads by releasing Done into fState.
            fn(std::forward<Args>(args)...);
            fState.store(Done, std::memory_order_release);
            return;
        }

        // Some other thread is calling fn().
        // We'll just spin here acquiring until it releases Done into fState.
        SK_POTENTIALLY_BLOCKING_REGION_BEGIN;
        while (fState.load(std::memory_order_acquire) != Done) { /*spin*/ }
        SK_POTENTIALLY_BLOCKING_REGION_END;
    }

private:
    enum State : uint8_t { NotStarted, Claimed, Done};
    std::atomic<uint8_t> fState{NotStarted};
};

#endif  // SkOnce_DEFINED
