/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkColorSpaceXformSteps_DEFINED
#define SkColorSpaceXformSteps_DEFINED

#include "modules/skcms/skcms.h"

#include <array>
#include <cstdint>

class SkColorSpace;
class SkRasterPipeline;
enum SkAlphaType : int;

struct SkColorSpaceXformSteps {

    struct Flags {
        bool unpremul         = false;
        bool linearize        = false;
        bool src_ootf         = false;
        bool gamut_transform  = false;
        bool dst_ootf         = false;
        bool encode           = false;
        bool premul           = false;

        constexpr uint32_t mask() const {
            const uint32_t bits = (unpremul        ?  1 : 0)
                 | (linearize       ?  2 : 0)
                 | (src_ootf        ? 32 : 0)
                 | (gamut_transform ?  4 : 0)
                 | (dst_ootf        ? 64 : 0)
                 | (encode          ?  8 : 0)
                 | (premul          ? 16 : 0);
            return bits;
        }
    };

    SkColorSpaceXformSteps() {}
    SkColorSpaceXformSteps(const SkColorSpace* src, SkAlphaType srcAT,
                           const SkColorSpace* dst, SkAlphaType dstAT);

    template <typename S, typename D>
    SkColorSpaceXformSteps(const S& src, const D& dst)
        : SkColorSpaceXformSteps(src.colorSpace(), src.alphaType(),
                                 dst.colorSpace(), dst.alphaType()) {}

    // Transform exactly four RGBA floats in place, disjoint from this plan's
    // coefficients. All computation remains binary32, with the established order.
    void apply(float rgba[4]) const;
    // The pipeline borrows coefficients; keep this plan alive and unchanged
    // through execution. Plan construction does not retain source color spaces.
    void apply(SkRasterPipeline*) const &;
    void apply(SkRasterPipeline*) const && = delete;

    explicit operator bool() const {
        const bool active = fFlags.mask() != 0;
        return active;
    }

    Flags fFlags{};
    skcms_TransferFunction fSrcTF{};    // Active only for linearize.
    skcms_TransferFunction fDstTFInv{}; // Active only for encode.
    std::array<float, 9> fSrcToDstMatrix{}; // 3x3 column-major gamut transform.
    std::array<float, 4> fSrcOotf{}; // RGB luminance weights and gamma-minus-one, before gamut.
    std::array<float, 4> fDstOotf{}; // RGB luminance weights and gamma-minus-one, after gamut.
};

#endif//SkColorSpaceXformSteps_DEFINED
