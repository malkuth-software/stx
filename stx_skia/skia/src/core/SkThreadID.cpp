/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/SkThreadID.h"
#include <bit>
#include <cstring>

#ifdef SK_BUILD_FOR_WIN
#include "src/core/SkLeanWindows.h"
SkThreadID SkGetThreadID() {
    const DWORD native = GetCurrentThreadId();
    const SkThreadID identity = static_cast<SkThreadID>(native);
    return identity;
}
#else
#include <pthread.h>
SkThreadID SkGetThreadID() {
    const pthread_t native = pthread_self();
    static_assert(sizeof(native) <= sizeof(uint64_t));
    // Preserve the opaque native representation on the admitted Linux/macOS
    // targets, without pointer-to-signed-integer or out-of-range numeric casts.
    uint64_t bits = 0;
    std::memcpy(&bits, &native, sizeof(native));
    const SkThreadID identity = std::bit_cast<SkThreadID>(bits);
    return identity;
}
#endif
