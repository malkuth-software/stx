/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkIDChangeListener_DEFINED
#define SkIDChangeListener_DEFINED

#include "include/core/SkRefCnt.h"
#include "include/private/SkMutex.h"
#include "include/private/SkTArray.h"
#include "include/private/SkThreadAnnotations.h"

#include <atomic>

/**
 * Used to be notified when a gen/unique ID is invalidated, typically to preemptively purge
 * associated items from a cache that are no longer reachable. The listener can
 * be marked for deregistration if the cached item is removed before the listener is
 * triggered. This prevents unbounded listener growth when cache items are routinely
 * removed before the gen ID/unique ID is invalidated.
 */
class SkIDChangeListener : public SkRefCnt {
public:
    SkIDChangeListener();

    ~SkIDChangeListener() override;

    virtual void changed() = 0;

    /**
     * Mark the listener is no longer needed. It should be removed and changed() should not be
     * called.
     */
    void markShouldDeregister() { fShouldDeregister.store(true, std::memory_order_relaxed); }

    /** Indicates whether markShouldDeregister was called. */
    bool shouldDeregister() {
        const bool deregister = fShouldDeregister.load(std::memory_order_acquire);
        return deregister;
    }

    /** Owns listener references. Registration, counting, reset and changed() are
     *  serialized by the list mutex. Notifications run under that mutex, so a
     *  listener must not reenter the same list. Notification order is unspecified:
     *  removing deregistered entries may move the final entry into their place.
     *  Destruction requires exclusive ownership and notifies remaining listeners.
     *  Any context borrowed by a listener must outlive that listener until notification
     *  or deregistration and release have completed.
     */
    class List {
    public:
        List();

        ~List();

        /**
         * Add a new listener to the list. It must not already be deregistered. Also clears out
         * previously deregistered listeners.
         */
        void add(sk_sp<SkIDChangeListener> listener) SK_EXCLUDES(fMutex);

        /**
         * The number of stored listeners, including deregistered listeners awaiting removal.
         */
        int count() const SK_EXCLUDES(fMutex);

        /** Calls changed() on all listeners that haven't been deregistered and resets the list. */
        void changed() SK_EXCLUDES(fMutex);

        /** Resets without calling changed() on the listeners. */
        void reset() SK_EXCLUDES(fMutex);

    private:
        mutable SkMutex fMutex;
        skia_private::STArray<1, sk_sp<SkIDChangeListener>> fListeners SK_GUARDED_BY(fMutex);
    };

private:
    std::atomic<bool> fShouldDeregister;
};

#endif
