
/*
 * Copyright 2008 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef SkUnPreMultiply_DEFINED
#define SkUnPreMultiply_DEFINED

#include "include/core/SkColor.h"
#include "include/core/SkTypes.h"
#include "include/private/SkCPUTypes.h"

#include <cstdint>

class SK_API SkUnPreMultiply {
public:
    typedef uint32_t Scale;

    // index this table with alpha [0..255]
    static const Scale* GetScaleTable() {
        return gTable;
    }

    // Stops in Release for an alpha past the 256-entry table.
    static Scale GetScale(U8CPU alpha) {
        SkASSERT_RELEASE(alpha <= 255);
        const Scale scale = gTable[alpha];
        return scale;
    }

    /** Usage:

        const Scale* table = SkUnPreMultiply::GetScaleTable();

        for (...) {
            unsigned a = ...
            SkUnPreMultiply::Scale scale = table[a];

            red = SkUnPreMultiply::ApplyScale(scale, red);
            ...
            // now red is unpremultiplied
        }
    */
    static U8CPU ApplyScale(Scale scale, U8CPU component) {
        SkASSERT(component <= 255);
        return (scale * component + (1 << 23)) >> 24;
    }

    static SkColor PMColorToColor(SkPMColor c);

private:
    static const uint32_t gTable[256];
};

#endif
