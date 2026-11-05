/*
 * Copyright 2008 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkWriter32_DEFINED
#define SkWriter32_DEFINED

#include "include/core/SkData.h"
#include "include/core/SkPath.h"
#include "include/core/SkPoint.h"
#include "include/core/SkPoint3.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkRegion.h"
#include "include/core/SkScalar.h"
#include "include/core/SkStream.h"
#include "include/core/SkTypes.h"
#include "include/private/SkAlign.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkNoncopyable.h"
#include "include/private/SkTemplates.h"
#include "include/private/SkTo.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <cstring>

class SkMatrix;
struct SkSamplingOptions;

// Owns internal storage and borrows optional caller-owned initial storage. The
// caller keeps external storage alive and disjoint from this object and its allocation.
// Reset discards live bytes but retains the internal allocation until reuse or
// destruction. Growing copies external live bytes once and then uses internal storage.
// Extent/offset violations and allocation failure terminate; writes have no error state.
class SkWriter32 : SkNoncopyable {
public:
    explicit SkWriter32(void* external = nullptr, size_t externalBytes = 0) {
        reset(external, externalBytes);
    }

    size_t bytesWritten() const { return fUsed; }
    bool usingInitialStorage() const {
        const bool external = fData == fExternal;
        return external;
    }

    void reset(void* external = nullptr, size_t externalBytes = 0) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(external);
        SkASSERT_RELEASE(SkIsAlign4(address));
        SkASSERT_RELEASE(external != nullptr || externalBytes == 0);
        SkASSERT_RELEASE(externalBytes <= MaximumExtent);
        fData = static_cast<uint8_t*>(external);
        fCapacity = externalBytes & ~size_t{3};
        fUsed = 0;
        fExternal = external;
    }

    // Claims uninitialized live bytes. The returned borrow lasts until growth,
    // reset or destruction; fill it before snapshot/flatten/read operations.
    uint32_t* reserve(size_t size) {
        SkASSERT_RELEASE(SkIsAlign4(size));
        SkASSERT_RELEASE(size <= MaximumExtent);
        const size_t offset = fUsed;
        // Each addend fits ptrdiff_t, so their sum fits size_t. Only growth can
        // exceed the admitted extent; fitting writes keep the original fast path.
        const size_t required = offset + size;
        if (required > fCapacity) {
            SkASSERT_RELEASE(required <= MaximumExtent);
            growToAtLeast(required);
        }
        fUsed = required;
        if (size == 0 && fData == nullptr) return nullptr;
        uint32_t* storage = reinterpret_cast<uint32_t*>(fData + offset);
        return storage;
    }

    // Returns a value, so subsequent growth/overwrite cannot invalidate the read.
    template <typename T> T readTAt(size_t offset) const {
        static_assert(std::is_trivially_copyable_v<T>);
        SkASSERT_RELEASE(SkIsAlign4(offset));
        SkASSERT_RELEASE(offset <= fUsed && sizeof(T) <= fUsed - offset);
        T value{};
        std::memcpy(&value, fData + offset, sizeof(T));
        return value;
    }

    template <typename T> void overwriteTAt(size_t offset, const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        SkASSERT_RELEASE(SkIsAlign4(offset));
        SkASSERT_RELEASE(offset <= fUsed && sizeof(T) <= fUsed - offset);
        // Overwriting may use an overlapping record from the same allocation.
        std::memmove(fData + offset, &value, sizeof(T));
    }

    bool writeBool(bool value) {
        write32(static_cast<int32_t>(value));
        return value;
    }
    void writeInt(int32_t value) { write32(value); }
    void write8(int32_t value) {
        const int32_t low = value & 0xff;
        write32(low);
    }
    void write16(int32_t value) {
        const int32_t low = value & 0xffff;
        write32(low);
    }
    // Begin the scalar record lifetime while retaining its type for alias analysis.
    void write32(int32_t value) {
        int32_t* destination = reinterpret_cast<int32_t*>(reserve(sizeof(value)));
        std::construct_at(destination, value);
    }
    void writeScalar(SkScalar value) {
        SkScalar* destination = reinterpret_cast<SkScalar*>(reserve(sizeof(value)));
        std::construct_at(destination, value);
    }
    void writePoint(const SkPoint& point) { write(&point, sizeof(point)); }
    void writePoint3(const SkPoint3& point) { write(&point, sizeof(point)); }
    void writeRect(const SkRect& rect) { write(&rect, sizeof(rect)); }
    void writeIRect(const SkIRect& rect) { write(&rect, sizeof(rect)); }
    void writeRRect(const SkRRect& rect) {
        void* destination = reserve(SkRRect::kSizeInMemory);
        rect.writeToMemory(destination);
    }
    void writePath(const SkPath& path) {
        const size_t bytes = path.writeToMemory(nullptr);
        void* destination = reserve(bytes);
        path.writeToMemory(destination);
    }
    void writeMatrix(const SkMatrix& matrix);
    void writeRegion(const SkRegion& region) {
        const size_t bytes = region.writeToMemory(nullptr);
        void* destination = reserve(bytes);
        region.writeToMemory(destination);
    }
    void writeSampling(const SkSamplingOptions& sampling);
    void writeMul4(const void* values, size_t size) { write(values, size); }

    // Source supplies size bytes, disjoint from writer storage. Size is a multiple
    // of four; source need not be aligned. A zero size permits null source/storage.
    void write(const void* values, size_t size) {
        SkASSERT_RELEASE(SkIsAlign4(size));
        SkASSERT_RELEASE(values != nullptr || size == 0);
        if (size == 0) return;
        void* destination = reserve(size);
        std::memcpy(destination, values, size);
    }

    // When padding is needed, zero the final word. The caller fills the full payload.
    uint32_t* reservePad(size_t size) {
        SkASSERT_RELEASE(size <= MaximumExtent - 3);
        const size_t aligned = SkAlign4(size);
        uint32_t* storage = reserve(aligned);
        if (aligned != size) {
            const uint32_t zero = 0;
            uint8_t* bytes = reinterpret_cast<uint8_t*>(storage);
            std::memcpy(bytes + aligned - sizeof(zero), &zero, sizeof(zero));
        }
        return storage;
    }

    void writePad(const void* source, size_t size) {
        SkASSERT_RELEASE(source != nullptr || size == 0);
        if (size == 0) return;
        void* destination = reservePad(size);
        std::memcpy(destination, source, size);
    }

    // UnknownLength requests strlen; null writes an empty string. Wire format is
    // a uint32 length, bytes, terminator and zero padding to a four-byte boundary.
    static constexpr size_t UnknownLength = std::numeric_limits<size_t>::max();
    void writeString(const char* text, size_t length = UnknownLength);
    static size_t WriteStringSize(const char* text, size_t length = UnknownLength);

    void writeData(const SkData* data) {
        size_t size = 0;
        if (data != nullptr) size = (*data).size();
        SkASSERT_RELEASE(size <= std::numeric_limits<uint32_t>::max());
        const uint32_t length = static_cast<uint32_t>(size);
        write32(length);
        if (data != nullptr) writePad((*data).data(), size);
    }
    static size_t WriteDataSize(const SkData* data) {
        size_t bytes = 0;
        if (data != nullptr) bytes = (*data).size();
        SkASSERT_RELEASE(bytes <= std::numeric_limits<uint32_t>::max());
        SkASSERT_RELEASE(bytes <= MaximumExtent - 7);
        const size_t total = sizeof(uint32_t) + SkAlign4(bytes);
        return total;
    }

    void rewindToOffset(size_t offset) {
        SkASSERT_RELEASE(SkIsAlign4(offset) && offset <= fUsed);
        fUsed = offset;
    }

    // Destination supplies fUsed writable bytes, disjoint from writer storage.
    void flatten(void* destination) const {
        if (fUsed == 0) return;
        SkASSERT_RELEASE(destination != nullptr);
        std::memcpy(destination, fData, fUsed);
    }
    bool writeToStream(SkWStream* stream) const {
        SkASSERT_RELEASE(stream != nullptr);
        const bool written = (*stream).write(fData, fUsed);
        return written;
    }
    sk_sp<SkData> snapshotAsData() const;

private:
    static constexpr size_t MaximumExtent = static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max());
    static_assert(MaximumExtent <= std::numeric_limits<size_t>::max() / 2);
    void growToAtLeast(size_t size);

    uint8_t* fData = nullptr;  // Borrows either fInternal or external storage.
    size_t fCapacity = 0;     // Writable bytes in the selected allocation.
    size_t fUsed = 0;         // Live bytes, always a multiple of four.
    void* fExternal = nullptr;
    skia_private::AutoTMalloc<uint8_t> fInternal{};
};

// Inline capacity begins its lifetime before the constructor body lends it to
// the base. The base destructor releases only internal storage, never this borrow.
template <size_t SIZE> class SkSWriter32 : public SkWriter32 {
public:
    static_assert(SIZE > 0);
    SkSWriter32() { reset(); }
    void reset() { SkWriter32::reset(fStorage, SIZE); }

private:
    // Uninitialized capacity; each live record is filled before any read.
    alignas(void*) alignas(double) uint8_t fStorage[SIZE];
};

#endif
