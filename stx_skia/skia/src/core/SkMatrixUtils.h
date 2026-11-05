/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkMatrixUtils_DEFINED
#define SkMatrixUtils_DEFINED

#include "include/core/SkPoint.h"
#include "include/core/SkSize.h"

class SkMatrix;
struct SkSamplingOptions;

/**
 *  Given a matrix, size and an antialias setting, return true if the computed dst-rect
 *  would align such that there is a 1-to-1 coorspondence between src and dst pixels.
 *  This can be called by drawing code to see if drawBitmap can be turned into
 *  drawSprite (which is faster).
 *
 *  The src-rect is defined to be { 0, 0, size.width(), size.height() }
 */
bool SkTreatAsSprite(const SkMatrix&, const SkISize& size, const SkSamplingOptions&,
                     bool isAntiAlias);

#endif
