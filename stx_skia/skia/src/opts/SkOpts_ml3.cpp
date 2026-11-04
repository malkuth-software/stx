/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
// stx_skia: the avx2_fma tier of the raster pipeline (include/stx_skia/simd.hpp), rewritten
// for this tree's typed stage tables (CONVERSION.md, "SIMD tiers").

// Every header the kernel header includes is included here first, outside the target
// region, so their inline functions are compiled at the baseline: only the kernels in
// namespace ml3 carry the AVX2 target, and no shared inline or COMDAT copy can.
#include "include/core/SkSpan.h"
#include "include/core/SkTypes.h"
#include "include/private/SkFeatures.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkTemplates.h"
#include "modules/skcms/skcms.h"
#include "src/core/SkOpts.h"
#include "src/core/SkOptsTargets.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpContextTypes.h"
#include "src/core/SkUtils.h"
#include "src/shaders/SkPerlinNoiseShaderType.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>

// The avx2_fma tier rounds as the vector128 tier does (docs/ISA_PROFILES.md, "SIMD tiers"):
// mad and nmad round the product before the add, and the fast reciprocal is the refined one,
// so both tiers draw the same pixels. Measured 2026-11-03 on the instruction-set workload:
// fused mad moved 1,087 of 768,000 high-precision pixels (560 by a whole texel), and the
// unfused form costs nothing measurable at 256 bits.
#define STX_SKIA_ML3_VECTOR128_ROUNDING 1
#if defined(SK_CPU_X86) && !defined(SK_ENABLE_OPTIMIZE_SIZE)

#define SK_OPTS_TARGET SK_OPTS_TARGET_ML3
#include "src/opts/SkOpts_SetTarget.h"

#include "src/opts/SkRasterPipeline_opts.h"  // IWYU pragma: keep

#include "src/opts/SkOpts_RestoreTarget.h"

namespace SkOpts {
    void Init_ml3() {
        raster_pipeline_lowp_stride  = SK_OPTS_NS::raster_pipeline_lowp_stride();
        raster_pipeline_highp_stride = SK_OPTS_NS::raster_pipeline_highp_stride();

    #define M(st) ops_highp[static_cast<size_t>(SkRasterPipelineOp::st)] =                     \
            reinterpret_cast<StageFn>(                                                       \
                    SK_OPTS_NS::kHighpStages[static_cast<size_t>(SkRasterPipelineOp::st)].stage);
        SK_RASTER_PIPELINE_OPS_ALL(M)
        just_return_highp = reinterpret_cast<StageFn>(SK_OPTS_NS::just_return);
        start_pipeline_highp = SK_OPTS_NS::start_pipeline;
    #undef M

    #define M(st) ops_lowp[static_cast<size_t>(SkRasterPipelineOp::st)] =                      \
            reinterpret_cast<StageFn>(SK_OPTS_NS::lowp::kLowpStages[                         \
                    static_cast<size_t>(SkRasterPipelineOp::st)].stage);
        SK_RASTER_PIPELINE_OPS_LOWP(M)
        just_return_lowp = reinterpret_cast<StageFn>(SK_OPTS_NS::lowp::just_return);
        start_pipeline_lowp = SK_OPTS_NS::lowp::start_pipeline;
    #undef M
    }
}  // namespace SkOpts

#endif
