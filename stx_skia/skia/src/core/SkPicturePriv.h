/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkPicturePriv_DEFINED
#define SkPicturePriv_DEFINED

class SkPicturePriv {
public:
    static constexpr int kDefaultRecursionLimit = 100;
    enum Version {
        kPictureShaderFilterParam_Version   = 82,
        kMatrixImageFilterSampling_Version  = 83,
        kImageFilterImageSampling_Version   = 84,
        kNoFilterQualityShaders_Version     = 85,
        kVerticesRemoveCustomData_Version   = 86,
        kSkBlenderInSkPaint                 = 87,
        kBlenderInEffects                   = 88,
        kNoExpandingClipOps                 = 89,
        kBackdropScaleFactor                = 90,
        kRawImageShaders                    = 91,
        kAnisotropicFilter                  = 92,
        kBlend4fColorFilter                 = 93,
        kNoShaderLocalMatrix                = 94,
        kShaderImageFilterSerializeShader   = 95,
        kRevampMagnifierFilter              = 96,
        kRuntimeImageFilterSampleRadius     = 97,
        kCombineBlendArithmeticFilters      = 98,
        kRemoveLegacyMagnifierFilter        = 99,
        kDropShadowImageFilterComposition   = 100,
        kCropImageFilterSupportsTiling      = 101,
        kConvolutionImageFilterTilingUpdate = 102,
        kRemoveDeprecatedCropRect           = 103,
        kMultipleFiltersOnSaveLayer         = 104,
        kUnclampedMatrixColorFilter         = 105,
        kSaveLayerBackdropTileMode          = 106,
        kCombineColorShaders                = 107,
        kSerializeStableKeys                = 108,
        kWorkingColorSpaceOutput            = 109,

        // Only SKPs within the min/current picture version range (inclusive) can be read.
        //
        // When updating kMin_Version also update oldestSupportedSkpVersion in
        // infra/bots/gen_tasks_logic/gen_tasks_logic.go
        //
        // Steps on how to find which oldestSupportedSkpVersion to use:
        // 1) Find the git hash when the desired kMin_Version was the kCurrent_Version from the
        //    git logs: https://skia.googlesource.com/skia/+log/main/src/core/SkPicturePriv.h
        //    Eg: https://skia.googlesource.com/skia/+/bfd330d081952424a93d51715653e4d1314d4822%5E%21/#F1
        //
        // 2) Use that git hash to find the SKP asset version number at that time here:
        //    https://skia.googlesource.com/skia/+/bfd330d081952424a93d51715653e4d1314d4822/infra/bots/assets/skp/VERSION
        //
        // 3) [Optional] Increment the SKP asset version number from step 3 and verify that it has
        //    the expected version number by downloading the asset and running skpinfo on it.
        //
        // 4) Use the incremented SKP asset version number as the oldestSupportedSkpVersion in
        //    infra/bots/gen_tasks_logic/gen_tasks_logic.go
        //
        // 5) Run `make -C infra/bots train`
        //
        // Contact the Infra Gardener if the above steps do not work for you.
        kMin_Version     = kPictureShaderFilterParam_Version,
        kCurrent_Version = kWorkingColorSpaceOutput
    };
};

#endif
