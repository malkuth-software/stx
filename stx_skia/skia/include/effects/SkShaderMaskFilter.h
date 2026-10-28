/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkShaderMaskFilter_DEFINED
#define SkShaderMaskFilter_DEFINED

#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"

class SkMaskFilter;
class SkShader;

// Retained CPU coverage-shader factory. The filter shares the supplied shader;
// a null shader produces no filter.
class SK_API SkShaderMaskFilter {
public:
    static sk_sp<SkMaskFilter> Make(sk_sp<SkShader> shader);

private:
    friend class SkFlattenable;
};

#endif
