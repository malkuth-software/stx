/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/effects/colorfilters/SkTableColorFilter.h"

#include "include/core/SkColorFilter.h"
#include "include/core/SkColorTable.h"
#include "include/core/SkFlattenable.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpContexts.h"
#include "src/core/SkRasterPipelineOpList.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"

#include <cstdint>
#include <utility>

bool SkTableColorFilter::appendStages(const SkStageRec& rec, bool shaderIsOpaque) const {
    SkRasterPipeline& pipeline = *rec.fPipeline;
    if (!shaderIsOpaque) {
        pipeline.append(SkRasterPipelineOp::unpremul);
    }

    // The pipeline borrows the table's immutable rows. Its caller retains this
    // filter until execution completes; the arena owns only the context record.
    SkRasterPipelineContexts::TablesCtx* tables =
            (*rec.fAlloc).make<SkRasterPipelineContexts::TablesCtx>();
    (*tables).a = (*fTable).alphaTable();
    (*tables).r = (*fTable).redTable();
    (*tables).g = (*fTable).greenTable();
    (*tables).b = (*fTable).blueTable();
    pipeline.append(SkRasterPipelineOp::byte_tables, tables);

    const bool definitelyOpaque = shaderIsOpaque && (*tables).a[0xff] == 0xff;
    if (!definitelyOpaque) {
        pipeline.append(SkRasterPipelineOp::premul);
    }
    return true;
}

void SkTableColorFilter::flatten(SkWriteBuffer& buffer) const {
    (*fTable).flatten(buffer);
}

sk_sp<SkFlattenable> SkTableColorFilter::CreateProc(SkReadBuffer& buffer) {
    sk_sp<SkColorTable> table = SkColorTable::Deserialize(buffer);
    sk_sp<SkFlattenable> filter = SkColorFilters::Table(std::move(table));
    return filter;
}

///////////////////////////////////////////////////////////////////////////////

sk_sp<SkColorFilter> SkColorFilters::Table(const uint8_t table[256]) {
    sk_sp<SkColorTable> lookup = SkColorTable::Make(table);
    sk_sp<SkColorFilter> filter = SkColorFilters::Table(std::move(lookup));
    return filter;
}

sk_sp<SkColorFilter> SkColorFilters::TableARGB(const uint8_t tableA[256],
                                               const uint8_t tableR[256],
                                               const uint8_t tableG[256],
                                               const uint8_t tableB[256]) {
    sk_sp<SkColorTable> lookup = SkColorTable::Make(tableA, tableR, tableG, tableB);
    sk_sp<SkColorFilter> filter = SkColorFilters::Table(std::move(lookup));
    return filter;
}

sk_sp<SkColorFilter> SkColorFilters::Table(sk_sp<SkColorTable> table) {
    if (!table) {
        return nullptr;
    }
    sk_sp<SkColorFilter> filter = sk_make_sp<SkTableColorFilter>(std::move(table));
    return filter;
}
