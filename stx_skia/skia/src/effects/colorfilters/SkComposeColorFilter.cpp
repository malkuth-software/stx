/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/effects/colorfilters/SkComposeColorFilter.h"

#include "include/core/SkRefCnt.h"
#include "include/private/SkAssert.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"

#include <utility>
struct SkStageRec;

SkComposeColorFilter::SkComposeColorFilter(sk_sp<SkColorFilter> outer, sk_sp<SkColorFilter> inner)
        : fOuter(as_CFB_sp(std::move(outer))), fInner(as_CFB_sp(std::move(inner))) {
    SkASSERT(fOuter && fInner);
}

bool SkComposeColorFilter::onIsAlphaUnchanged() const {
    const bool unchanged = (*fOuter).isAlphaUnchanged() && (*fInner).isAlphaUnchanged();
    return unchanged;
}

bool SkComposeColorFilter::appendStages(const SkStageRec& rec, bool shaderIsOpaque) const {
    const SkColorFilterBase& inner = *fInner;
    const SkColorFilterBase& outer = *fOuter;
    const bool innerIsOpaque = shaderIsOpaque && inner.isAlphaUnchanged();
    if (!inner.appendStages(rec, shaderIsOpaque)) {
        return false;
    }
    const bool appended = outer.appendStages(rec, innerIsOpaque);
    return appended;
}

void SkComposeColorFilter::flatten(SkWriteBuffer& buffer) const {
    buffer.writeFlattenable(fOuter.get());
    buffer.writeFlattenable(fInner.get());
}

sk_sp<SkFlattenable> SkComposeColorFilter::CreateProc(SkReadBuffer& buffer) {
    sk_sp<SkColorFilter> outer(buffer.readColorFilter());
    sk_sp<SkColorFilter> inner(buffer.readColorFilter());
    if (!outer) {
        return inner;
    }
    sk_sp<SkFlattenable> composed = (*outer).makeComposed(std::move(inner));
    return composed;
}

sk_sp<SkColorFilter> SkColorFilter::makeComposed(sk_sp<SkColorFilter> inner) const {
    if (!inner) {
        sk_sp<SkColorFilter> retained = sk_ref_sp(this);
        return retained;
    }

    sk_sp<SkColorFilter> outer = sk_ref_sp(this);
    sk_sp<SkColorFilter> composed(new SkComposeColorFilter(std::move(outer), std::move(inner)));
    return composed;
}
