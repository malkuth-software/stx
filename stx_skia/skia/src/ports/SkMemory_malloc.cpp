/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/SkAssert.h"
#include "include/private/SkDebug.h"
#include "include/private/SkFeatures.h"
#include "include/private/SkMalloc.h"

#include <algorithm>
#include <cstdlib>

#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
#include <malloc/malloc.h>
#elif defined(SK_BUILD_FOR_UNIX) || defined(SK_BUILD_FOR_WIN)
#include <malloc.h>
#endif

#if defined(SK_DEBUG) && defined(SK_BUILD_FOR_WIN)
#include <intrin.h>
#ifndef FAST_FAIL_FATAL_APP_EXIT
#define FAST_FAIL_FATAL_APP_EXIT 7
#endif
#endif

namespace {
[[noreturn]] void allocation_failure(size_t size) {
    SkDEBUGFAILF("sk_out_of_memory (asked for %zu bytes)", size);
    std::abort();
}

void* require_allocation(size_t size, void* storage) {
    if (size != 0 && storage == nullptr) allocation_failure(size);
    return storage;
}
}

void sk_abort_no_print() {
#if defined(SK_DEBUG) && defined(SK_BUILD_FOR_WIN)
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
#elif defined(__clang__)
    __builtin_trap();
#else
    std::abort();
#endif
}

void sk_out_of_memory() {
    SkDEBUGFAIL("sk_out_of_memory");
    std::abort();
}

void* sk_realloc_throw(void* previous, size_t size) {
    if (size == 0) {
        sk_free(previous);
        return nullptr;
    }
    void* storage = std::realloc(previous, size);
    storage = require_allocation(size, storage);
    return storage;
}

void sk_free(void* storage) {
    // Retain the upstream null fast path; allocator calls remain outside kernels.
    if (storage != nullptr) std::free(storage);
}

void* sk_malloc_flags(size_t size, unsigned flags) {
    void* storage = nullptr;
    if ((flags & SK_MALLOC_ZERO_INITIALIZE) != 0) storage = std::calloc(size, 1);
    else storage = std::malloc(size);
    if ((flags & SK_MALLOC_THROW) != 0) storage = require_allocation(size, storage);
    return storage;
}

size_t sk_malloc_size(void* storage, size_t requested) {
    size_t completeSize = requested;
#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
    const size_t measured = malloc_size(storage);
    completeSize = std::max(measured, requested);
#elif defined(SK_BUILD_FOR_UNIX)
    completeSize = malloc_usable_size(storage);
    SkASSERT(completeSize >= requested);
#elif defined(SK_BUILD_FOR_WIN)
    completeSize = _msize(storage);
    SkASSERT(completeSize >= requested);
#endif
    return completeSize;
}
