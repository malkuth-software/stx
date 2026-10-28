/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef SkStrokerPriv_DEFINED
#define SkStrokerPriv_DEFINED

#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkScalar.h"

class SkPathBuilder;

class SkStrokerPriv {
public:
    using CapProc = void (*)(SkPathBuilder* path,
                             const SkPoint& pivot,
                             const SkVector& normal,
                             const SkPoint& stop,
                             bool extendLastPt);

    // outer and inner must be distinct builders.
    using JoinProc = void (*)(SkPathBuilder* outer, SkPathBuilder* inner,
                              const SkVector& beforeUnitNormal,
                              const SkPoint& pivot,
                              const SkVector& afterUnitNormal,
                              SkScalar radius, SkScalar invMiterLimit,
                              bool prevIsLine, bool currIsLine);

    static CapProc  CapFactory(SkPaint::Cap);
    static JoinProc JoinFactory(SkPaint::Join);
};

#endif
