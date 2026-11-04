/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
// stx_skia: the avx2_fma tier of the swizzlers (include/stx_skia/simd.hpp; CONVERSION.md,
// "SIMD tiers"). The kernel file's own includes come first, outside the target region, so
// only the kernels in namespace ml3 carry the AVX2 target.
#include "include/private/SkFeatures.h"
#include "src/core/SkColorData.h"
#include "src/core/SkOptsTargets.h"
#include "src/core/SkSwizzlePriv.h"
#include "src/core/SkUtils.h"
#include "src/core/SkVx.h"

#include <algorithm>
#include <cmath>
#include <utility>

#if defined(SK_CPU_X86) && !defined(SK_ENABLE_OPTIMIZE_SIZE)

#define SK_OPTS_TARGET SK_OPTS_TARGET_ML3
#include "src/opts/SkOpts_SetTarget.h"

#include "src/opts/SkSwizzler_opts.inc"  // IWYU pragma: keep

#include "src/opts/SkOpts_RestoreTarget.h"

namespace SkOpts {
    void Init_Swizzler_ml3() {
        RGBA_to_BGRA = ml3::RGBA_to_BGRA;
        RGBA_to_rgbA = ml3::RGBA_to_rgbA;
        RGBA_to_bgrA = ml3::RGBA_to_bgrA;
        rgbA_to_RGBA = ml3::rgbA_to_RGBA;
        rgbA_to_BGRA = ml3::rgbA_to_BGRA;
        RGB_to_RGB1 = ml3::RGB_to_RGB1;
        RGB_to_BGR1 = ml3::RGB_to_BGR1;
        gray_to_RGB1 = ml3::gray_to_RGB1;
        grayA_to_RGBA = ml3::grayA_to_RGBA;
        grayA_to_rgbA = ml3::grayA_to_rgbA;
        inverted_CMYK_to_RGB1 = ml3::inverted_CMYK_to_RGB1;
        inverted_CMYK_to_BGR1 = ml3::inverted_CMYK_to_BGR1;
    }
}  // namespace SkOpts

#endif
