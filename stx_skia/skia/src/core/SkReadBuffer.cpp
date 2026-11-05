/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkReadBuffer.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageGenerator.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkM44.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPath.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkPoint3.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRegion.h"
#include "include/core/SkSize.h"
#include "include/core/SkString.h"
#include "include/private/SkMalloc.h"
#include "src/core/SkMathPriv.h"
#include "src/core/SkMatrixPriv.h"
#include "src/core/SkMipmapBuilder.h"
#include "src/core/SkSafeMath.h"
#include "src/core/SkWriteBuffer.h"

#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace {
    // This generator intentionally should always fail on all attempts to get its pixels,
    // simulating a bad or empty codec stream.
    class EmptyImageGenerator final : public SkImageGenerator {
    public:
        EmptyImageGenerator(const SkImageInfo& info) : SkImageGenerator(info) { }

    };

    static sk_sp<SkImage> MakeEmptyImage(int width, int height) {
        const SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
        std::unique_ptr<EmptyImageGenerator> generator = std::make_unique<EmptyImageGenerator>(info);
        sk_sp<SkImage> image = SkImages::DeferredFromGenerator(std::move(generator));
        return image;
    }

} // anonymous namespace

void SkReadBuffer::setMemory(const void* data, size_t size) {
    const size_t maximum = static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max());
    const bool extentValid = size <= maximum && (data != nullptr || size == 0);
    if (!validate(extentValid && IsPtrAlign4(data) && SkIsAlign4(size))) return;
    fBase = static_cast<const char*>(data);
    fCurr = fBase;
    fStop = fBase;
    if (size != 0) fStop += size;
}

void SkReadBuffer::setInvalid() {
    if (!fError) {
        // When an error is found, send the read cursor to the end of the stream
        fCurr = fStop;
        fError = true;
    }
}

const void* SkReadBuffer::skip(size_t size) {
    const size_t padded = SkAlign4(size);
    if (!validate(padded >= size && isAvailable(padded))) return nullptr;
    const void* address = fCurr;
    if (padded != 0) fCurr += padded;
    return address;
}

const void* SkReadBuffer::skip(size_t count, size_t elementSize) {
    const size_t bytes = SkSafeMath::Mul(count, elementSize);
    const void* address = skip(bytes);
    return address;
}

void SkReadBuffer::setDeserialProcs(const SkDeserialProcs& procs) {
    fProcs = procs;
}

bool SkReadBuffer::readBool() {
    const uint32_t value = readUInt();
    validate(value <= 1);
    const bool result = value != 0;
    return result;
}

SkColor SkReadBuffer::readColor() {
    const SkColor color = readUInt();
    return color;
}

int32_t SkReadBuffer::readInt() {
    // Keep the original guarded word path; byte copying does not require a typed input object.
    if (!validate(IsPtrAlign4(fCurr) && isAvailable(sizeof(int32_t)))) return 0;
    int32_t value = 0;
    std::memcpy(&value, fCurr, sizeof(value));
    fCurr += sizeof(value);
    return value;
}

SkScalar SkReadBuffer::readScalar() {
    if (!validate(IsPtrAlign4(fCurr) && isAvailable(sizeof(SkScalar)))) return 0;
    SkScalar value = 0;
    std::memcpy(&value, fCurr, sizeof(value));
    fCurr += sizeof(value);
    return value;
}

uint32_t SkReadBuffer::readUInt() {
    const int32_t value = readInt();
    const uint32_t result = static_cast<uint32_t>(value);
    return result;
}

int32_t SkReadBuffer::read32() {
    const int32_t value = readInt();
    return value;
}

uint8_t SkReadBuffer::peekByte() {
    if (!validate(isAvailable(1))) return 0;
    const uint8_t value = static_cast<uint8_t>(*fCurr);
    return value;
}

bool SkReadBuffer::readPad32(void* buffer, size_t bytes) {
    if (!validate(buffer != nullptr || bytes == 0)) return false;
    const void* source = skip(bytes);
    if (!isValid()) return false;
    if (bytes != 0) std::memcpy(buffer, source, bytes);
    return true;
}

const char* SkReadBuffer::readString(size_t* len) {
    *len = readUInt();

    // The string is len characters and a terminating \0.
    const size_t terminated = SkSafeMath::Add(*len, 1);
    const char* c_str = skipT<char>(terminated);

    if (validate(c_str && c_str[*len] == '\0')) {
        return c_str;
    }
    return nullptr;
}

void SkReadBuffer::readString(SkString* string) {
    size_t len = 0;
    if (const char* c_str = readString(&len)) {
        (*string).set(c_str, len);
        return;
    }
    (*string).reset();
}

void SkReadBuffer::readColor4f(SkColor4f* color) {
    if (!readPad32(color, sizeof(SkColor4f))) {
        *color = {0, 0, 0, 0};
    }
}

void SkReadBuffer::readPoint(SkPoint* point) {
    (*point).fX = readScalar();
    (*point).fY = readScalar();
}

void SkReadBuffer::readPoint3(SkPoint3* point) {
    readPad32(point, sizeof(SkPoint3));
}

void SkReadBuffer::read(SkM44* matrix) {
    float entries[16]{};
    if (!readPad32(entries, sizeof(entries))) {
        *matrix = SkM44();
        return;
    }
    *matrix = SkM44::ColMajor(entries);
}

void SkReadBuffer::readMatrix(SkMatrix* matrix) {
    size_t size = 0;
    if (isValid()) {
        size = SkMatrixPriv::ReadFromMemory(matrix, fCurr, available());
        (void)validate((SkAlign4(size) == size) && (0 != size));
    }
    if (!isValid()) {
        (*matrix).reset();
    }
    (void)skip(size);
}

void SkReadBuffer::readIRect(SkIRect* rect) {
    if (!readPad32(rect, sizeof(SkIRect))) {
        (*rect).setEmpty();
    }
}

void SkReadBuffer::readRect(SkRect* rect) {
    if (!readPad32(rect, sizeof(SkRect))) {
        (*rect).setEmpty();
    }
}

SkRect SkReadBuffer::readRect() {
    SkRect r{};
    if (!readPad32(&r, sizeof(SkRect))) {
        r.setEmpty();
    }
    return r;
}

SkSamplingOptions SkReadBuffer::readSampling() {
    if (!isVersionLT(SkPicturePriv::kAnisotropicFilter)) {
        int maxAniso = readInt();
        if (maxAniso != 0) {
            const SkSamplingOptions sampling = SkSamplingOptions::Aniso(maxAniso);
            return sampling;
        }
    }
    if (readBool()) {
        float B = readScalar();
        float C = readScalar();
        const SkSamplingOptions sampling(SkCubicResampler{B, C});
        return sampling;
    } else {
        SkFilterMode filter = read32LE(SkFilterMode::kLinear);
        SkMipmapMode mipmap = read32LE(SkMipmapMode::kLinear);
        const SkSamplingOptions sampling(filter, mipmap);
        return sampling;
    }
}

void SkReadBuffer::readRRect(SkRRect* rrect) {
    size_t size = 0;
    if (!fError) {
        size = (*rrect).readFromMemory(fCurr, available());
        if (!validate((SkAlign4(size) == size) && (0 != size))) {
            (*rrect).setEmpty();
        }
    }
    (void)skip(size);
}

void SkReadBuffer::readRegion(SkRegion* region) {
    size_t size = 0;
    if (!fError) {
        size = (*region).readFromMemory(fCurr, available());
        if (!validate((SkAlign4(size) == size) && (0 != size))) {
            (*region).setEmpty();
        }
    }
    (void)skip(size);
}

std::optional<SkPath> SkReadBuffer::readPath() {
    if (fError) {
        return {};
    }

    size_t size = 0;
    std::optional<SkPath> path = SkPath::ReadFromMemory(fCurr, available(), &size);

    // Path records must already include their four-byte padding.
    (void)validate(SkAlign4(size) == size && path.has_value());

    // we move forward, regardless of if the path succeeded
    (void)skip(size);

    return path;
}

bool SkReadBuffer::readArray(void* value, size_t size, size_t elementSize) {
    const uint32_t count = readUInt();
    if (!validate(size == count)) return false;
    const size_t bytes = SkSafeMath::Mul(size, elementSize);
    const bool read = readPad32(value, bytes);
    return read;
}

bool SkReadBuffer::readByteArray(void* value, size_t size) {
    const bool read = readArray(value, size, sizeof(uint8_t));
    return read;
}

bool SkReadBuffer::readColorArray(SkSpan<SkColor> colors) {
    const bool read = readArray(colors.data(), colors.size(), sizeof(SkColor));
    return read;
}

bool SkReadBuffer::readColor4fArray(SkSpan<SkColor4f> colors) {
    const bool read = readArray(colors.data(), colors.size(), sizeof(SkColor4f));
    return read;
}

bool SkReadBuffer::readIntArray(SkSpan<int32_t> values) {
    const bool read = readArray(values.data(), values.size(), sizeof(int32_t));
    return read;
}

bool SkReadBuffer::readPointArray(SkSpan<SkPoint> points) {
    const bool read = readArray(points.data(), points.size(), sizeof(SkPoint));
    return read;
}

bool SkReadBuffer::readScalarArray(SkSpan<SkScalar> values) {
    const bool read = readArray(values.data(), values.size(), sizeof(SkScalar));
    return read;
}

const void* SkReadBuffer::skipByteArray(size_t* size) {
    const uint32_t count = readUInt();
    const void* buf = skip(count);
    if (size) {
        *size = isValid() ? count : 0;
    }
    return buf;
}

sk_sp<SkData> SkReadBuffer::readByteArrayAsData() {
    const size_t bytes = readUInt();
    const void* source = skip(bytes);
    if (!isValid()) return nullptr;
    // Validate the complete record before allocating its independently owned copy.
    sk_sp<SkData> data = SkData::MakeWithCopy(source, bytes);
    return data;
}

uint32_t SkReadBuffer::getArrayCount() {
    if (!validate(IsPtrAlign4(fCurr) && isAvailable(sizeof(uint32_t)))) return 0;
    uint32_t count = 0;
    std::memcpy(&count, fCurr, sizeof(count));
    return count;
}

static sk_sp<SkImage> deserialize_image(sk_sp<SkData> data, SkDeserialProcs dProcs,
                                        std::optional<SkAlphaType> alphaType) {
    sk_sp<SkImage> image{};
    if (dProcs.fImageDataProc) {
        image = dProcs.fImageDataProc(data, alphaType, dProcs.fImageCtx);
    } else if (dProcs.fImageProc) {
        const void* bytes = (*data).data();
        const size_t size = (*data).size();
#if !defined(SK_LEGACY_DESERIAL_IMAGE_PROC)
        image = dProcs.fImageProc(bytes, size, dProcs.fImageCtx);
#else
        image = dProcs.fImageProc(bytes, size, alphaType, dProcs.fImageCtx);
#endif
    }
    return image;
}

static sk_sp<SkImage> add_mipmaps(sk_sp<SkImage> img, sk_sp<SkData> data,
                                  SkDeserialProcs dProcs, std::optional<SkAlphaType> alphaType) {
    const SkImageInfo info = (*img).imageInfo();
    SkMipmapBuilder builder(info);

    const void* bytes = (*data).data();
    const size_t byteCount = (*data).size();
    SkReadBuffer buffer(bytes, byteCount);
    int count = buffer.read32();
    if (builder.countLevels() != count) {
        return img;
    }
    for (int i = 0; i < count; ++i) {
        const int32_t recordedSize = buffer.read32();
        const size_t size = static_cast<size_t>(recordedSize);
        const void* ptr = buffer.skip(size);
        if (!ptr) {
            return img;
        }
        // The callback contract requires copying bytes retained beyond this call.
        // This borrowed level and its local image are consumed while data is alive.
        sk_sp<SkData> borrowed = SkData::MakeWithoutCopy(ptr, size);
        sk_sp<SkImage> mip = deserialize_image(std::move(borrowed), dProcs, alphaType);
        if (!mip) {
            return img;
        }

        SkPixmap pm = builder.level(i);
        if ((*mip).dimensions() != pm.dimensions()) {
            return img;
        }
        if (!(*mip).readPixels(nullptr, pm, 0, 0)) {
            return img;
        }
    }
    if (!buffer.isValid()) {
        return img;
    }
    sk_sp<SkImage> raster = (*img).makeRasterImage(nullptr);
    if (!raster) {
        return img;
    }
    sk_sp<SkImage> rasterWithMips = builder.attachTo(raster);
    SkASSERT(rasterWithMips); // attachTo should never return null
    return rasterWithMips;
}


// Invalid byte-array records fail. A missing callback image produces a non-drawing placeholder.
sk_sp<SkImage> SkReadBuffer::readImage() {
    const uint32_t flags = readUInt();

    std::optional<SkAlphaType> alphaType = std::nullopt;
    if (flags & SkWriteBufferImageFlags::kUnpremul) {
        alphaType = kUnpremul_SkAlphaType;
    }
    sk_sp<SkImage> image{};
    {
        sk_sp<SkData> data = readByteArrayAsData();
        if (!data) {
            validate(false);
            return nullptr;
        }
        image = deserialize_image(data, fProcs, alphaType);
    }

    // This flag is not written by new SKPs anymore.
    if (flags & SkWriteBufferImageFlags::kHasSubsetRect) {
        SkIRect subset{};
        readIRect(&subset);
        if (image) {
            image = (*image).makeSubset(nullptr, subset, {});
        }
    }

    if (flags & SkWriteBufferImageFlags::kHasMipmap) {
        sk_sp<SkData> data = readByteArrayAsData();
        if (!data) {
            validate(false);
            return nullptr;
        }
        if (image) {
            image = add_mipmaps(image, std::move(data), fProcs, alphaType);
        }
    }
    if (!image) image = MakeEmptyImage(1, 1);
    return image;
}



SkFlattenable* SkReadBuffer::readRawFlattenable() {
    if (!isValid()) {
        return nullptr;
    }
    // The writer's null marker is a zero word. Named effects and picture factory
    // indices are unsupported; rejecting them needs no name table or factory array.
    const uint32_t token = readUInt();
    validate(token == 0);
    return nullptr;
}

SkFlattenable* SkReadBuffer::readFlattenable(SkFlattenable::Type) {
    SkFlattenable* effect = readRawFlattenable();
    return effect;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

int32_t SkReadBuffer::checkInt(int32_t minimum, int32_t maximum) {
    SkASSERT(minimum <= maximum);
    int32_t value = read32();
    if (value < minimum || value > maximum) {
        validate(false);
        value = minimum;
    }
    return value;
}

SkLegacyFQ SkReadBuffer::checkFilterQuality() {
    const SkLegacyFQ quality = checkRange<SkLegacyFQ>(kNone_SkLegacyFQ, kLast_SkLegacyFQ);
    return quality;
}
