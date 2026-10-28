#pragma once
// SkMaskFilter::MakeBlur returns SkBlurMaskFilterResult in the candidate and
// sk_sp<SkMaskFilter> in the reference profile (STX_SKIA_REFERENCE); filter_of gives the filter
// either way, so a test that only draws with the blur compiles against both.

#include "include/core/SkMaskFilter.h"

#include <utility>

#ifdef STX_SKIA_REFERENCE
inline sk_sp<SkMaskFilter> filter_of(sk_sp<SkMaskFilter> made) {
    return made;
}
#else
inline sk_sp<SkMaskFilter> filter_of(SkBlurMaskFilterResult made) {
    sk_sp<SkMaskFilter> filter = std::move(made.filter);
    return filter;
}
#endif
