/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkStringUtils.h"

#include "include/core/SkScalar.h"
#include "include/core/SkString.h"
#include "src/core/SkFloatBits.h"

void SkAppendScalar(SkString* str, SkScalar value, SkScalarAsStringType asType) {
    switch (asType) {
        case kHex_SkScalarAsStringType:
            (*str).appendf("SkBits2Float(0x%08x)", SkFloat2Bits(value));
            break;
        case kDec_SkScalarAsStringType: {
            SkString tmp{};
            tmp.printf("%.9g", value);
            if (tmp.contains('.')) {
                tmp.appendUnichar('f');
            }
            (*str).append(tmp);
            break;
        }
    }
}
