/*
 * Copyright 2010 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkImageInfo.h"

#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/private/SkAssert.h"
#include "src/core/SkImageInfoPriv.h"
#include "src/core/SkSafeMath.h"

int SkColorTypeBytesPerPixel(SkColorType ct) {
    switch (ct) {
        case kUnknown_SkColorType:            return 0;
        case kAlpha_8_SkColorType:            return 1;
        case kRGB_565_SkColorType:            return 2;
        case kARGB_4444_SkColorType:          return 2;
        case kRGBA_8888_SkColorType:          return 4;
        case kBGRA_8888_SkColorType:          return 4;
        case kRGB_888x_SkColorType:           return 4;
        case kRGBA_1010102_SkColorType:       return 4;
        case kRGB_101010x_SkColorType:        return 4;
        case kBGRA_1010102_SkColorType:       return 4;
        case kBGR_101010x_SkColorType:        return 4;
        case kBGR_101010x_XR_SkColorType:     return 4;
        case kBGRA_10101010_XR_SkColorType:   return 8;
        case kRGBA_10x6_SkColorType:          return 8;
        case kGray_8_SkColorType:             return 1;
        case kRGBA_F16Norm_SkColorType:       return 8;
        case kRGBA_F16_SkColorType:           return 8;
        case kRGB_F16F16F16x_SkColorType:     return 8;
        case kRGBA_F32_SkColorType:           return 16;
        case kR8G8_unorm_SkColorType:         return 2;
        case kA16_unorm_SkColorType:          return 2;
        case kR16_unorm_SkColorType:          return 2;
        case kR16G16_unorm_SkColorType:       return 4;
        case kA16_float_SkColorType:          return 2;
        case kR16_float_SkColorType:          return 2;
        case kR16G16_float_SkColorType:       return 4;
        case kR16G16B16A16_unorm_SkColorType: return 8;
        case kSRGBA_8888_SkColorType:         return 4;
        case kR8_unorm_SkColorType:           return 1;
    }
    SkUNREACHABLE;
}

bool SkColorTypeIsAlwaysOpaque(SkColorType ct) {
    const uint32_t channels = SkColorTypeChannelFlags(ct);
    const bool opaque = (channels & kAlpha_SkColorChannelFlag) == 0;
    return opaque;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool SkYUVColorSpaceIsLimitedRange(SkYUVColorSpace cs) {
    switch (cs) {
        case kRec601_Limited_SkYUVColorSpace:
        case kRec709_Limited_SkYUVColorSpace:
        case kBT2020_8bit_Limited_SkYUVColorSpace:
        case kBT2020_10bit_Limited_SkYUVColorSpace:
        case kBT2020_12bit_Limited_SkYUVColorSpace:
        case kBT2020_16bit_Limited_SkYUVColorSpace:
        case kFCC_Limited_SkYUVColorSpace:
        case kSMPTE240_Limited_SkYUVColorSpace:
        case kYDZDX_Limited_SkYUVColorSpace:
        case kGBR_Limited_SkYUVColorSpace:
        case kYCgCo_8bit_Limited_SkYUVColorSpace:
        case kYCgCo_10bit_Limited_SkYUVColorSpace:
        case kYCgCo_12bit_Limited_SkYUVColorSpace:
        case kYCgCo_16bit_Limited_SkYUVColorSpace:
            return true;

        case kJPEG_Full_SkYUVColorSpace:
        case kRec709_Full_SkYUVColorSpace:
        case kBT2020_8bit_Full_SkYUVColorSpace:
        case kBT2020_10bit_Full_SkYUVColorSpace:
        case kBT2020_12bit_Full_SkYUVColorSpace:
        case kBT2020_16bit_Full_SkYUVColorSpace:
        case kFCC_Full_SkYUVColorSpace:
        case kSMPTE240_Full_SkYUVColorSpace:
        case kYDZDX_Full_SkYUVColorSpace:
        case kGBR_Full_SkYUVColorSpace:
        case kYCgCo_8bit_Full_SkYUVColorSpace:
        case kYCgCo_10bit_Full_SkYUVColorSpace:
        case kYCgCo_12bit_Full_SkYUVColorSpace:
        case kYCgCo_16bit_Full_SkYUVColorSpace:
        case kIdentity_SkYUVColorSpace:
            return false;

        default:
            SkUNREACHABLE;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////

SkColorInfo::SkColorInfo() = default;
SkColorInfo::~SkColorInfo() = default;

SkColorInfo::SkColorInfo(SkColorType ct, SkAlphaType at, sk_sp<SkColorSpace> cs)
            : fColorSpace(std::move(cs)), fColorType(ct), fAlphaType(at) {}

SkColorInfo::SkColorInfo(const SkColorInfo&) = default;
SkColorInfo::SkColorInfo(SkColorInfo&&) = default;

SkColorInfo& SkColorInfo::operator=(const SkColorInfo&) = default;
SkColorInfo& SkColorInfo::operator=(SkColorInfo&&) = default;

SkColorSpace* SkColorInfo::colorSpace() const {
    SkColorSpace* space = fColorSpace.get();
    return space;
}
sk_sp<SkColorSpace> SkColorInfo::refColorSpace() const { return fColorSpace; }

bool SkColorInfo::operator==(const SkColorInfo& other) const {
    if (fColorType != other.fColorType || fAlphaType != other.fAlphaType) return false;
    const bool equal_space = SkColorSpace::Equals(fColorSpace.get(), other.fColorSpace.get());
    return equal_space;
}

bool SkColorInfo::operator!=(const SkColorInfo& other) const {
    const bool different = !(*this == other);
    return different;
}

SkColorInfo SkColorInfo::makeAlphaType(SkAlphaType newAlphaType) const {
    SkColorInfo result = SkColorInfo(colorType(), newAlphaType, refColorSpace());
    return result;
}

SkColorInfo SkColorInfo::makeColorType(SkColorType newColorType) const {
    SkColorInfo result = SkColorInfo(newColorType, alphaType(), refColorSpace());
    return result;
}

SkColorInfo SkColorInfo::makeColorSpace(sk_sp<SkColorSpace> cs) const {
    SkColorInfo result = SkColorInfo(colorType(), alphaType(), std::move(cs));
    return result;
}

int SkColorInfo::bytesPerPixel() const {
    const int bytes = SkColorTypeBytesPerPixel(fColorType);
    return bytes;
}

bool SkColorInfo::gammaCloseToSRGB() const {
    if (!fColorSpace) return false;
    const bool close = (*fColorSpace).gammaCloseToSRGB();
    return close;
}

int SkColorInfo::shiftPerPixel() const {
    const int shift = SkColorTypeShiftPerPixel(fColorType);
    return shift;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

size_t SkImageInfo::computeOffset(int x, int y, size_t rowBytes) const {
    SkASSERTF(x >= 0 && x < width(), "x=%d; width=%d\n", x, width());
    SkASSERTF(y >= 0 && y < height(), "y=%d; height=%d\n", y, height());
    const size_t offset = SkColorTypeComputeOffset(colorType(), x, y, rowBytes);
    return offset;
}

std::optional<size_t> SkImageInfo::checkedByteSize(size_t rowBytes) const {
    if (0 == height()) {
        return 0;
    }
    SkSafeMath safe;
    const int prior_rows = safe.addInt(height(), -1);
    const size_t prior_bytes = safe.mul(static_cast<size_t>(prior_rows), rowBytes);
    const size_t final_row = safe.mul(static_cast<size_t>(width()),
                                      static_cast<size_t>(bytesPerPixel()));
    const size_t bytes = safe.add(prior_bytes, final_row);

    // The CPU backend implements some memory operations on images using instructions that take a
    // signed 32-bit offset from the base. If we ever make an image larger than that, overflow can
    // cause us to read/write memory that starts 2GB *before* the buffer. (crbug.com/1264705)
    constexpr size_t kMaxSigned32BitSize = SK_MaxS32;
    if (!safe.ok() || bytes > kMaxSigned32BitSize) return std::nullopt;
    return bytes;
}

size_t SkImageInfo::computeByteSize(size_t rowBytes) const {
    const std::optional<size_t> bytes = checkedByteSize(rowBytes);
    const size_t result = bytes.value_or(SIZE_MAX);
    return result;
}

SkColorSpace* SkImageInfo::colorSpace() const {
    SkColorSpace* space = fColorInfo.colorSpace();
    return space;
}

sk_sp<SkColorSpace> SkImageInfo::refColorSpace() const {
    sk_sp<SkColorSpace> space = fColorInfo.refColorSpace();
    return space;
}

SkImageInfo SkImageInfo::makeColorSpace(sk_sp<SkColorSpace> cs) const {
    SkColorInfo color = fColorInfo.makeColorSpace(std::move(cs));
    SkImageInfo result = Make(fDimensions, std::move(color));
    return result;
}

SkImageInfo SkImageInfo::Make(int width, int height, SkColorType ct, SkAlphaType at) {
    SkImageInfo result = Make(width, height, ct, at, nullptr);
    return result;
}

SkImageInfo SkImageInfo::Make(int width, int height, SkColorType ct, SkAlphaType at,
                              sk_sp<SkColorSpace> cs) {
    SkImageInfo result = SkImageInfo({width, height}, {ct, at, std::move(cs)});
    return result;
}

SkImageInfo SkImageInfo::Make(SkISize dimensions, SkColorType ct, SkAlphaType at) {
    SkImageInfo result = Make(dimensions, ct, at, nullptr);
    return result;
}

SkImageInfo SkImageInfo::Make(SkISize dimensions, SkColorType ct, SkAlphaType at,
                        sk_sp<SkColorSpace> cs) {
    SkImageInfo result = SkImageInfo(dimensions, {ct, at, std::move(cs)});
    return result;
}

SkImageInfo SkImageInfo::MakeN32(int width, int height, SkAlphaType at) {
    SkImageInfo result = MakeN32(width, height, at, nullptr);
    return result;
}

SkImageInfo SkImageInfo::MakeN32(int width, int height, SkAlphaType at, sk_sp<SkColorSpace> cs) {
    SkImageInfo result = Make({width, height}, kN32_SkColorType, at, std::move(cs));
    return result;
}

SkImageInfo SkImageInfo::MakeS32(int width, int height, SkAlphaType at) {
    sk_sp<SkColorSpace> space = SkColorSpace::MakeSRGB();
    SkImageInfo result = SkImageInfo({width, height}, {kN32_SkColorType, at, std::move(space)});
    return result;
}

SkImageInfo SkImageInfo::MakeN32Premul(int width, int height) {
    SkImageInfo result = MakeN32Premul(width, height, nullptr);
    return result;
}

SkImageInfo SkImageInfo::MakeN32Premul(int width, int height, sk_sp<SkColorSpace> cs) {
    SkImageInfo result = Make({width, height}, kN32_SkColorType, kPremul_SkAlphaType, std::move(cs));
    return result;
}

SkImageInfo SkImageInfo::MakeN32Premul(SkISize dimensions) {
    SkImageInfo result = MakeN32Premul(dimensions, nullptr);
    return result;
}

SkImageInfo SkImageInfo::MakeN32Premul(SkISize dimensions, sk_sp<SkColorSpace> cs) {
    SkImageInfo result = Make(dimensions, kN32_SkColorType, kPremul_SkAlphaType, std::move(cs));
    return result;
}

SkImageInfo SkImageInfo::MakeA8(int width, int height) {
    SkImageInfo result = Make({width, height}, kAlpha_8_SkColorType, kPremul_SkAlphaType, nullptr);
    return result;
}

SkImageInfo SkImageInfo::MakeA8(SkISize dimensions) {
    SkImageInfo result = Make(dimensions, kAlpha_8_SkColorType, kPremul_SkAlphaType, nullptr);
    return result;
}

SkImageInfo SkImageInfo::MakeUnknown(int width, int height) {
    SkImageInfo result = Make({width, height}, kUnknown_SkColorType, kUnknown_SkAlphaType, nullptr);
    return result;
}

#ifdef SK_DEBUG
void SkImageInfo::validate() const {
    SkASSERT(fDimensions.width() >= 0);
    SkASSERT(fDimensions.height() >= 0);
    SkASSERT(SkColorTypeIsValid(colorType()));
    SkASSERT(SkAlphaTypeIsValid(alphaType()));
}
#endif

bool SkColorTypeValidateAlphaType(SkColorType colorType, SkAlphaType alphaType,
                                  SkAlphaType* canonical) {
    switch (colorType) {
        case kUnknown_SkColorType:
            alphaType = kUnknown_SkAlphaType;
            break;
        case kAlpha_8_SkColorType:         // fall-through
        case kA16_unorm_SkColorType:       // fall-through
        case kA16_float_SkColorType:
            if (kUnpremul_SkAlphaType == alphaType) {
                alphaType = kPremul_SkAlphaType;
            }
            [[fallthrough]];
        case kARGB_4444_SkColorType:
        case kRGBA_8888_SkColorType:
        case kSRGBA_8888_SkColorType:
        case kBGRA_8888_SkColorType:
        case kRGBA_1010102_SkColorType:
        case kBGRA_1010102_SkColorType:
        case kRGBA_10x6_SkColorType:
        case kRGBA_F16Norm_SkColorType:
        case kRGBA_F16_SkColorType:
        case kRGBA_F32_SkColorType:
        case kBGRA_10101010_XR_SkColorType:
        case kR16G16B16A16_unorm_SkColorType:
            if (kUnknown_SkAlphaType == alphaType) {
                return false;
            }
            break;
        case kGray_8_SkColorType:
        case kR8G8_unorm_SkColorType:
        case kR16_unorm_SkColorType:
        case kR16_float_SkColorType:
        case kR16G16_unorm_SkColorType:
        case kR16G16_float_SkColorType:
        case kRGB_565_SkColorType:
        case kRGB_888x_SkColorType:
        case kRGB_101010x_SkColorType:
        case kBGR_101010x_SkColorType:
        case kBGR_101010x_XR_SkColorType:
        case kRGB_F16F16F16x_SkColorType:
        case kR8_unorm_SkColorType:
            alphaType = kOpaque_SkAlphaType;
            break;
        default:
            return false;
    }
    if (canonical) {
        *canonical = alphaType;
    }
    return true;
}
