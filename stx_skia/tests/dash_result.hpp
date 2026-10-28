#pragma once
// SkDashPathEffect::Make returns SkDashPathEffectResult and SkDashPath::CheckDashPath names a
// rejection in the candidate; the reference profile (STX_SKIA_REFERENCE) has Make return
// sk_sp<SkPathEffect> and SkDashPath::ValidDashPath return bool. effect_of and dash_valid give
// the same answer from either, so a test that only dashes compiles against both.

#include "include/effects/SkDashPathEffect.h"
#include "src/utils/SkDashPathPriv.h"

#include <utility>

#ifdef STX_SKIA_REFERENCE
inline sk_sp<SkPathEffect> effect_of(sk_sp<SkPathEffect> made) {
    return made;
}
inline bool dash_valid(SkScalar phase, SkSpan<const SkScalar> intervals) {
    const bool valid = SkDashPath::ValidDashPath(phase, intervals);
    return valid;
}
#else
inline sk_sp<SkPathEffect> effect_of(SkDashPathEffectResult made) {
    sk_sp<SkPathEffect> effect = std::move(made.effect);
    return effect;
}
inline bool dash_valid(SkScalar phase, SkSpan<const SkScalar> intervals) {
    const bool valid = SkDashPath::CheckDashPath(phase, intervals) == SkDashRejection::kNone;
    return valid;
}
#endif
