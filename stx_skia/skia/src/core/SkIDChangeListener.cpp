/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/SkIDChangeListener.h"

#include "include/private/SkAssert.h"

#include <utility>
SkIDChangeListener::SkIDChangeListener() : fShouldDeregister(false) {}

SkIDChangeListener::~SkIDChangeListener() = default;

using List = SkIDChangeListener::List;

List::List() = default;

List::~List() {
    // We don't need the mutex. No other thread should have this list while it's being
    // destroyed.
    for (sk_sp<SkIDChangeListener>& listener : fListeners) {
        SkIDChangeListener& target = *listener;
        if (!target.shouldDeregister()) target.changed();
    }
}

void List::add(sk_sp<SkIDChangeListener> listener) {
    if (!listener) {
        return;
    }
    SkASSERT(!(*listener).shouldDeregister());

    SkAutoMutexExclusive lock(fMutex);
    // Clean out any stale listeners before we append the new one.
    int index = 0;
    while (index < fListeners.size()) {
        SkIDChangeListener& target = *fListeners[index];
        if (target.shouldDeregister()) {
            // Revisit the replacement moved from the end. Registration order is not promised.
            fListeners.removeShuffle(index);
        } else {
            ++index;
        }
    }
    fListeners.push_back(std::move(listener));
}

int List::count() const {
    SkAutoMutexExclusive lock(fMutex);
    const int count = fListeners.size();
    return count;
}

void List::changed() {
    SkAutoMutexExclusive lock(fMutex);
    for (sk_sp<SkIDChangeListener>& listener : fListeners) {
        SkIDChangeListener& target = *listener;
        if (!target.shouldDeregister()) target.changed();
    }
    fListeners.clear();
}

void List::reset() {
    SkAutoMutexExclusive lock(fMutex);
    fListeners.clear();
}
