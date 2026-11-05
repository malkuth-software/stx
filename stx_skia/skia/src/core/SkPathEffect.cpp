/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "include/core/SkPathEffect.h"

#include "include/core/SkFlattenable.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRefCnt.h"
#include "src/core/SkPathEffectBase.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"

#include <cstddef>
#include <utility>

class SkStrokeRec;
struct SkDeserialProcs;
struct SkRect;

///////////////////////////////////////////////////////////////////////////////

bool SkPathEffect::filterPath(SkPathBuilder* dst, const SkPath& src, SkStrokeRec* rec) const {
    const bool filtered = filterPath(dst, src, rec, nullptr, SkMatrix::I());
    return filtered;
}

bool SkPathEffect::filterPath(SkPathBuilder* dst, const SkPath& src, SkStrokeRec* rec,
                              const SkRect* bounds, const SkMatrix& ctm) const {
    const bool filtered = (*as_PEB(this)).onFilterPath(dst, src, rec, bounds, ctm);
    return filtered;
}

bool SkPathEffectBase::asPoints(PointData* results, const SkPath& src,
                    const SkStrokeRec& rec, const SkMatrix& mx, const SkRect* rect) const {
    const bool points = onAsPoints(results, src, rec, mx, rect);
    return points;
}

bool SkPathEffect::needsCTM() const {
    const bool needs = (*as_PEB(this)).onNeedsCTM();
    return needs;
}

///////////////////////////////////////////////////////////////////////////////

/** \class SkPairPathEffect

 Common baseclass for Compose and Sum. This subclass manages two pathEffects,
 including flattening them. It does nothing in filterPath, and is only useful
 for managing the lifetimes of its two arguments.
 */
class SkPairPathEffect : public SkPathEffectBase {
protected:
    SkPairPathEffect(sk_sp<SkPathEffect> pe0, sk_sp<SkPathEffect> pe1)
        : fPE0(std::move(pe0)), fPE1(std::move(pe1))
    {
        SkASSERT(fPE0.get());
        SkASSERT(fPE1.get());
    }

    void flatten(SkWriteBuffer& buffer) const override {
        buffer.writeFlattenable(fPE0.get());
        buffer.writeFlattenable(fPE1.get());
    }

    // these are visible to our subclasses
    sk_sp<SkPathEffect> fPE0;
    sk_sp<SkPathEffect> fPE1;

private:
    using INHERITED = SkPathEffectBase;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

class SkComposePathEffect : public SkPairPathEffect {
public:
    /** Construct a pathEffect whose effect is to apply first the inner pathEffect
     and the the outer pathEffect (e.g. outer(inner(path)))
     The reference counts for outer and inner are both incremented in the constructor,
     and decremented in the destructor.
     */
    static sk_sp<SkPathEffect> Make(sk_sp<SkPathEffect> outer, sk_sp<SkPathEffect> inner) {
        if (!outer) {
            return inner;
        }
        if (!inner) {
            return outer;
        }
        const sk_sp<SkPathEffect> composed(
                new SkComposePathEffect(std::move(outer), std::move(inner)));
        return composed;
    }

    SkComposePathEffect(sk_sp<SkPathEffect> outer, sk_sp<SkPathEffect> inner)
            : INHERITED(std::move(outer), std::move(inner)) {}

    SkComposePathEffect(const SkComposePathEffect&) = delete;
    SkComposePathEffect& operator=(const SkComposePathEffect&) = delete;

    bool onFilterPath(SkPathBuilder* builder, const SkPath& src, SkStrokeRec* rec,
                       const SkRect* cullRect, const SkMatrix& ctm) const override {
        // The outer effect reads the inner result from tmp after the builder is detached.
        SkPath          tmp;
        const SkPath*   ptr = &src;

        if ((*fPE1).filterPath(builder, src, rec, cullRect, ctm)) {
            tmp = (*builder).detach();
            ptr = &tmp;
        }
        const bool filtered = (*fPE0).filterPath(builder, *ptr, rec, cullRect, ctm);
        return filtered;
    }

    SK_FLATTENABLE_HOOKS(SkComposePathEffect)

    bool computeFastBounds(SkRect* bounds) const override {
        // inner (fPE1) is computed first, automatically updating bounds before computing outer.
        if (!(*as_PEB(fPE1)).computeFastBounds(bounds)) {
            return false;
        }
        const bool computed = (*as_PEB(fPE0)).computeFastBounds(bounds);
        return computed;
    }

private:
    friend class SkPathEffect;

    using INHERITED = SkPairPathEffect;
};

sk_sp<SkFlattenable> SkComposePathEffect::CreateProc(SkReadBuffer& buffer) {
    sk_sp<SkPathEffect> pe0(buffer.readPathEffect());
    sk_sp<SkPathEffect> pe1(buffer.readPathEffect());
    const sk_sp<SkFlattenable> effect =
            SkComposePathEffect::Make(std::move(pe0), std::move(pe1));
    return effect;
}

///////////////////////////////////////////////////////////////////////////////

/** \class SkSumPathEffect

 This subclass of SkPathEffect applies two pathEffects, one after the other.
 Its filterPath() returns true if either of the effects succeeded.
 */
class SkSumPathEffect : public SkPairPathEffect {
public:
    /** Construct a pathEffect whose effect is to apply two effects, in sequence.
     (e.g. first(path) + second(path))
     The reference counts for first and second are both incremented in the constructor,
     and decremented in the destructor.
     */
    static sk_sp<SkPathEffect> Make(sk_sp<SkPathEffect> first, sk_sp<SkPathEffect> second) {
        if (!first) {
            return second;
        }
        if (!second) {
            return first;
        }
        const sk_sp<SkPathEffect> summed(new SkSumPathEffect(std::move(first), std::move(second)));
        return summed;
    }

    SkSumPathEffect(sk_sp<SkPathEffect> first, sk_sp<SkPathEffect> second)
            : INHERITED(std::move(first), std::move(second)) {}

    SkSumPathEffect(const SkSumPathEffect&) = delete;
    SkSumPathEffect& operator=(const SkSumPathEffect&) = delete;

    bool onFilterPath(SkPathBuilder* builder, const SkPath& src, SkStrokeRec* rec,
                      const SkRect* cullRect, const SkMatrix& ctm) const override {
        // always call both, even if the first one succeeds
        const bool filteredFirst = (*fPE0).filterPath(builder, src, rec, cullRect, ctm);
        const bool filteredSecond = (*fPE1).filterPath(builder, src, rec, cullRect, ctm);
        const bool filtered = filteredFirst || filteredSecond;
        return filtered;
    }

    SK_FLATTENABLE_HOOKS(SkSumPathEffect)

    bool computeFastBounds(SkRect* bounds) const override {
        // Unlike Compose(), PE0 modifies the path first for Sum
        if (!(*as_PEB(fPE0)).computeFastBounds(bounds)) {
            return false;
        }
        const bool computed = (*as_PEB(fPE1)).computeFastBounds(bounds);
        return computed;
    }

private:
    friend class SkPathEffect;

    using INHERITED = SkPairPathEffect;
};

sk_sp<SkFlattenable> SkSumPathEffect::CreateProc(SkReadBuffer& buffer) {
    sk_sp<SkPathEffect> pe0(buffer.readPathEffect());
    sk_sp<SkPathEffect> pe1(buffer.readPathEffect());
    const sk_sp<SkFlattenable> effect = SkSumPathEffect::Make(std::move(pe0), std::move(pe1));
    return effect;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

sk_sp<SkPathEffect> SkPathEffect::MakeSum(sk_sp<SkPathEffect> first, sk_sp<SkPathEffect> second) {
    const sk_sp<SkPathEffect> effect = SkSumPathEffect::Make(std::move(first), std::move(second));
    return effect;
}

sk_sp<SkPathEffect> SkPathEffect::MakeCompose(sk_sp<SkPathEffect> outer,
                                              sk_sp<SkPathEffect> inner) {
    const sk_sp<SkPathEffect> effect =
            SkComposePathEffect::Make(std::move(outer), std::move(inner));
    return effect;
}


sk_sp<SkPathEffect> SkPathEffect::Deserialize(const void* data, size_t size,
                                              const SkDeserialProcs* procs) {
    const sk_sp<SkPathEffect> effect(static_cast<SkPathEffect*>(
            SkFlattenable::Deserialize(kSkPathEffect_Type, data, size, procs).release()));
    return effect;
}
