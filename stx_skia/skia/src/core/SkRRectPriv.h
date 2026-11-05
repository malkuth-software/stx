/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRRectPriv_DEFINED
#define SkRRectPriv_DEFINED

#include "include/core/SkRRect.h"

class SkRBuffer;
class SkWBuffer;

// Path serialization of rounded rectangles. The serialized form is the rectangle and the
// four corner radii, twelve floats in that order; the type tag is derived on read.
class SkRRectPriv {
public:
    // Reads and validates one rounded rectangle. On failure the buffer may have advanced
    // and *rr is unchanged.
    static bool ReadFromBuffer(SkRBuffer* buffer, SkRRect* rr);

    static void WriteToBuffer(const SkRRect& rr, SkWBuffer* buffer);
};

#endif
