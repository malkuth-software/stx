/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkColorFilterBase_DEFINED
#define SkColorFilterBase_DEFINED

#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkFlattenable.h"
#include "include/core/SkRefCnt.h"
#include "src/core/SkColorData.h"

#include <cstddef>

class SkColorSpace;
enum class SkBlendMode;
struct SkDeserialProcs;
struct SkStageRec;

#define SK_ALL_COLOR_FILTERS(M) \
    M(BlendMode)                \
    M(ColorSpaceXform)          \
    M(Compose)                  \
    M(Gaussian)                 \
    M(Matrix)                   \
    M(Runtime)                  \
    M(Table)                    \
    M(WorkingFormat)

class SkColorFilterBase : public SkColorFilter {
public:
    // The caller retains this filter and the arena through pipeline execution.
    // Failure may leave partial stages; the caller must discard that pipeline.
    [[nodiscard]] virtual bool appendStages(const SkStageRec& rec, bool shaderIsOpaque) const = 0;

    // Override only when every input preserves alpha.
    virtual bool onIsAlphaUnchanged() const { return false; }

    enum class Type {
        // Used for stubs/tests
        kNoop,
#define M(type) k##type,
        SK_ALL_COLOR_FILTERS(M)
#undef M

    };

    virtual Type type() const = 0;

    bool affectsTransparentBlack() const {
        const SkColor4f filtered = filterColor4f(SkColors::kTransparent, nullptr, nullptr);
        const bool affected = filtered != SkColors::kTransparent;
        return affected;
    }


    static SkFlattenable::Type GetFlattenableType() {
        return kSkColorFilter_Type;
    }

    SkFlattenable::Type getFlattenableType() const override {
        return kSkColorFilter_Type;
    }

    static sk_sp<SkColorFilter> Deserialize(const void* data, size_t size,
                                          const SkDeserialProcs* procs = nullptr) {
        sk_sp<SkColorFilter> filter = SkColorFilter::Deserialize(data, size, procs);
        return filter;
    }

    virtual SkPMColor4f onFilterColor4f(const SkPMColor4f& color, SkColorSpace* dstCS) const;

protected:
    SkColorFilterBase() {}

    virtual bool onAsAColorMatrix(float[20]) const;
    virtual bool onAsAColorMode(SkColor* color, SkBlendMode* bmode) const;

private:
    friend class SkColorFilter;

    using INHERITED = SkFlattenable;
};

static inline SkColorFilterBase* as_CFB(SkColorFilter* filter) {
    SkColorFilterBase* base = static_cast<SkColorFilterBase*>(filter);
    return base;
}

static inline const SkColorFilterBase* as_CFB(const SkColorFilter* filter) {
    const SkColorFilterBase* base = static_cast<const SkColorFilterBase*>(filter);
    return base;
}

// Borrowed base views remain valid only while the filter has a live owner.
static inline const SkColorFilterBase* as_CFB(const sk_sp<SkColorFilter>& filter) {
    const SkColorFilterBase* base = static_cast<const SkColorFilterBase*>(filter.get());
    return base;
}

static inline const SkColorFilterBase* as_CFB(const sk_sp<SkColorFilter>&&) = delete;

static inline sk_sp<SkColorFilterBase> as_CFB_sp(sk_sp<SkColorFilter> filter) {
    sk_sp<SkColorFilterBase> base(static_cast<SkColorFilterBase*>(filter.release()));
    return base;
}


#endif
