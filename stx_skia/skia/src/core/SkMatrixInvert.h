/*
 * Copyright 2021 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkMatrixInvert_DEFINED
#define SkMatrixInvert_DEFINED

#include "include/core/SkScalar.h"

/**
 * Computes the inverse of `inMatrix`, passed in column-major order, into `outMatrix`. The two
 * may be the same array.
 * Returns true if the matrix is invertible: its determinant, computed in double precision, is
 * nonzero and every entry of the inverse is finite in float. Otherwise returns false and leaves
 * `outMatrix` holding unspecified values.
 */
[[nodiscard]] bool SkInvert4x4Matrix(const SkScalar (&inMatrix)[16], SkScalar (&outMatrix)[16]);

#endif
