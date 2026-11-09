/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "include/core/SkMaskFilter.h"

#include "include/core/SkFlattenable.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"
#include "src/core/SkMaskFilterBase.h"
struct SkDeserialProcs;


sk_sp<SkMaskFilter> SkMaskFilter::Deserialize(const void* data, size_t size,
                                              const SkDeserialProcs* procs) {
    sk_sp<SkFlattenable> decoded = SkFlattenable::Deserialize(kSkMaskFilter_Type, data, size, procs);
    SkMaskFilter* filter = static_cast<SkMaskFilter*>(decoded.release());
    sk_sp<SkMaskFilter> result(filter);
    return result;
}
