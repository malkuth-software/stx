/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/private/SkFeatures.h"
#include "src/core/SkBlitRow.h"
#include "src/core/SkCpu.h"
#include "src/core/SkOptsTargets.h"
#include "stx_skia/simd.hpp"

#define SK_OPTS_TARGET SK_OPTS_TARGET_DEFAULT
#include "src/opts/SkOpts_SetTarget.h"

#include "src/opts/SkBlitRow_opts.h"  // IWYU pragma: keep

#include "src/opts/SkOpts_RestoreTarget.h"

namespace SkOpts {
    DEFINE_DEFAULT(blit_row_color32);
    DEFINE_DEFAULT(blit_row_s32a_opaque);

    void Init_BlitRow_ml3();
    void Init_BlitRow_lasx();

    static bool init() {
    #if defined(SK_ENABLE_OPTIMIZE_SIZE)
        // All Init_foo functions are omitted when optimizing for size
    #elif defined(STX_SKIA_SIMD_TIERS)
        // stx_skia's tiers (include/stx_skia/simd.hpp): the build's baseline, and where CMake
        // compiled the avx2_fma units (x86-64 baseline builds) those when the level is avx2_fma.
        #if defined(STX_SKIA_ML3_UNITS)
            if (stx_skia::simd::selected_level() == stx_skia::simd::Level::avx2_fma) { Init_BlitRow_ml3(); }
        #endif
    #elif defined(SK_CPU_X86)
        #if SK_CPU_X64_LEVEL < SK_CPU_X64_LEVEL_AVX2
            if (SkCpu::Supports(SkX64::ML3)) { Init_BlitRow_ml3(); }
        #endif
    #elif defined(SK_CPU_LOONGARCH)
        #if SK_CPU_LSX_LEVEL < SK_CPU_LSX_LEVEL_LASX
            if (SkCpu::Supports(SkLoongArch::ASX)) { Init_BlitRow_lasx(); }
        #endif
    #endif
      return true;
    }

    void Init_BlitRow() {
        [[maybe_unused]] static bool gInitialized = init();
    }
}  // namespace SkOpts
