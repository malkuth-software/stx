/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkSemaphore_DEFINED
#define SkSemaphore_DEFINED

#include "include/private/SkAPI.h"
#include "include/private/SkAssert.h"
#include "include/private/SkOnce.h"
#include "include/private/SkThreadAnnotations.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>

class SkSemaphore {
public:
    // Nonnegative initial permits. Callers keep permit/waiter totals in int's
    // representable range and stop every signal/wait before destruction.
    constexpr SkSemaphore(int count = 0) : fCount(count) { SkASSERT(count >= 0); }

    // Cleanup the underlying OS semaphore.
    SK_SPI ~SkSemaphore();

    // Publish n nonnegative permits with release ordering.
    // Generally it's better to call signal(n) instead of signal() n times.
    void signal(int n = 1);

    // Claim one permit with acquire ordering; block if none was available.
    // The count need not be nonnegative when a particular waiter is released.
    void wait();

    // Try one acquire CAS when permits appear available. Failure may be spurious
    // or due to contention and consumes no permit. Never blocks or spins.
    SK_SPI bool try_wait();

private:
    // This implementation follows the general strategy of
    //     'A Lightweight Semaphore with Partial Spinning'
    // found here
    //     http://preshing.com/20150316/semaphores-are-surprisingly-versatile/
    // That article (and entire blog) are very much worth reading.
    //
    // We wrap an OS-provided semaphore with a user-space atomic counter that
    // lets us avoid interacting with the OS semaphore unless strictly required:
    // moving the count from >=0 to <0 or vice-versa, i.e. sleeping or waking threads.
    struct OSSemaphore;

    SK_SPI void osSignal(int n);
    SK_SPI void osWait();
    static void InitializeOSSemaphore(SkSemaphore& owner);

    std::atomic<int> fCount;
    SkOnce fOSSemaphoreOnce{};
    // Sole owner at the OS boundary, initialized once and released by destructor.
    // Keep constexpr construction without requiring C++23 constexpr unique_ptr.
    OSSemaphore* fOSSemaphore = nullptr;
};

inline void SkSemaphore::signal(int n) {
    SkASSERT(n >= 0);
    const int previous = fCount.fetch_add(n, std::memory_order_release);
    SkASSERT(previous < 0 || n <= std::numeric_limits<int>::max() - previous);

    // A negative previous count represents unsatisfied wait claims. Wake up to
    // n of them even if the resulting count remains negative. Widen before
    // negation so the minimum int representation cannot overflow this expression.
    if (previous < 0 && n > 0) {
        const int64_t waiting = -static_cast<int64_t>(previous);
        const int64_t wakeCount = std::min(waiting, static_cast<int64_t>(n));
        const int toSignal = static_cast<int>(wakeCount);
        osSignal(toSignal);
    }
}

inline void SkSemaphore::wait() {
    // Since this fetches the value before the subtract, zero and below means that there are no
    // resources left, so the thread needs to wait.
    const int previous = fCount.fetch_sub(1, std::memory_order_acquire);
    SkASSERT(previous > std::numeric_limits<int>::min());
    if (previous <= 0) {
        SK_POTENTIALLY_BLOCKING_REGION_BEGIN;
        osWait();
        SK_POTENTIALLY_BLOCKING_REGION_END;
    }
}

#endif//SkSemaphore_DEFINED
