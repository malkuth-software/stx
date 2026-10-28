/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkColorSpaceXformSteps.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"
#include "include/private/SkFloatingPoint.h"
#include "modules/skcms/skcms.h"
#include "src/core/SkColorSpacePriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpList.h"

#include <cmath>
#include <cstring>

// See skia.org/docs/user/color  (== site/docs/user/color.md).

// Compute the Y vector for the HLG OOTF in the primaries of a specified color space. The value
// is specified in Rec2020 primaries in ITU-R BT.2100.
static void set_ootf_Y(const SkColorSpace* cs, float* Y) {
    const sk_sp<SkColorSpace> rec2020 = SkColorSpace::MakeRGB(
            SkNamedTransferFn::kLinear, SkNamedGamut::kRec2020);
    skcms_Matrix3x3 m{};
    (*cs).gamutTransformTo(rec2020.get(), &m);
    constexpr std::array<float, 3> Y_rec2020{0.262700f, 0.678000f, 0.059300f};
    for (int i = 0; i < 3; ++i) {
        float weight = 0.0f;
        for (int j = 0; j < 3; ++j) {
            weight += m.vals[j][i] * Y_rec2020[j];
        }
        Y[i] = weight;
    }
}

SkColorSpaceXformSteps::SkColorSpaceXformSteps(const SkColorSpace* src, SkAlphaType srcAT,
                                               const SkColorSpace* dst, SkAlphaType dstAT) {
    // Opaque outputs are treated as the same alpha type as the source input.
    // TODO: we'd really like to have a good way of explaining why we think this is useful.
    if (dstAT == kOpaque_SkAlphaType) {
        dstAT =  srcAT;
    }

    // We have some options about what to do with null src or dst here.
    // This pair seems to be the most consistent with legacy expectations.
    if (!src) { src = sk_srgb_singleton(); }
    if (!dst) { dst = src; }

    if ((*src).hash() == (*dst).hash() && srcAT == dstAT) {
        SkASSERT(SkColorSpace::Equals(src,dst));
        return;
    }

    skcms_TransferFunction srcTrfn{};
    (*src).transferFn(&srcTrfn);
    skcms_TransferFunction dstTrfn{};
    (*dst).transferFn(&dstTrfn);

    // The scale factor is the amount that values in linear space will be scaled to accommodate
    // peak luminance and HDR reference white luminance.
    float scaleFactor = 1.f;

    // TODO(https://issues.skia.org/issues/420956739): Inline the constants for PQ and HLG transfer
    // functions when the PQish and HLGish transfer functions are no longer in use.
    static constexpr skcms_TransferFunction kPQish =
        {-2.0f, -107/128.0f, 1.0f, 32/2523.0f, 2413/128.0f, -2392/128.0f, 8192/1305.0f };
    static constexpr skcms_TransferFunction kHLGish =
        {-3.0f, 2.0f, 2.0f, 1/0.17883277f, 0.28466892f, 0.55991073f, 0.0f };
    const skcms_TFType srcTfType = skcms_TransferFunction_getType(&srcTrfn);
    switch (srcTfType) {
        case skcms_TFType_PQ:
            // PQ is always scaled by a peak luminance of 10,000 nits, then divided by the HDR
            // reference white luminance (a).
            scaleFactor *= 10000.f / srcTrfn.a;
            // Use the default PQish transfer function.
            fSrcTF = kPQish;
            fFlags.linearize = true;
            break;
        case skcms_TFType_HLG:
            // HLG is scaled by the peak luminance (b), then divided by the HDR reference white
            // luminance (a).
            scaleFactor *= srcTrfn.b / srcTrfn.a;
            fFlags.linearize = true;
            // Use the HLGish transfer function scaled by 1/12.
            fSrcTF = kHLGish;
            fSrcTF.f = 1/12.f - 1.f;
            // If the system gamma is not 1.0, then compute the parameters for the OOTF.
            if (srcTrfn.c != 1.f) {
                fFlags.src_ootf = true;
                fSrcOotf[3] = srcTrfn.c - 1.f;
                set_ootf_Y(src, fSrcOotf.data());
            }
            break;
        default:
            fFlags.linearize = memcmp(&srcTrfn, &SkNamedTransferFn::kLinear, sizeof(srcTrfn)) != 0;
            if (fFlags.linearize) {
              (*src).transferFn(&fSrcTF);
            }
            break;
    }

    const skcms_TFType dstTfType = skcms_TransferFunction_getType(&dstTrfn);
    switch (dstTfType) {
        case skcms_TFType_PQ:
            // This is the inverse of the treatment of source PQ.
            scaleFactor /= 10000.f / dstTrfn.a;
            fFlags.encode = true;
            fDstTFInv = kPQish;
            skcms_TransferFunction_invert(&fDstTFInv, &fDstTFInv);
            break;
        case skcms_TFType_HLG:
            // This is the inverse of the treatment of source HLG.
            scaleFactor /= dstTrfn.b / dstTrfn.a;
            fFlags.encode = true;
            fDstTFInv = kHLGish;
            fDstTFInv.f = 1/12.f - 1.f;
            skcms_TransferFunction_invert(&fDstTFInv, &fDstTFInv);
            if (dstTrfn.c != 1.f) {
                fFlags.dst_ootf = true;
                fDstOotf[3] = 1/dstTrfn.c - 1.f;
                set_ootf_Y(dst, fDstOotf.data());
            }
            break;
        default:
            fFlags.encode = memcmp(&dstTrfn, &SkNamedTransferFn::kLinear, sizeof(dstTrfn)) != 0;
            if (fFlags.encode) {
              (*dst).invTransferFn(&fDstTFInv);
            }
            break;
    }

    fFlags.unpremul        = srcAT == kPremul_SkAlphaType;
    fFlags.gamut_transform = (*src).toXYZD50Hash() != (*dst).toXYZD50Hash() ||
                                   scaleFactor != 1.f;
    fFlags.premul          = srcAT != kOpaque_SkAlphaType && dstAT == kPremul_SkAlphaType;

    if (fFlags.gamut_transform) {
        skcms_Matrix3x3 src_to_dst{}; // Raster stages consume column-major coefficients.
        (*src).gamutTransformTo(dst, &src_to_dst);

        fSrcToDstMatrix[0] = src_to_dst.vals[0][0] * scaleFactor;
        fSrcToDstMatrix[1] = src_to_dst.vals[1][0] * scaleFactor;
        fSrcToDstMatrix[2] = src_to_dst.vals[2][0] * scaleFactor;

        fSrcToDstMatrix[3] = src_to_dst.vals[0][1] * scaleFactor;
        fSrcToDstMatrix[4] = src_to_dst.vals[1][1] * scaleFactor;
        fSrcToDstMatrix[5] = src_to_dst.vals[2][1] * scaleFactor;

        fSrcToDstMatrix[6] = src_to_dst.vals[0][2] * scaleFactor;
        fSrcToDstMatrix[7] = src_to_dst.vals[1][2] * scaleFactor;
        fSrcToDstMatrix[8] = src_to_dst.vals[2][2] * scaleFactor;
    } else {
    #ifdef SK_DEBUG
        skcms_Matrix3x3 srcM{};
        skcms_Matrix3x3 dstM{};
        (*src).toXYZD50(&srcM);
        (*dst).toXYZD50(&dstM);
        SkASSERT(0 == memcmp(&srcM, &dstM, 9*sizeof(float)) && "Hash collision");
    #endif
    }

    // If the source and destination OOTFs cancel each other out, skip both.
    if ( fFlags.src_ootf        &&
        !fFlags.gamut_transform &&
         fFlags.dst_ootf) {
        // If there is no gamut transform, then the r,g,b coefficients for the
        // OOTFs must be the same.
        SkASSERT(0 == memcmp(fSrcOotf.data(), fDstOotf.data(), 3*sizeof(float)));
        // If the gammas cancel out, then remove the steps.
        if ((fSrcOotf[3] + 1.f) * (fDstOotf[3] + 1.f) == 1.f) {
            fFlags.src_ootf = false;
            fFlags.dst_ootf = false;
        }
    }

    // If we linearize then immediately reencode with the same transfer function, skip both.
    if ( fFlags.linearize       &&
        !fFlags.src_ootf        &&
        !fFlags.gamut_transform &&
        !fFlags.dst_ootf        &&
         fFlags.encode          &&
         (*src).transferFnHash() == (*dst).transferFnHash())
    {
    #ifdef SK_DEBUG
        // PQ and HLG types use PQish and HLGish for fSrcTF, so this check is not valid for them.
        if (srcTfType != skcms_TFType_PQ && srcTfType != skcms_TFType_HLG) {
            skcms_TransferFunction dstTF{};
            (*dst).transferFn(&dstTF);
            const std::array<float, 7> source{fSrcTF.g, fSrcTF.a, fSrcTF.b, fSrcTF.c,
                                             fSrcTF.d, fSrcTF.e, fSrcTF.f};
            const std::array<float, 7> destination{dstTF.g, dstTF.a, dstTF.b, dstTF.c,
                                                  dstTF.d, dstTF.e, dstTF.f};
            SkASSERT(source == destination && "Hash collision");
        }
    #endif
        fFlags.linearize  = false;
        fFlags.encode     = false;
    }

    // Skip unpremul...premul if there are no non-linear operations between.
    if ( fFlags.unpremul   &&
        !fFlags.linearize  &&
        !fFlags.encode     &&
         fFlags.premul)
    {
        fFlags.unpremul = false;
        fFlags.premul   = false;
    }
}

void SkColorSpaceXformSteps::apply(float* rgba) const {
    if (fFlags.unpremul) {
        float invA = sk_ieee_float_divide(1.0f, rgba[3]);
        // Preserve the established IEEE test, including zero-alpha and nonfinite
        // inputs. This kernel is compiled without fast-math reassociation.
        const bool finite = invA * 0.0f == 0.0f;
        invA = finite ? invA : 0.0f;
        rgba[0] *= invA;
        rgba[1] *= invA;
        rgba[2] *= invA;
    }
    if (fFlags.linearize) {
        rgba[0] = skcms_TransferFunction_eval(&fSrcTF, rgba[0]);
        rgba[1] = skcms_TransferFunction_eval(&fSrcTF, rgba[1]);
        rgba[2] = skcms_TransferFunction_eval(&fSrcTF, rgba[2]);
    }
    if (fFlags.src_ootf) {
        const float Y = fSrcOotf[0] * rgba[0] +
                        fSrcOotf[1] * rgba[1] +
                        fSrcOotf[2] * rgba[2];
        const float Y_to_gamma_minus_1 = std::pow(Y, fSrcOotf[3]);
        rgba[0] *= Y_to_gamma_minus_1;
        rgba[1] *= Y_to_gamma_minus_1;
        rgba[2] *= Y_to_gamma_minus_1;
    }
    if (fFlags.gamut_transform) {
        const std::array<float, 3> temp{rgba[0], rgba[1], rgba[2]};
        for (int i = 0; i < 3; ++i) {
            rgba[i] = fSrcToDstMatrix[    i] * temp[0] +
                      fSrcToDstMatrix[3 + i] * temp[1] +
                      fSrcToDstMatrix[6 + i] * temp[2];
        }
    }
    if (fFlags.dst_ootf) {
        const float Y = fDstOotf[0] * rgba[0] +
                        fDstOotf[1] * rgba[1] +
                        fDstOotf[2] * rgba[2];
        const float Y_to_gamma_minus_1 = std::pow(Y, fDstOotf[3]);
        rgba[0] *= Y_to_gamma_minus_1;
        rgba[1] *= Y_to_gamma_minus_1;
        rgba[2] *= Y_to_gamma_minus_1;
    }
    if (fFlags.encode) {
        rgba[0] = skcms_TransferFunction_eval(&fDstTFInv, rgba[0]);
        rgba[1] = skcms_TransferFunction_eval(&fDstTFInv, rgba[1]);
        rgba[2] = skcms_TransferFunction_eval(&fDstTFInv, rgba[2]);
    }
    if (fFlags.premul) {
        rgba[0] *= rgba[3];
        rgba[1] *= rgba[3];
        rgba[2] *= rgba[3];
    }
}

void SkColorSpaceXformSteps::apply(SkRasterPipeline* p) const & {
    SkRasterPipeline& pipeline = *p;
    if (fFlags.unpremul)        { pipeline.append(SkRasterPipelineOp::unpremul); }
    if (fFlags.linearize)       { pipeline.appendTransferFunction(fSrcTF); }
    if (fFlags.src_ootf)        { pipeline.append(SkRasterPipelineOp::ootf, fSrcOotf.data()); }
    if (fFlags.gamut_transform) { pipeline.append(SkRasterPipelineOp::matrix_3x3, fSrcToDstMatrix.data()); }
    if (fFlags.dst_ootf)        { pipeline.append(SkRasterPipelineOp::ootf, fDstOotf.data()); }
    if (fFlags.encode)          { pipeline.appendTransferFunction(fDstTFInv); }
    if (fFlags.premul)          { pipeline.append(SkRasterPipelineOp::premul); }
}
