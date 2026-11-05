// Copyright 2019 Google LLC
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "include/private/SkMalloc.h"
#include "src/core/SkSafeMath.h"

void* sk_calloc_throw(size_t count, size_t elementSize) {
    SkSafeMath arithmetic{};
    const size_t bytes = arithmetic.mul(count, elementSize);
    if (!arithmetic.ok()) sk_out_of_memory();
    void* storage = sk_calloc_throw(bytes);
    return storage;
}

void* sk_malloc_throw(size_t count, size_t elementSize) {
    SkSafeMath arithmetic{};
    const size_t bytes = arithmetic.mul(count, elementSize);
    if (!arithmetic.ok()) sk_out_of_memory();
    void* storage = sk_malloc_throw(bytes);
    return storage;
}

void* sk_realloc_throw(void* previous, size_t count, size_t elementSize) {
    SkSafeMath arithmetic{};
    const size_t bytes = arithmetic.mul(count, elementSize);
    if (!arithmetic.ok()) sk_out_of_memory();
    void* storage = sk_realloc_throw(previous, bytes);
    return storage;
}

void* sk_malloc_canfail(size_t count, size_t elementSize) {
    SkSafeMath arithmetic{};
    const size_t bytes = arithmetic.mul(count, elementSize);
    if (!arithmetic.ok()) return nullptr;
    void* storage = sk_malloc_canfail(bytes);
    return storage;
}
