/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/utils/SkPatchUtils.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPoint.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSize.h"
#include "include/core/SkTypes.h"
#include "include/core/SkVertices.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkMath.h"
#include "include/private/SkTPin.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkColorData.h"
#include "src/core/SkColorSpacePriv.h"
#include "src/core/SkConvertPixels.h"
#include "src/core/SkGeometry.h"
#include "src/core/SkVx.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {
    enum CubicCtrlPts {
        kTopP0_CubicCtrlPts = 0,
        kTopP1_CubicCtrlPts = 1,
        kTopP2_CubicCtrlPts = 2,
        kTopP3_CubicCtrlPts = 3,

        kRightP0_CubicCtrlPts = 3,
        kRightP1_CubicCtrlPts = 4,
        kRightP2_CubicCtrlPts = 5,
        kRightP3_CubicCtrlPts = 6,

        kBottomP0_CubicCtrlPts = 9,
        kBottomP1_CubicCtrlPts = 8,
        kBottomP2_CubicCtrlPts = 7,
        kBottomP3_CubicCtrlPts = 6,

        kLeftP0_CubicCtrlPts = 0,
        kLeftP1_CubicCtrlPts = 11,
        kLeftP2_CubicCtrlPts = 10,
        kLeftP3_CubicCtrlPts = 9,
    };

    // Enum for corner also clockwise.
    enum Corner {
        kTopLeft_Corner = 0,
        kTopRight_Corner,
        kBottomRight_Corner,
        kBottomLeft_Corner
    };
}  // namespace

/**
 * Evaluator to sample the values of a cubic bezier using forward differences.
 * Forward differences is a method for evaluating a nth degree polynomial at a uniform step by only
 * adding precalculated values.
 * For a linear example we have the function f(t) = m*t+b, then the value of that function at t+h
 * would be f(t+h) = m*(t+h)+b. If we want to know the uniform step that we must add to the first
 * evaluation f(t) then we need to substract f(t+h) - f(t) = m*t + m*h + b - m*t + b = mh. After
 * obtaining this value (mh) we could just add this constant step to our first sampled point
 * to compute the next one.
 *
 * For the cubic case the first difference gives as a result a quadratic polynomial to which we can
 * apply again forward differences and get linear function to which we can apply again forward
 * differences to get a constant difference. This is why we keep an array of size 4, the 0th
 * position keeps the sampled value while the next ones keep the quadratic, linear and constant
 * difference values.
 */

class FwDCubicEvaluator {
public:
    // Receives the 4 control points of the cubic bezier.
    explicit FwDCubicEvaluator(const SkPoint points[4]) : fCoefs(points) {
        std::memcpy(fPoints, points, 4 * sizeof(SkPoint));
        (*this).restart(1);
    }

    // Restarts the forward differences evaluator at t = 0, for `divisions` equal steps.
    void restart(int divisions) {
        const skvx::float2 h = 1.f / divisions;
        const skvx::float2 h2 = h * h;
        const skvx::float2 h3 = h2 * h;
        const skvx::float2 fwDiff3 = 6 * fCoefs.fA * h3;
        fFwDiff[3] = to_point(fwDiff3);
        fFwDiff[2] = to_point(fwDiff3 + times_2(fCoefs.fB) * h2);
        fFwDiff[1] = to_point(fCoefs.fA * h3 + fCoefs.fB * h2 + fCoefs.fC * h);
        fFwDiff[0] = to_point(fCoefs.fD);
    }

    // Returns the current sample and advances to the next one.
    SkPoint next() {
        const SkPoint point = fFwDiff[0];
        fFwDiff[0] += fFwDiff[1];
        fFwDiff[1] += fFwDiff[2];
        fFwDiff[2] += fFwDiff[3];
        return point;
    }

    const SkPoint* getCtrlPoints() const { return fPoints; }

private:
    SkCubicCoeff fCoefs;
    SkPoint fFwDiff[4] = {};
    SkPoint fPoints[4] = {};
};

////////////////////////////////////////////////////////////////////////////////

// size in pixels of each partition per axis, adjust this knob
static const int kPartitionSize = 10;

/**
 *  Calculate the approximate arc length given a bezier curve's control points.
 *  Returns -1 if bad calc (i.e. non-finite)
 */
static SkScalar approx_arc_length(const SkPoint points[], int count) {
    if (count < 2) {
        return 0;
    }
    SkScalar arcLength = 0;
    for (int i = 0; i < count - 1; i++) {
        arcLength += SkPoint::Distance(points[i], points[i + 1]);
    }
    return SkIsFinite(arcLength) ? arcLength : -1;
}

static SkScalar bilerp(SkScalar tx, SkScalar ty, SkScalar c00, SkScalar c10, SkScalar c01,
                       SkScalar c11) {
    const SkScalar a = c00 * (1.f - tx) + c10 * tx;
    const SkScalar b = c01 * (1.f - tx) + c11 * tx;
    return a * (1.f - ty) + b * ty;
}

static skvx::float4 bilerp(SkScalar tx, SkScalar ty,
                           const skvx::float4& c00,
                           const skvx::float4& c10,
                           const skvx::float4& c01,
                           const skvx::float4& c11) {
    const skvx::float4 a = c00 * (1.f - tx) + c10 * tx;
    const skvx::float4 b = c01 * (1.f - tx) + c11 * tx;
    return a * (1.f - ty) + b * ty;
}

SkISize SkPatchUtils::GetLevelOfDetail(const SkPoint cubics[12], const SkMatrix& matrix) {
    // Approximate length of each cubic.
    SkPoint pts[kNumPtsCubic];
    SkPatchUtils::GetTopCubic(cubics, pts);
    matrix.mapPoints(pts);
    const SkScalar topLength = approx_arc_length(pts, kNumPtsCubic);

    SkPatchUtils::GetBottomCubic(cubics, pts);
    matrix.mapPoints(pts);
    const SkScalar bottomLength = approx_arc_length(pts, kNumPtsCubic);

    SkPatchUtils::GetLeftCubic(cubics, pts);
    matrix.mapPoints(pts);
    const SkScalar leftLength = approx_arc_length(pts, kNumPtsCubic);

    SkPatchUtils::GetRightCubic(cubics, pts);
    matrix.mapPoints(pts);
    const SkScalar rightLength = approx_arc_length(pts, kNumPtsCubic);

    if (topLength < 0 || bottomLength < 0 || leftLength < 0 || rightLength < 0) {
        return {0, 0};  // negative length is a sentinel for bad length (i.e. non-finite)
    }

    // Level of detail per axis, based on the larger side between top and bottom or left and right.
    // A finite length can still exceed int; it saturates rather than converting out of range.
    const int lodX = sk_float_saturate2int(std::max(topLength, bottomLength) / kPartitionSize);
    const int lodY = sk_float_saturate2int(std::max(leftLength, rightLength) / kPartitionSize);

    return SkISize::Make(std::max(8, lodX), std::max(8, lodY));
}

void SkPatchUtils::GetTopCubic(const SkPoint cubics[12], SkPoint points[4]) {
    points[0] = cubics[kTopP0_CubicCtrlPts];
    points[1] = cubics[kTopP1_CubicCtrlPts];
    points[2] = cubics[kTopP2_CubicCtrlPts];
    points[3] = cubics[kTopP3_CubicCtrlPts];
}

void SkPatchUtils::GetBottomCubic(const SkPoint cubics[12], SkPoint points[4]) {
    points[0] = cubics[kBottomP0_CubicCtrlPts];
    points[1] = cubics[kBottomP1_CubicCtrlPts];
    points[2] = cubics[kBottomP2_CubicCtrlPts];
    points[3] = cubics[kBottomP3_CubicCtrlPts];
}

void SkPatchUtils::GetLeftCubic(const SkPoint cubics[12], SkPoint points[4]) {
    points[0] = cubics[kLeftP0_CubicCtrlPts];
    points[1] = cubics[kLeftP1_CubicCtrlPts];
    points[2] = cubics[kLeftP2_CubicCtrlPts];
    points[3] = cubics[kLeftP3_CubicCtrlPts];
}

void SkPatchUtils::GetRightCubic(const SkPoint cubics[12], SkPoint points[4]) {
    points[0] = cubics[kRightP0_CubicCtrlPts];
    points[1] = cubics[kRightP1_CubicCtrlPts];
    points[2] = cubics[kRightP2_CubicCtrlPts];
    points[3] = cubics[kRightP3_CubicCtrlPts];
}

static void skcolor_to_float(SkPMColor4f* dst, const SkColor* src, int count, SkColorSpace* dstCS) {
    SkImageInfo srcInfo = SkImageInfo::Make(count, 1, kBGRA_8888_SkColorType,
                                            kUnpremul_SkAlphaType, SkColorSpace::MakeSRGB());
    SkImageInfo dstInfo = SkImageInfo::Make(count, 1, kRGBA_F32_SkColorType,
                                            kPremul_SkAlphaType, sk_ref_sp(dstCS));
    SkAssertResult(SkConvertPixels(dstInfo, dst, 0, srcInfo, src, 0));
}

static void float_to_skcolor(SkColor* dst, const SkPMColor4f* src, int count, SkColorSpace* srcCS) {
    SkImageInfo srcInfo = SkImageInfo::Make(count, 1, kRGBA_F32_SkColorType,
                                            kPremul_SkAlphaType, sk_ref_sp(srcCS));
    SkImageInfo dstInfo = SkImageInfo::Make(count, 1, kBGRA_8888_SkColorType,
                                            kUnpremul_SkAlphaType, SkColorSpace::MakeSRGB());
    SkAssertResult(SkConvertPixels(dstInfo, dst, 0, srcInfo, src, 0));
}

sk_sp<SkVertices> SkPatchUtils::MakeVertices(const SkPoint cubics[12], const SkColor srcColors[4],
                                             const SkPoint srcTexCoords[4], int lodX, int lodY,
                                             SkColorSpace* colorSpace) {
    if (lodX < 1 || lodY < 1 || cubics == nullptr) {
        return nullptr;
    }

    // Treat null interpolation space as sRGB.
    if (!colorSpace) {
        colorSpace = sk_srgb_singleton();
    }

    // It is recommended to generate draw calls of no more than 65536 indices, so we never generate
    // more than 60000 indices. Levels whose vertex count would pass 10000, or either of which
    // passes 200, are reduced in proportion before any count is formed. The counts are taken in
    // 64 bits, so every level, up to INT_MAX, is tessellated rather than refused.
    const int64_t requestedVertices =
            (static_cast<int64_t>(lodX) + 1) * (static_cast<int64_t>(lodY) + 1);
    if (requestedVertices > 10000 || lodX > 200 || lodY > 200) {
        const float total = static_cast<float>(static_cast<int64_t>(lodX) + lodY);
        const float weightX = static_cast<float>(lodX) / total;
        const float weightY = static_cast<float>(lodY) / total;

        // 200 comes from the 100 * 2 which is the max value of vertices because of the limit of
        // 60000 indices ( sqrt(60000 / 6) that comes from an index count of lodX * lodY * 6)
        // Need a min of 1 since we later divide by lod
        lodX = std::max(1, sk_float_floor2int_no_saturate(weightX * 200));
        lodY = std::max(1, sk_float_floor2int_no_saturate(weightY * 200));
    }
    const int vertexCount = (lodX + 1) * (lodY + 1);
    const int indexCount = lodX * lodY * 6;
    uint32_t flags = 0;
    if (srcTexCoords) {
        flags |= SkVertices::kHasTexCoords_BuilderFlag;
    }
    if (srcColors) {
        flags |= SkVertices::kHasColors_BuilderFlag;
    }

    SkSTArenaAlloc<2048> alloc;
    SkPMColor4f* cornerColors = srcColors ? alloc.makeArray<SkPMColor4f>(4) : nullptr;
    SkPMColor4f* tmpColors = srcColors ? alloc.makeArray<SkPMColor4f>(vertexCount) : nullptr;

    SkVertices::Builder builder(SkVertices::kTriangles_VertexMode, vertexCount, indexCount, flags);
    SkPoint* pos = builder.positions();
    SkPoint* texs = builder.texCoords();
    uint16_t* indices = builder.indices();

    if (cornerColors) {
        skcolor_to_float(cornerColors, srcColors, kNumCorners, colorSpace);
    }

    SkPoint pts[kNumPtsCubic];
    SkPatchUtils::GetBottomCubic(cubics, pts);
    FwDCubicEvaluator bottomEdge(pts);
    SkPatchUtils::GetTopCubic(cubics, pts);
    FwDCubicEvaluator topEdge(pts);
    SkPatchUtils::GetLeftCubic(cubics, pts);
    FwDCubicEvaluator leftEdge(pts);
    SkPatchUtils::GetRightCubic(cubics, pts);
    FwDCubicEvaluator rightEdge(pts);

    bottomEdge.restart(lodX);
    topEdge.restart(lodX);

    SkScalar u = 0.0f;
    const int stride = lodY + 1;
    for (int x = 0; x <= lodX; x++) {
        const SkPoint bottom = bottomEdge.next();
        const SkPoint top = topEdge.next();
        leftEdge.restart(lodY);
        rightEdge.restart(lodY);
        SkScalar v = 0.f;
        for (int y = 0; y <= lodY; y++) {
            const int dataIndex = x * (lodY + 1) + y;

            const SkPoint left = leftEdge.next();
            const SkPoint right = rightEdge.next();

            const SkPoint s0 = SkPoint::Make((1.0f - v) * top.x() + v * bottom.x(),
                                       (1.0f - v) * top.y() + v * bottom.y());
            const SkPoint s1 = SkPoint::Make((1.0f - u) * left.x() + u * right.x(),
                                       (1.0f - u) * left.y() + u * right.y());
            const SkPoint s2 = SkPoint::Make(
                                       (1.0f - v) * ((1.0f - u) * topEdge.getCtrlPoints()[0].x()
                                                     + u * topEdge.getCtrlPoints()[3].x())
                                       + v * ((1.0f - u) * bottomEdge.getCtrlPoints()[0].x()
                                              + u * bottomEdge.getCtrlPoints()[3].x()),
                                       (1.0f - v) * ((1.0f - u) * topEdge.getCtrlPoints()[0].y()
                                                     + u * topEdge.getCtrlPoints()[3].y())
                                       + v * ((1.0f - u) * bottomEdge.getCtrlPoints()[0].y()
                                              + u * bottomEdge.getCtrlPoints()[3].y()));
            pos[dataIndex] = s0 + s1 - s2;

            if (cornerColors) {
                bilerp(u, v, skvx::float4::Load(cornerColors[kTopLeft_Corner].vec()),
                             skvx::float4::Load(cornerColors[kTopRight_Corner].vec()),
                             skvx::float4::Load(cornerColors[kBottomLeft_Corner].vec()),
                             skvx::float4::Load(cornerColors[kBottomRight_Corner].vec()))
                    .store(tmpColors[dataIndex].vec());
            }

            if (texs) {
                texs[dataIndex] = SkPoint::Make(bilerp(u, v, srcTexCoords[kTopLeft_Corner].x(),
                                                       srcTexCoords[kTopRight_Corner].x(),
                                                       srcTexCoords[kBottomLeft_Corner].x(),
                                                       srcTexCoords[kBottomRight_Corner].x()),
                                                bilerp(u, v, srcTexCoords[kTopLeft_Corner].y(),
                                                       srcTexCoords[kTopRight_Corner].y(),
                                                       srcTexCoords[kBottomLeft_Corner].y(),
                                                       srcTexCoords[kBottomRight_Corner].y()));

            }

            if (x < lodX && y < lodY) {
                const int i = 6 * (x * lodY + y);
                indices[i] = x * stride + y;
                indices[i + 1] = x * stride + 1 + y;
                indices[i + 2] = (x + 1) * stride + 1 + y;
                indices[i + 3] = indices[i];
                indices[i + 4] = indices[i + 2];
                indices[i + 5] = (x + 1) * stride + y;
            }
            v = SkTPin(v + 1.f / lodY, 0.0f, 1.0f);
        }
        u = SkTPin(u + 1.f / lodX, 0.0f, 1.0f);
    }

    if (tmpColors) {
        float_to_skcolor(builder.colors(), tmpColors, vertexCount, colorSpace);
    }
    return builder.detach();
}
