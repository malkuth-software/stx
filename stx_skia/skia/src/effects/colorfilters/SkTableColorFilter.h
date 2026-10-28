/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef SkTableColorFilter_DEFINED
#define SkTableColorFilter_DEFINED

#include "include/core/SkColorTable.h"
#include "include/core/SkFlattenable.h"
#include "include/core/SkRefCnt.h"
#include "include/private/SkAssert.h"
#include "include/private/SkDebug.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"

#include <utility>

class SkBitmap;
class SkReadBuffer;
class SkWriteBuffer;
struct SkStageRec;

class SkTableColorFilter final : public SkColorFilterBase {
public:
    explicit SkTableColorFilter(sk_sp<SkColorTable> table) : fTable(std::move(table)) {
        SkASSERT(fTable);
    }

    SkColorFilterBase::Type type() const override { return SkColorFilterBase::Type::kTable; }

    bool appendStages(const SkStageRec& rec, bool shaderIsOpaque) const override;

    void flatten(SkWriteBuffer& buffer) const override;

    const SkBitmap& bitmap() const & {
        const SkBitmap& image = (*fTable).bitmap();
        return image;
    }
    const SkBitmap& bitmap() const && = delete;

private:
    SK_FLATTENABLE_HOOKS(SkTableColorFilter)

    sk_sp<SkColorTable> fTable;
};

#endif
