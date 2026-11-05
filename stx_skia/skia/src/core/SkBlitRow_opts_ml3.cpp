/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
// stx_skia: the avx2_fma tier of the row blitters (include/stx_skia/simd.hpp; CONVERSION.md,
// "SIMD tiers"). The kernel header's own includes come first, outside the target region, so
// only the kernels in namespace ml3 carry the AVX2 target.
#include "include/private/SkFeatures.h"
#include "src/core/SkBlitRow.h"
#include "src/core/SkColorData.h"
#include "src/core/SkMSAN.h"
#include "src/core/SkOptsTargets.h"
#include "src/core/SkVx.h"

#if defined(SK_CPU_X86) && !defined(SK_ENABLE_OPTIMIZE_SIZE)

#define SK_OPTS_TARGET SK_OPTS_TARGET_ML3
#include "src/opts/SkOpts_SetTarget.h"

#include "src/opts/SkBlitRow_opts.h"  // IWYU pragma: keep

#include "src/opts/SkOpts_RestoreTarget.h"

namespace SkOpts {
    void Init_BlitRow_ml3() {
        blit_row_color32 = ml3::blit_row_color32;
        blit_row_s32a_opaque = ml3::blit_row_s32a_opaque;
    }
}  // namespace SkOpts

#endif
