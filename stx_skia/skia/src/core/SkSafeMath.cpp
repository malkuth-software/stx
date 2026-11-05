/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkSafeMath.h"

size_t SkSafeMath::Add(size_t x, size_t y) {
    SkSafeMath tmp;
    const size_t sum = tmp.add(x, y);
    if (!tmp.ok()) return SIZE_MAX;
    return sum;
}

size_t SkSafeMath::Mul(size_t x, size_t y) {
    SkSafeMath tmp;
    const size_t prod = tmp.mul(x, y);
    if (!tmp.ok()) return SIZE_MAX;
    return prod;
}
