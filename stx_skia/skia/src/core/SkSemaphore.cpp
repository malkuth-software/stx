/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/SkSemaphore.h"
#include "include/private/SkFeatures.h"

#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
#include <dispatch/dispatch.h>

struct SkSemaphore::OSSemaphore {
    dispatch_semaphore_t fSemaphore = nullptr;

    OSSemaphore() {
        fSemaphore = dispatch_semaphore_create(0);
        if (fSemaphore == nullptr) {
            SK_ABORT("Cannot create OS semaphore");
        }
    }
    ~OSSemaphore() { dispatch_release(fSemaphore); }

    void signal(int count) {
        for (int index = 0; index < count; ++index) {
            // A nonzero result reports a woken waiter, not failure.
            dispatch_semaphore_signal(fSemaphore);
        }
    }
    void wait() {
        const long result = dispatch_semaphore_wait(fSemaphore, DISPATCH_TIME_FOREVER);
        if (result != 0) {
            SK_ABORT("Cannot wait on OS semaphore");
        }
    }
};
#elif defined(SK_BUILD_FOR_WIN)
#include "src/core/SkLeanWindows.h"

struct SkSemaphore::OSSemaphore {
    HANDLE fSemaphore = nullptr;

    OSSemaphore() {
        fSemaphore = CreateSemaphoreW(nullptr, 0, MAXLONG, nullptr);
        if (fSemaphore == nullptr) {
            SK_ABORT("Cannot create OS semaphore");
        }
    }
    ~OSSemaphore() {
        const BOOL closed = CloseHandle(fSemaphore);
        if (!closed) {
            SK_ABORT("Cannot close OS semaphore");
        }
    }
    void signal(int count) {
        const BOOL released = ReleaseSemaphore(fSemaphore, static_cast<LONG>(count), nullptr);
        if (!released) {
            SK_ABORT("Cannot signal OS semaphore");
        }
    }
    void wait() {
        const DWORD result = WaitForSingleObject(fSemaphore, INFINITE);
        if (result != WAIT_OBJECT_0) {
            SK_ABORT("Cannot wait on OS semaphore");
        }
    }
};
#else
// macOS uses dispatch above: its unnamed POSIX semaphores are not supported.
#include <cerrno>
#include <semaphore.h>

struct SkSemaphore::OSSemaphore {
    sem_t fSemaphore{};

    OSSemaphore() {
        const int result = sem_init(&fSemaphore, 0, 0);
        if (result != 0) {
            SK_ABORT("Cannot create OS semaphore");
        }
    }
    ~OSSemaphore() {
        const int result = sem_destroy(&fSemaphore);
        if (result != 0) {
            SK_ABORT("Cannot destroy OS semaphore");
        }
    }
    void signal(int count) {
        for (int index = 0; index < count; ++index) {
            const int result = sem_post(&fSemaphore);
            if (result != 0) {
                SK_ABORT("Cannot signal OS semaphore");
            }
        }
    }
    void wait() {
        for (;;) {
            const int result = sem_wait(&fSemaphore);
            if (result == 0) {
                return;
            }
            if (errno != EINTR) {
                SK_ABORT("Cannot wait on OS semaphore");
            }
        }
    }
};
#endif

SkSemaphore::~SkSemaphore() {
    // Caller has joined all waiters/signallers, including lazy initialization.
    delete fOSSemaphore;
}

void SkSemaphore::InitializeOSSemaphore(SkSemaphore& owner) {
    owner.fOSSemaphore = new OSSemaphore;
}

void SkSemaphore::osSignal(int count) {
    fOSSemaphoreOnce(InitializeOSSemaphore, *this);
    (*fOSSemaphore).signal(count);
}

void SkSemaphore::osWait() {
    fOSSemaphoreOnce(InitializeOSSemaphore, *this);
    (*fOSSemaphore).wait();
}

bool SkSemaphore::try_wait() {
    int count = fCount.load(std::memory_order_relaxed);
    if (count <= 0) {
        return false;
    }
    const int remaining = count - 1;
    const bool acquired = fCount.compare_exchange_weak(count, remaining,
                                                      std::memory_order_acquire,
                                                      std::memory_order_acquire);
    return acquired;
}
