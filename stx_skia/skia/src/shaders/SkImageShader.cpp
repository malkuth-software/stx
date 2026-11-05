/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/shaders/SkImageShader.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkColorType.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkScalar.h"
#include "include/core/SkShader.h"
#include "include/core/SkTileMode.h"
#include "include/private/SkMath.h"
#include "modules/skcms/skcms.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkColorSpaceXformSteps.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkImageInfoPriv.h"
#include "src/core/SkMipmapAccessor.h"
#include "src/core/SkPicturePriv.h"
#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpContexts.h"
#include "src/core/SkRasterPipelineOpList.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkSamplingPriv.h"
#include "src/core/SkWriteBuffer.h"
#include "src/image/SkImage_Base.h"

#include <array>
#include <cmath>
#include <optional>
#include <utility>

class SkColorSpace;

SkM44 SkImageShader::CubicResamplerMatrix(float B, float C) {
    const SkM44 matrix(    (1.f/6)*B, -(3.f/6)*B - C,       (3.f/6)*B + 2*C,    - (1.f/6)*B - C,
                 1 - (2.f/6)*B,              0, -3 + (12.f/6)*B +   C,  2 - (9.f/6)*B - C,
                     (1.f/6)*B,  (3.f/6)*B + C,  3 - (15.f/6)*B - 2*C, -2 + (9.f/6)*B + C,
                             0,              0,                    -C,      (1.f/6)*B + C);
    return matrix;
}

/**
 *  We are faster in clamp, so always use that tiling when we can.
 */
static SkTileMode optimize(SkTileMode tm, int dimension) {
    SkASSERT(dimension > 0);
#ifdef SK_BUILD_FOR_ANDROID_FRAMEWORK
    // need to update frameworks/base/libs/hwui/tests/unit/SkiaBehaviorTests.cpp:55 to allow
    // for transforming to clamp.
    return tm;
#else
    // mirror and repeat on a 1px axis are the same as clamping, but decal will still transition to
    // transparent black.
    const SkTileMode optimized = (tm != SkTileMode::kDecal && dimension == 1)
            ? SkTileMode::kClamp : tm;
    return optimized;
#endif
}

SkImageShader::SkImageShader(sk_sp<SkImage> img,
                             SkTileMode tmx, SkTileMode tmy,
                             const SkSamplingOptions& sampling,
                             bool raw,
                             bool clampAsIfUnpremul)
        : fImage(std::move(img))
        , fSampling(sampling)
        , fTileModeX(optimize(tmx, (*fImage).width()))
        , fTileModeY(optimize(tmy, (*fImage).height()))
        , fRaw(raw)
        , fClampAsIfUnpremul(clampAsIfUnpremul) {
    // These options should never appear together:
    SkASSERT(!fRaw || !fClampAsIfUnpremul);

    // Bicubic filtering of raw image shaders would add a surprising clamp - so we don't support it
    SkASSERT(!fRaw || !fSampling.useCubic);
}

// fClampAsIfUnpremul is always false when constructed through public APIs,
// so there's no need to read or write it here.

sk_sp<SkFlattenable> SkImageShader::CreateProc(SkReadBuffer& buffer) {
    const SkTileMode tmx = buffer.read32LE<SkTileMode>(SkTileMode::kLastTileMode);
    const SkTileMode tmy = buffer.read32LE<SkTileMode>(SkTileMode::kLastTileMode);

    SkSamplingOptions sampling{};
    bool readSampling = true;
    if (buffer.isVersionLT(SkPicturePriv::kNoFilterQualityShaders_Version) &&
        !buffer.readBool() /* legacy has_sampling */)
    {
        readSampling = false;
        // we just default to Nearest in sampling
    }
    if (readSampling) {
        sampling = buffer.readSampling();
    }

    SkMatrix localMatrix{};
    if (buffer.isVersionLT(SkPicturePriv::Version::kNoShaderLocalMatrix)) {
        buffer.readMatrix(&localMatrix);
    }
    sk_sp<SkImage> img = buffer.readImage();
    if (!img) {
        return nullptr;
    }

    const bool raw = buffer.isVersionLT(SkPicturePriv::Version::kRawImageShaders) ? false
                                                                            : buffer.readBool();

    sk_sp<SkFlattenable> result{};
    if (raw) {
        result = SkImageShader::MakeRaw(std::move(img), tmx, tmy, sampling, &localMatrix);
    } else {
        result = SkImageShader::Make(std::move(img), tmx, tmy, sampling, &localMatrix);
    }
    return result;
}

void SkImageShader::flatten(SkWriteBuffer& buffer) const {
    buffer.writeUInt(static_cast<unsigned>(fTileModeX));
    buffer.writeUInt(static_cast<unsigned>(fTileModeY));

    buffer.writeSampling(fSampling);

    buffer.writeImage(fImage.get());
    SkASSERT(fClampAsIfUnpremul == false);

    buffer.writeBool(fRaw);
}

bool SkImageShader::isOpaque() const {
    const bool opaque = (*fImage).isOpaque() &&
           fTileModeX != SkTileMode::kDecal && fTileModeY != SkTileMode::kDecal;
    return opaque;
}

SkImage* SkImageShader::onIsAImage(SkMatrix* texM, SkTileMode xy[]) const {
    if (texM) {
        *texM = SkMatrix::I();
    }
    if (xy) {
        xy[0] = fTileModeX;
        xy[1] = fTileModeY;
    }
    SkImage* image = fImage.get();
    return image;
}

sk_sp<SkShader> SkImageShader::Make(sk_sp<SkImage> image,
                                    SkTileMode tmx, SkTileMode tmy,
                                    const SkSamplingOptions& options,
                                    const SkMatrix* localMatrix,
                                    bool clampAsIfUnpremul) {
    if (options.useCubic) {
        if (!(options.cubic.B >= 0 && options.cubic.B <= 1) ||
            !(options.cubic.C >= 0 && options.cubic.C <= 1)) {
            return nullptr;
        }
    }
    if (!image) {
        sk_sp<SkShader> empty = SkShaders::Empty();
        return empty;
    }
    const SkISize dimensions = (*image).dimensions();
    if (dimensions.isEmpty()) {
        sk_sp<SkShader> empty = SkShaders::Empty();
        return empty;
    }
    sk_sp<SkShader> shader = sk_make_sp<SkImageShader>(std::move(image), tmx, tmy,
                                                     options, false, clampAsIfUnpremul);
    const SkMatrix matrix = localMatrix ? *localMatrix : SkMatrix::I();
    sk_sp<SkShader> wrapped = (*shader).makeWithLocalMatrix(matrix);
    return wrapped;
}

sk_sp<SkShader> SkImageShader::MakeRaw(sk_sp<SkImage> image,
                                       SkTileMode tmx, SkTileMode tmy,
                                       const SkSamplingOptions& options,
                                       const SkMatrix* localMatrix) {
    if (options.useCubic) {
        return nullptr;
    }
    if (!image) {
        sk_sp<SkShader> empty = SkShaders::Empty();
        return empty;
    }
    sk_sp<SkShader> s = sk_make_sp<SkImageShader>(std::move(image),
                                                  tmx, tmy,
                                                  options,
                                                  /*raw=*/true,
                                                  /*clampAsIfUnpremul=*/false);
    const SkMatrix matrix = localMatrix ? *localMatrix : SkMatrix::I();
    sk_sp<SkShader> wrapped = (*s).makeWithLocalMatrix(matrix);
    return wrapped;
}

namespace {

struct MipLevelHelper {
    SkPixmap pm{};
    SkMatrix inv{};
    SkRasterPipelineContexts::GatherCtx* gather = nullptr;
    SkRasterPipelineContexts::TileCtx* limitX = nullptr;
    SkRasterPipelineContexts::TileCtx* limitY = nullptr;
    SkRasterPipelineContexts::DecalTileCtx* decalCtx = nullptr;

    void allocAndInit(SkArenaAlloc* alloc,
                      const SkSamplingOptions& sampling,
                      SkTileMode tileModeX,
                      SkTileMode tileModeY) {
        gather = (*alloc).make<SkRasterPipelineContexts::GatherCtx>();
        (*gather).pixels = pm.addr();
        (*gather).stride = pm.rowBytesAsPixels();
        (*gather).width = pm.width();
        (*gather).height = pm.height();

        if (sampling.useCubic) {
            const SkM44 weights = SkImageShader::CubicResamplerMatrix(sampling.cubic.B, sampling.cubic.C);
            weights.getColMajor((*gather).weights);
        }

        limitX = (*alloc).make<SkRasterPipelineContexts::TileCtx>();
        limitY = (*alloc).make<SkRasterPipelineContexts::TileCtx>();
        (*limitX).scale = pm.width();
        (*limitX).invScale = 1.0f / pm.width();
        (*limitY).scale = pm.height();
        (*limitY).invScale = 1.0f / pm.height();

        // We would like an image that is mapped 1:1 with device pixels but at a half pixel offset
        // to select every pixel from the src image once. Our rasterizer biases upward. That is a
        // rect from 0.5...1.5 fills pixel 1 and not pixel 0. So we make exact integer pixel sample
        // values select the pixel to the left/above the integer value.
        //
        // Note that a mirror mapping between canvas and image space will not have this property -
        // on one side of the image a row/column will be skipped and one repeated on the other side.
        //
        // The GM nearest_half_pixel_image tests both of the above scenarios.
        //
        // The implementation of SkTileMode::kMirror also modifies integer pixel snapping to create
        // consistency when the sample coords are running backwards and must account for gather
        // modification we perform here. The GM mirror_tile tests this.
        if (!sampling.useCubic && sampling.filter == SkFilterMode::kNearest) {
            (*gather).roundDownAtInteger = true;
            (*limitX).mirrorBiasDir = 1;
            (*limitY).mirrorBiasDir = 1;
        }

        if (tileModeX == SkTileMode::kDecal || tileModeY == SkTileMode::kDecal) {
            decalCtx = (*alloc).make<SkRasterPipelineContexts::DecalTileCtx>();
            (*decalCtx).limit_x = (*limitX).scale;
            (*decalCtx).limit_y = (*limitY).scale;

            // When integer sample coords snap left/up then we want the right/bottom edge of the
            // image bounds to be inside the image rather than the left/top edge, that is (0, w]
            // rather than [0, w).
            if ((*gather).roundDownAtInteger) {
                (*decalCtx).inclusiveEdge_x = (*decalCtx).limit_x;
                (*decalCtx).inclusiveEdge_y = (*decalCtx).limit_y;
            }
        }
    }
};

// Borrows the pipeline and arena-owned sampler only while constructing stages.
struct SamplingStages {
    SkRasterPipeline& pipeline;
    const SkSamplingOptions& sampling;
    SkTileMode tileModeX;
    SkTileMode tileModeY;
    SkRasterPipelineContexts::SamplerCtx& sampler;
    bool decalBothAxes;

    bool appendGather(const MipLevelHelper& level) {
        if (decalBothAxes) {
            pipeline.append(SkRasterPipelineOp::decal_x_and_y,  level.decalCtx);
        } else {
            switch (tileModeX) {
                case SkTileMode::kClamp: /* The gather_xxx stage will clamp for us. */
                    break;
                case SkTileMode::kMirror:
                    pipeline.append(SkRasterPipelineOp::mirror_x, level.limitX);
                    break;
                case SkTileMode::kRepeat:
                    pipeline.append(SkRasterPipelineOp::repeat_x, level.limitX);
                    break;
                case SkTileMode::kDecal:
                    pipeline.append(SkRasterPipelineOp::decal_x, level.decalCtx);
                    break;
                default:
                    return false;
            }
            switch (tileModeY) {
                case SkTileMode::kClamp: /* The gather_xxx stage will clamp for us. */
                    break;
                case SkTileMode::kMirror:
                    pipeline.append(SkRasterPipelineOp::mirror_y, level.limitY);
                    break;
                case SkTileMode::kRepeat:
                    pipeline.append(SkRasterPipelineOp::repeat_y, level.limitY);
                    break;
                case SkTileMode::kDecal:
                    pipeline.append(SkRasterPipelineOp::decal_y, level.decalCtx);
                    break;
                default:
                    return false;
            }
        }

        SkRasterPipelineContexts::GatherCtx* ctx = level.gather;
        switch (level.pm.colorType()) {
            case kAlpha_8_SkColorType:      pipeline.append(SkRasterPipelineOp::gather_a8,    ctx); break;
            case kA16_unorm_SkColorType:    pipeline.append(SkRasterPipelineOp::gather_a16,   ctx); break;
            case kA16_float_SkColorType:    pipeline.append(SkRasterPipelineOp::gather_af16,  ctx); break;
            case kR16_float_SkColorType:    pipeline.append(SkRasterPipelineOp::gather_rf16,  ctx); break;
            case kRGB_565_SkColorType:      pipeline.append(SkRasterPipelineOp::gather_565,   ctx); break;
            case kARGB_4444_SkColorType:    pipeline.append(SkRasterPipelineOp::gather_4444,  ctx); break;
            case kR8G8_unorm_SkColorType:   pipeline.append(SkRasterPipelineOp::gather_rg88,  ctx); break;
            case kR16_unorm_SkColorType:    pipeline.append(SkRasterPipelineOp::gather_r16,   ctx); break;
            case kR16G16_unorm_SkColorType: pipeline.append(SkRasterPipelineOp::gather_rg1616,ctx); break;
            case kR16G16_float_SkColorType: pipeline.append(SkRasterPipelineOp::gather_rgf16, ctx); break;
            case kRGBA_8888_SkColorType:    pipeline.append(SkRasterPipelineOp::gather_8888,  ctx); break;

            case kRGBA_1010102_SkColorType:
                pipeline.append(SkRasterPipelineOp::gather_1010102, ctx);
                break;

            case kR16G16B16A16_unorm_SkColorType:
                pipeline.append(SkRasterPipelineOp::gather_16161616, ctx);
                break;

            case kRGBA_F16Norm_SkColorType:
            case kRGBA_F16_SkColorType:     pipeline.append(SkRasterPipelineOp::gather_f16,   ctx); break;
            case kRGBA_F32_SkColorType:     pipeline.append(SkRasterPipelineOp::gather_f32,   ctx); break;
            case kBGRA_10101010_XR_SkColorType:
                pipeline.append(SkRasterPipelineOp::gather_10101010_xr,  ctx);
                pipeline.append(SkRasterPipelineOp::swap_rb);
                break;
            case kRGBA_10x6_SkColorType:    pipeline.append(SkRasterPipelineOp::gather_10x6,  ctx); break;

            case kGray_8_SkColorType:       pipeline.append(SkRasterPipelineOp::gather_a8,    ctx);
                                            pipeline.append(SkRasterPipelineOp::alpha_to_gray    ); break;

            case kR8_unorm_SkColorType:     pipeline.append(SkRasterPipelineOp::gather_a8,    ctx);
                                            pipeline.append(SkRasterPipelineOp::alpha_to_red     ); break;

            case kRGB_888x_SkColorType:     pipeline.append(SkRasterPipelineOp::gather_8888,  ctx);
                                            pipeline.append(SkRasterPipelineOp::force_opaque     ); break;
            case kRGB_F16F16F16x_SkColorType:
                pipeline.append(SkRasterPipelineOp::gather_f16,  ctx);
                pipeline.append(SkRasterPipelineOp::force_opaque);
                break;
            case kBGRA_1010102_SkColorType:
                pipeline.append(SkRasterPipelineOp::gather_1010102, ctx);
                pipeline.append(SkRasterPipelineOp::swap_rb);
                break;

            case kRGB_101010x_SkColorType:
                pipeline.append(SkRasterPipelineOp::gather_1010102, ctx);
                pipeline.append(SkRasterPipelineOp::force_opaque);
                break;

            case kBGR_101010x_XR_SkColorType:
                pipeline.append(SkRasterPipelineOp::gather_1010102_xr, ctx);
                pipeline.append(SkRasterPipelineOp::force_opaque);
                pipeline.append(SkRasterPipelineOp::swap_rb);
                break;

            case kBGR_101010x_SkColorType:
                pipeline.append(SkRasterPipelineOp::gather_1010102, ctx);
                pipeline.append(SkRasterPipelineOp::force_opaque);
                pipeline.append(SkRasterPipelineOp::swap_rb);
                break;

            case kBGRA_8888_SkColorType:
                pipeline.append(SkRasterPipelineOp::gather_8888, ctx);
                pipeline.append(SkRasterPipelineOp::swap_rb);
                break;

            case kSRGBA_8888_SkColorType:
                pipeline.append(SkRasterPipelineOp::gather_8888, ctx);
                pipeline.appendTransferFunction(*skcms_sRGB_TransferFunction());
                break;

            case kUnknown_SkColorType:
            default:
                return false;
        }
        if (level.decalCtx) {
            pipeline.append(SkRasterPipelineOp::check_decal_mask, level.decalCtx);
        }

        return true;
    }

    bool sample(SkRasterPipelineOp setup_x, SkRasterPipelineOp setup_y,
                const MipLevelHelper& level) {
        pipeline.append(setup_x, &sampler);
        pipeline.append(setup_y, &sampler);
        if (!appendGather(level)) return false;
        pipeline.append(SkRasterPipelineOp::accumulate, &sampler);

        return true;
    }

    bool sampleLevel(const MipLevelHelper& level) {
        if (sampling.useCubic) {
            const SkM44 weights = SkImageShader::CubicResamplerMatrix(sampling.cubic.B, sampling.cubic.C);
            weights.getColMajor(sampler.weights);

            pipeline.append(SkRasterPipelineOp::bicubic_setup, &sampler);

            if (!sample(SkRasterPipelineOp::bicubic_n3x, SkRasterPipelineOp::bicubic_n3y, level)) return false;
            if (!sample(SkRasterPipelineOp::bicubic_n1x, SkRasterPipelineOp::bicubic_n3y, level)) return false;
            if (!sample(SkRasterPipelineOp::bicubic_p1x, SkRasterPipelineOp::bicubic_n3y, level)) return false;
            if (!sample(SkRasterPipelineOp::bicubic_p3x, SkRasterPipelineOp::bicubic_n3y, level)) return false;

            if (!sample(SkRasterPipelineOp::bicubic_n3x, SkRasterPipelineOp::bicubic_n1y, level)) return false;
            if (!sample(SkRasterPipelineOp::bicubic_n1x, SkRasterPipelineOp::bicubic_n1y, level)) return false;
            if (!sample(SkRasterPipelineOp::bicubic_p1x, SkRasterPipelineOp::bicubic_n1y, level)) return false;
            if (!sample(SkRasterPipelineOp::bicubic_p3x, SkRasterPipelineOp::bicubic_n1y, level)) return false;

            if (!sample(SkRasterPipelineOp::bicubic_n3x, SkRasterPipelineOp::bicubic_p1y, level)) return false;
            if (!sample(SkRasterPipelineOp::bicubic_n1x, SkRasterPipelineOp::bicubic_p1y, level)) return false;
            if (!sample(SkRasterPipelineOp::bicubic_p1x, SkRasterPipelineOp::bicubic_p1y, level)) return false;
            if (!sample(SkRasterPipelineOp::bicubic_p3x, SkRasterPipelineOp::bicubic_p1y, level)) return false;

            if (!sample(SkRasterPipelineOp::bicubic_n3x, SkRasterPipelineOp::bicubic_p3y, level)) return false;
            if (!sample(SkRasterPipelineOp::bicubic_n1x, SkRasterPipelineOp::bicubic_p3y, level)) return false;
            if (!sample(SkRasterPipelineOp::bicubic_p1x, SkRasterPipelineOp::bicubic_p3y, level)) return false;
            if (!sample(SkRasterPipelineOp::bicubic_p3x, SkRasterPipelineOp::bicubic_p3y, level)) return false;

            pipeline.append(SkRasterPipelineOp::move_dst_src);
        } else if (sampling.filter == SkFilterMode::kLinear) {
            pipeline.append(SkRasterPipelineOp::bilinear_setup, &sampler);

            if (!sample(SkRasterPipelineOp::bilinear_nx, SkRasterPipelineOp::bilinear_ny, level)) return false;
            if (!sample(SkRasterPipelineOp::bilinear_px, SkRasterPipelineOp::bilinear_ny, level)) return false;
            if (!sample(SkRasterPipelineOp::bilinear_nx, SkRasterPipelineOp::bilinear_py, level)) return false;
            if (!sample(SkRasterPipelineOp::bilinear_px, SkRasterPipelineOp::bilinear_py, level)) return false;

            pipeline.append(SkRasterPipelineOp::move_dst_src);
        } else {
            if (!appendGather(level)) return false;
        }

        return true;
    }
};

}  // namespace

static SkSamplingOptions tweak_sampling(SkSamplingOptions sampling, const SkMatrix& matrix) {
    SkFilterMode filter = sampling.filter;

    // When the matrix is just an integer translate, bilerp == nearest neighbor.
    if (filter == SkFilterMode::kLinear && matrix.getType() <= SkMatrix::kTranslate_Mask) {
        const float x = matrix.getTranslateX();
        const float y = matrix.getTranslateY();
        // Large finite translations can be integral without fitting in int.
        // Test integrality in the computation's own precision, without narrowing.
        const bool integral = std::isfinite(x) && std::isfinite(y) &&
                              x == std::trunc(x) && y == std::trunc(y);
        if (integral) {
            filter = SkFilterMode::kNearest;
        }
    }

    const SkSamplingOptions adjusted(filter, sampling.mipmap);
    return adjusted;
}

void SkImageShader::appendColorStages(const SkStageRec& rec, const SkPixmap& pixels,
                                      const SkSamplingOptions& sampling) const {
    SkColorSpace* cs = pixels.colorSpace();
    SkAlphaType   at = pixels.alphaType();

    // Color for alpha-only images comes from the paint (already converted to dst color space).
    if (SkColorTypeIsAlphaOnly(pixels.colorType()) && !fRaw) {
        (*rec.fPipeline).appendSetRGB(rec.fAlloc, rec.fPaintColor);

        cs = rec.fDstCS;
        at = kUnpremul_SkAlphaType;
    }

    // Bicubic filtering naturally produces out of range values on both sides of [0,1].
    if (sampling.useCubic) {
        (*rec.fPipeline).append(at == kUnpremul_SkAlphaType || fClampAsIfUnpremul
                      ? SkRasterPipelineOp::clamp_01
                      : SkRasterPipelineOp::clamp_gamut);
    }

    // Transform color space and alpha type to match shader convention (dst CS, premul alpha).
    if (!fRaw) {
        const SkColorSpaceXformSteps* conversion = (*rec.fAlloc).make<SkColorSpaceXformSteps>(
                cs, at, rec.fDstCS, kPremul_SkAlphaType);
        (*conversion).apply(rec.fPipeline);
    }
}

bool SkImageShader::appendStages(const SkStageRec& rec, const SkShaders::MatrixRec& mRec) const {

    // We only support certain sampling options in stages so far
    SkSamplingOptions sampling = fSampling;
    if (sampling.isAniso()) {
        sampling = SkSamplingPriv::AnisoFallback((*fImage).hasMipmaps());
    }

    SkRasterPipeline* p = rec.fPipeline;
    SkArenaAlloc* alloc = rec.fAlloc;

    SkMatrix baseInv{};
    // If the total matrix isn't valid then we will always access the base MIP level.
    if (mRec.totalMatrixIsValid()) {
        const std::optional<SkMatrix> inv = mRec.totalInverse();
        if (!inv) {
            return false;
        }
        baseInv = *inv;
        baseInv.normalizePerspective();
    }

    SkASSERT(!sampling.useCubic || sampling.mipmap == SkMipmapMode::kNone);
    SkMipmapAccessor* access = SkMipmapAccessor::Make(alloc, fImage.get(), baseInv, sampling.mipmap);
    if (!access) {
        return false;
    }

    MipLevelHelper upper{};
    const std::pair<SkPixmap, SkMatrix> upperLevel = (*access).level();
    upper.pm = upperLevel.first;
    upper.inv = upperLevel.second;

    if (!sampling.useCubic) {
        // TODO: can tweak_sampling sometimes for cubic too when B=0
        if (mRec.totalMatrixIsValid()) {
            const SkMatrix total = SkMatrix::Concat(upper.inv, baseInv);
            sampling = tweak_sampling(sampling, total);
        }
    }

    if (!mRec.apply(rec, upper.inv)) {
        return false;
    }

    upper.allocAndInit(alloc, sampling, fTileModeX, fTileModeY);

    MipLevelHelper lower{};
    SkRasterPipelineContexts::MipmapCtx* mipmapCtx = nullptr;
    const float lowerWeight = (*access).lowerWeight();
    if (lowerWeight > 0) {
        const std::pair<SkPixmap, SkMatrix> lowerLevel = (*access).lowerLevel();
        lower.pm = lowerLevel.first;
        lower.inv = lowerLevel.second;
        mipmapCtx = (*alloc).make<SkRasterPipelineContexts::MipmapCtx>();
        (*mipmapCtx).lowerWeight = lowerWeight;
        (*mipmapCtx).scaleX = static_cast<float>(lower.pm.width()) / upper.pm.width();
        (*mipmapCtx).scaleY = static_cast<float>(lower.pm.height()) / upper.pm.height();

        lower.allocAndInit(alloc, sampling, fTileModeX, fTileModeY);

        (*p).append(SkRasterPipelineOp::mipmap_linear_init, mipmapCtx);
    }

    const bool decalBothAxes = fTileModeX == SkTileMode::kDecal && fTileModeY == SkTileMode::kDecal;

    // Check for fast-path stages.
    // TODO: Could we use the fast-path stages for each level when doing linear mipmap filtering?
    const SkColorType ct = upper.pm.colorType();
    if ((ct == kRGBA_8888_SkColorType || ct == kBGRA_8888_SkColorType) &&
        !sampling.useCubic && sampling.filter == SkFilterMode::kLinear &&
        sampling.mipmap != SkMipmapMode::kLinear &&
        fTileModeX == SkTileMode::kClamp && fTileModeY == SkTileMode::kClamp) {
        // Check bounding box of points we will sample to see if we can use lowp
        // and not over/under flow.
        bool shouldUseHighPBilerp = false;
        if (!rec.fDstBounds.isEmpty()) {
            std::array<SkPoint, 4> quad = rec.fDstBounds.toQuad();
            baseInv.mapPoints(quad);
            SkRect deviceImageSpace{};
            deviceImageSpace.setBounds(quad);
            for (const float val : SkSpan<const float>(deviceImageSpace.asScalars(), 4)) {
                if (val > INT16_MAX || val < INT16_MIN || !std::isfinite(val)) {
                    shouldUseHighPBilerp = true;
                    break;
                }
            }
        }

        if (shouldUseHighPBilerp) {
            (*p).append(SkRasterPipelineOp::bilerp_clamp_8888_force_highp, upper.gather);
        } else {
            (*p).append(SkRasterPipelineOp::bilerp_clamp_8888, upper.gather);
        }

        if (ct == kBGRA_8888_SkColorType) {
            (*p).append(SkRasterPipelineOp::swap_rb);
        }
        appendColorStages(rec, upper.pm, sampling);
        return true;
    }
    if ((ct == kRGBA_8888_SkColorType || ct == kBGRA_8888_SkColorType) &&
        sampling.useCubic &&
        fTileModeX == SkTileMode::kClamp && fTileModeY == SkTileMode::kClamp) {

        (*p).append(SkRasterPipelineOp::bicubic_clamp_8888, upper.gather);
        if (ct == kBGRA_8888_SkColorType) {
            (*p).append(SkRasterPipelineOp::swap_rb);
        }
        appendColorStages(rec, upper.pm, sampling);
        return true;
    }

    // This context can be shared by both levels when doing linear mipmap filtering
    SkRasterPipelineContexts::SamplerCtx* sampler =
            (*alloc).make<SkRasterPipelineContexts::SamplerCtx>();

    SamplingStages stages{*p, sampling, fTileModeX, fTileModeY, *sampler, decalBothAxes};
    if (!stages.sampleLevel(upper)) return false;

    if (mipmapCtx) {
        (*p).append(SkRasterPipelineOp::mipmap_linear_update, mipmapCtx);
        if (!stages.sampleLevel(lower)) return false;
        (*p).append(SkRasterPipelineOp::mipmap_linear_finish, mipmapCtx);
    }

    appendColorStages(rec, upper.pm, sampling);
    return true;
}

namespace SkShaders {

sk_sp<SkShader> Image(sk_sp<SkImage> image,
                      SkTileMode tmx, SkTileMode tmy,
                      const SkSamplingOptions& options,
                      const SkMatrix* localMatrix) {
    sk_sp<SkShader> shader = SkImageShader::Make(std::move(image), tmx, tmy, options, localMatrix);
    return shader;
}

sk_sp<SkShader> RawImage(sk_sp<SkImage> image,
                         SkTileMode tmx, SkTileMode tmy,
                         const SkSamplingOptions& options,
                         const SkMatrix* localMatrix) {
    sk_sp<SkShader> shader = SkImageShader::MakeRaw(std::move(image), tmx, tmy, options, localMatrix);
    return shader;
}

}  // namespace SkShaders
