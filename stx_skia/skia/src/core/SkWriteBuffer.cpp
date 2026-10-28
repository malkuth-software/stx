/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkWriteBuffer.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkData.h"
#include "include/core/SkFlattenable.h"
#include "include/core/SkImage.h"
#include "include/core/SkPoint.h"
#include "include/core/SkPoint3.h"
#include "include/core/SkRect.h"
#include "include/private/SkAssert.h"
#include "include/private/SkTFitsIn.h"
#include "src/core/SkSafeMath.h"
#include "src/core/SkMatrixPriv.h"
#include "src/core/SkMipmap.h"
#include "src/image/SkImage_Base.h"

#include <cstring>
#include <limits>
#include <utility>

class SkMatrix;
class SkRegion;

///////////////////////////////////////////////////////////////////////////////////////////////////

SkBinaryWriteBuffer::SkBinaryWriteBuffer(const SkSerialProcs& procs)
        : SkWriteBuffer(procs) {}

SkBinaryWriteBuffer::SkBinaryWriteBuffer(void* storage, size_t storageSize, const SkSerialProcs& procs)
        : SkWriteBuffer(procs) {
    reset(storage, storageSize);
}

SkBinaryWriteBuffer::~SkBinaryWriteBuffer() {}

bool SkBinaryWriteBuffer::usingInitialStorage() const {
    const bool initial = fWriter.usingInitialStorage();
    return initial;
}

void SkBinaryWriteBuffer::reset(void* storage, size_t storageSize) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(storage);
    SkASSERT_RELEASE(SkIsAlign4(address));
    SkASSERT_RELEASE(storage != nullptr || storageSize == 0);
    fWriter.reset(storage, storageSize);
    fFlattenableDict.reset();
}

void SkBinaryWriteBuffer::write(const void* buffer, size_t bytes) {
    SkASSERT_RELEASE(SkIsAlign4(bytes));
    SkASSERT_RELEASE(buffer != nullptr || bytes == 0);
    if (bytes != 0) fWriter.write(buffer, bytes);
}

void SkBinaryWriteBuffer::writePad32(const void* buffer, size_t bytes) {
    SkASSERT_RELEASE(buffer != nullptr || bytes == 0);
    SkASSERT_RELEASE(bytes <= std::numeric_limits<size_t>::max() - 3);
    if (bytes != 0) fWriter.writePad(buffer, bytes);
}

void SkBinaryWriteBuffer::writeToMemory(void* destination) const {
    const size_t bytes = fWriter.bytesWritten();
    if (bytes == 0) return;
    SkASSERT_RELEASE(destination != nullptr);
    fWriter.flatten(destination);
}

sk_sp<SkData> SkBinaryWriteBuffer::snapshotAsData() const {
    if (fWriter.bytesWritten() == 0) {
        sk_sp<SkData> empty = SkData::MakeEmpty();
        return empty;
    }
    sk_sp<SkData> snapshot = fWriter.snapshotAsData();
    return snapshot;
}

namespace {
// All typed arrays use the same count prefix and raw four-byte-aligned payload.
void write_array(SkWriter32& writer, const void* values, size_t count, size_t elementSize) {
    SkASSERT_RELEASE(count <= static_cast<size_t>(std::numeric_limits<int32_t>::max()));
    SkASSERT_RELEASE(values != nullptr || count == 0);
    SkSafeMath safe{};
    const size_t bytes = safe.mul(count, elementSize);
    SkASSERT_RELEASE(safe.ok());
    const uint32_t encodedCount = static_cast<uint32_t>(count);
    writer.write32(encodedCount);
    writer.write(values, bytes);
}
}

void SkBinaryWriteBuffer::writeByteArray(const void* data, size_t size) {
    SkASSERT_RELEASE(data != nullptr || size == 0);
    SkASSERT_RELEASE(size <= std::numeric_limits<uint32_t>::max());
    SkASSERT_RELEASE(size <= std::numeric_limits<size_t>::max() - 3);
    const uint32_t count = static_cast<uint32_t>(size);
    fWriter.write32(count);
    fWriter.writePad(data, size);
}

void SkBinaryWriteBuffer::writeBool(bool value) {
    fWriter.writeBool(value);
}

void SkBinaryWriteBuffer::writeScalar(SkScalar value) {
    fWriter.writeScalar(value);
}

void SkBinaryWriteBuffer::writeScalarArray(SkSpan<const SkScalar> values) {
    write_array(fWriter, values.data(), values.size(), sizeof(SkScalar));
}

void SkBinaryWriteBuffer::writeInt(int32_t value) {
    fWriter.write32(value);
}

void SkBinaryWriteBuffer::writeIntArray(SkSpan<const int32_t> values) {
    write_array(fWriter, values.data(), values.size(), sizeof(int32_t));
}

void SkBinaryWriteBuffer::writeUInt(uint32_t value) {
    fWriter.write32(value);
}

void SkBinaryWriteBuffer::writeString(std::string_view value) {
    const size_t bytes = value.size();
    SkASSERT_RELEASE(bytes <= std::numeric_limits<uint32_t>::max());
    SkASSERT_RELEASE(bytes <= std::numeric_limits<size_t>::max() - 8);
    fWriter.writeString(value.data(), bytes);
}

void SkBinaryWriteBuffer::writeColor(SkColor color) {
    fWriter.write32(color);
}

void SkBinaryWriteBuffer::writeColorArray(SkSpan<const SkColor> values) {
    write_array(fWriter, values.data(), values.size(), sizeof(SkColor));
}

void SkBinaryWriteBuffer::writeColor4f(const SkColor4f& color) {
    fWriter.write(&color, sizeof(SkColor4f));
}

void SkBinaryWriteBuffer::writeColor4fArray(SkSpan<const SkColor4f> values) {
    write_array(fWriter, values.data(), values.size(), sizeof(SkColor4f));
}

void SkBinaryWriteBuffer::writePoint(const SkPoint& point) {
    fWriter.writeScalar(point.fX);
    fWriter.writeScalar(point.fY);
}

void SkBinaryWriteBuffer::writePoint3(const SkPoint3& point) {
    writePad32(&point, sizeof(SkPoint3));
}

void SkBinaryWriteBuffer::writePointArray(SkSpan<const SkPoint> values) {
    write_array(fWriter, values.data(), values.size(), sizeof(SkPoint));
}

void SkBinaryWriteBuffer::write(const SkM44& matrix) {
    const float* entries = SkMatrixPriv::M44ColMajor(matrix);
    fWriter.write(entries, sizeof(float) * 16);
}

void SkBinaryWriteBuffer::writeMatrix(const SkMatrix& matrix) {
    fWriter.writeMatrix(matrix);
}

void SkBinaryWriteBuffer::writeIRect(const SkIRect& rect) {
    fWriter.write(&rect, sizeof(SkIRect));
}

void SkBinaryWriteBuffer::writeRect(const SkRect& rect) {
    fWriter.writeRect(rect);
}

void SkBinaryWriteBuffer::writeRegion(const SkRegion& region) {
    fWriter.writeRegion(region);
}

void SkBinaryWriteBuffer::writeSampling(const SkSamplingOptions& sampling) {
    fWriter.writeSampling(sampling);
}

void SkBinaryWriteBuffer::writePath(const SkPath& path) {
    fWriter.writePath(path);
}

bool SkBinaryWriteBuffer::writeToStream(SkWStream* stream) const {
    SkASSERT_RELEASE(stream != nullptr);
    const bool written = fWriter.writeToStream(stream);
    return written;
}

static sk_sp<const SkData> serialize_image(const SkImage* image, SkSerialProcs procs) {
    sk_sp<const SkData> data{};
    if (procs.fImageProc) {
        data = procs.fImageProc(const_cast<SkImage*>(image), procs.fImageCtx);
    }
    if (data) {
        return data;
    }
    // Check to see if the image's source was an encoded block of data.
    // If so, just use that.
    data = (*image).refEncodedData();
    if (data) {
        return data;
    }
    return nullptr;
}

static sk_sp<SkData> serialize_mipmap(const SkMipmap* mipmap, SkSerialProcs procs) {
    /*  Format
        count_levels:32
        for each level, starting with the biggest (index 0 in our iterator)
            encoded_size:32
            encoded_data (padded)
    */
    const int count = (*mipmap).countLevels();

    // This buffer does not need procs because it is just writing SkDatas
    const SkSerialProcs plain{};
    SkBinaryWriteBuffer buffer(plain);
    buffer.write32(count);
    for (int i = 0; i < count; ++i) {
        SkMipmap::Level level{};
        if ((*mipmap).getLevel(i, &level)) {
            sk_sp<SkImage> levelImage = SkImages::RasterFromPixmap(level.fPixmap, nullptr, nullptr);
            if (!levelImage) return nullptr;
            sk_sp<const SkData> levelData = serialize_image(levelImage.get(), procs);
            buffer.writeDataAsByteArray(levelData.get());
        } else {
            return nullptr;
        }
    }
    sk_sp<SkData> snapshot = buffer.snapshotAsData();
    return snapshot;
}

/*  Format:
 *      flags: U32
 *      encoded : size_32 + data[]
 *      [subset: IRect]
 *      [mips]  : size_32 + data[]
 */
void SkBinaryWriteBuffer::writeImage(const SkImage* image) {
    uint32_t flags = 0;
    SkASSERT_RELEASE(image != nullptr);
    const SkImage_Base* base = as_IB(image);
    const SkMipmap* mips = (*base).onPeekMips();
    if (mips) {
        flags |= SkWriteBufferImageFlags::kHasMipmap;
    }
    if ((*image).alphaType() == kUnpremul_SkAlphaType) {
        flags |= SkWriteBufferImageFlags::kUnpremul;
    }

    write32(flags);

    sk_sp<const SkData> data = serialize_image(image, fProcs);
    SkASSERT(data);
    writeDataAsByteArray(data.get());

    if (flags & SkWriteBufferImageFlags::kHasMipmap) {
        sk_sp<SkData> mipData = serialize_mipmap(mips, fProcs);
        writeDataAsByteArray(mipData.get());
    }
}



void SkBinaryWriteBuffer::writeFlattenable(const SkFlattenable* flattenable) {
    if (nullptr == flattenable) {
        write32(0);
        return;
    }

    // Ordinary streams carry inline type names, then dictionary indices on reuse.
    const char* name = (*flattenable).getTypeName();
    SkASSERT_RELEASE(name != nullptr && name[0] != '\0');

    if (uint32_t* indexPtr = fFlattenableDict.find(name)) {
        // We will write the index as a 32-bit int.  We want the first byte
        // that we send to be zero - this will act as a sentinel that we
        // have an index (not a string).  This means that we will send the
        // the index shifted left by 8.  The remaining 24-bits should be
        // plenty to store the index.  Note that this strategy depends on
        // being little endian, and type names being non-empty.
        SkASSERT_RELEASE(*indexPtr <= 0x00ffffffU);
        const uint32_t token = *indexPtr << 8;
        writeUInt(token);
    } else {
        const int count = fFlattenableDict.count();
        SkASSERT_RELEASE(count < 0x00ffffff);
        writeString(name);
        const uint32_t index = static_cast<uint32_t>(count) + 1;
        fFlattenableDict.set(name, index);
    }

    // make room for the size of the flattened object
    (void)fWriter.reserve(sizeof(uint32_t));
    // record the current size, so we can subtract after the object writes.
    const size_t offset = fWriter.bytesWritten();
    // now flatten the object
    (*flattenable).flatten(*this);
    const size_t objSize = fWriter.bytesWritten() - offset;
    SkASSERT_RELEASE(objSize <= std::numeric_limits<uint32_t>::max());
    const uint32_t encodedSize = static_cast<uint32_t>(objSize);
    // record the obj's size
    fWriter.overwriteTAt(offset - sizeof(uint32_t), encodedSize);
}
