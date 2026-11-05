/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkWriteBuffer_DEFINED
#define SkWriteBuffer_DEFINED

#include "include/core/SkColor.h"
#include "include/core/SkData.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSerialProcs.h"
#include "include/core/SkSpan.h"
#include "src/core/SkTHash.h"
#include "src/core/SkWriter32.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

class SkFlattenable;
class SkImage;
class SkM44;
class SkMatrix;
class SkPath;
class SkRegion;
class SkWStream;
struct SkIRect;
struct SkPoint3;
struct SkPoint;
struct SkRect;

class SkWriteBuffer {
public:
    explicit SkWriteBuffer(const SkSerialProcs& procs) : fProcs(procs) {}
    virtual ~SkWriteBuffer() {}

    virtual void writePad32(const void* buffer, size_t bytes) = 0;

    virtual void writeByteArray(const void* data, size_t size) = 0;
    void writeDataAsByteArray(const SkData* data) {
        if (data == nullptr) {
            write32(0);
            return;
        }
        const void* bytes = (*data).data();
        const size_t size = (*data).size();
        writeByteArray(bytes, size);
    }

    virtual void writeBool(bool value) = 0;
    virtual void writeScalar(SkScalar value) = 0;
    virtual void writeScalarArray(SkSpan<const SkScalar>) = 0;
    virtual void writeInt(int32_t value) = 0;
    virtual void writeIntArray(SkSpan<const int32_t>) = 0;
    virtual void writeUInt(uint32_t value) = 0;
    void write32(int32_t value) {
        writeInt(value);
    }
    virtual void writeString(std::string_view value) = 0;

    virtual void writeFlattenable(const SkFlattenable* flattenable) = 0;
    virtual void writeColor(SkColor color) = 0;
    virtual void writeColorArray(SkSpan<const SkColor>) = 0;
    virtual void writeColor4f(const SkColor4f& color) = 0;
    virtual void writeColor4fArray(SkSpan<const SkColor4f>) = 0;
    virtual void writePoint(const SkPoint& point) = 0;
    virtual void writePointArray(SkSpan<const SkPoint>) = 0;
    virtual void writePoint3(const SkPoint3& point) = 0;
    virtual void write(const SkM44&) = 0;
    virtual void writeMatrix(const SkMatrix& matrix) = 0;
    virtual void writeIRect(const SkIRect& rect) = 0;
    virtual void writeRect(const SkRect& rect) = 0;
    virtual void writeRegion(const SkRegion& region) = 0;
    virtual void writeSampling(const SkSamplingOptions&) = 0;
    virtual void writePath(const SkPath& path) = 0;
    virtual void writeImage(const SkImage*) = 0;

    // The copied procedure record borrows its callback context for synchronous writes.
    const SkSerialProcs& serialProcs() const & { return fProcs; }
    const SkSerialProcs& serialProcs() const && = delete;

protected:
    SkSerialProcs fProcs{};
};

// Owns grown storage and type-name dictionary entries. Initial storage is borrowed
// and must remain writable and alive until reset/destruction. Input buffers are
// consumed synchronously and must not overlap writer storage. Stored type-name
// pointers are process-lifetime strings supplied by SkFlattenable.
// The void write interface treats invalid extents/counts as programmer errors;
// allocation failure follows SkWriter32's terminating allocation policy.
class SkBinaryWriteBuffer : public SkWriteBuffer {
public:
    explicit SkBinaryWriteBuffer(const SkSerialProcs& procs);
    SkBinaryWriteBuffer(void* initialStorage, size_t storageSize, const SkSerialProcs& procs);
    ~SkBinaryWriteBuffer() override;

    void write(const void* buffer, size_t bytes);
    void writePad32(const void* buffer, size_t bytes) override;

    // Begins an independent stream, discarding prior bytes and type-name indices.
    // SkWriter32 retains its internal allocation until reuse/destruction.
    void reset(void* storage = nullptr, size_t storageSize = 0);

    size_t bytesWritten() const {
        const size_t bytes = fWriter.bytesWritten();
        return bytes;
    }

    // Returns true iff all of the bytes written so far are stored in the initial storage
    // buffer provided in the constructor or the most recent call to reset.
    bool usingInitialStorage() const;

    void writeByteArray(const void* data, size_t size) override;
    void writeBool(bool value) override;
    void writeScalar(SkScalar value) override;
    void writeScalarArray(SkSpan<const SkScalar>) override;
    void writeInt(int32_t value) override;
    void writeIntArray(SkSpan<const int32_t>) override;
    void writeUInt(uint32_t value) override;
    void writeString(std::string_view value) override;

    void writeFlattenable(const SkFlattenable* flattenable) override;
    void writeColor(SkColor color) override;
    void writeColorArray(SkSpan<const SkColor>) override;
    void writeColor4f(const SkColor4f& color) override;
    void writeColor4fArray(SkSpan<const SkColor4f>) override;
    void writePoint(const SkPoint& point) override;
    void writePointArray(SkSpan<const SkPoint>) override;
    void writePoint3(const SkPoint3& point) override;
    void write(const SkM44&) override;
    void writeMatrix(const SkMatrix& matrix) override;
    void writeIRect(const SkIRect& rect) override;
    void writeRect(const SkRect& rect) override;
    void writeRegion(const SkRegion& region) override;
    void writeSampling(const SkSamplingOptions&) override;
    void writePath(const SkPath& path) override;
    void writeImage(const SkImage*) override;

    bool writeToStream(SkWStream*) const;
    // Destination supplies bytesWritten() disjoint writable bytes. Null is valid
    // for an empty stream. Snapshots own a copy and survive reset/destruction.
    void writeToMemory(void* destination) const;
    sk_sp<SkData> snapshotAsData() const;

private:
    SkWriter32 fWriter{};

    // Inline type names are shared within this stream.
    skia_private::THashMap<const char*, uint32_t> fFlattenableDict{};
};

enum SkWriteBufferImageFlags {
    kVersion_bits   = 8,
    kCurrVersion    = 0,

    kHasSubsetRect  = 1 << 8,
    kHasMipmap      = 1 << 9,
    kUnpremul       = 1 << 10,
};


#endif // SkWriteBuffer_DEFINED
