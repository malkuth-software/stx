/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkReadBuffer_DEFINED
#define SkReadBuffer_DEFINED

#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkFlattenable.h"
#include "include/core/SkFourByteTag.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSerialProcs.h"
#include "include/core/SkShader.h"
#include "include/core/SkSpan.h"
#include "include/private/SkAlign.h"
#include "include/private/SkAssert.h"
#include "src/core/SkBlenderBase.h"
#include "src/core/SkMaskFilterBase.h"
#include "src/core/SkPicturePriv.h"
#include "src/core/SkSamplingPriv.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"
#include "src/shaders/SkShaderBase.h"

#include <cstddef>
#include <cstdint>
#include <optional>

class SkBlender;
class SkData;
class SkImage;
class SkM44;
class SkMaskFilter;
class SkMatrix;
class SkPath;
class SkRRect;
class SkRegion;
class SkString;
struct SkPoint3;

// Borrows aligned immutable input for the reader's lifetime. Callers keep that
// allocation alive and unchanged; setMemory replaces the borrow, not the storage.
// Failure is sticky and moves the cursor to the end. No reader method owns input.
class SkReadBuffer {
public:
    SkReadBuffer() = default;
    SkReadBuffer(const void* data, size_t size) {
        setMemory(data, size);
    }

    // Null is admitted only for an empty extent. Invalid replacement keeps the
    // existing extent and invalidates the reader; it does not clear an old error.
    void setMemory(const void* data, size_t size);

    bool isVersionLT(SkPicturePriv::Version targetVersion) const {
        SkASSERT(targetVersion > 0);
        const bool older = fVersion > 0 && fVersion < targetVersion;
        return older;
    }

    uint32_t getVersion() const {
        const uint32_t version = static_cast<uint32_t>(fVersion);
        return version;
    }

    // Version is selected once, before reading version-dependent records.
    void setVersion(int version) {
        SkASSERT(fVersion == 0 || version == fVersion);
        fVersion = version;
    }

    size_t size() const {
        if (fBase == nullptr) return 0;
        const size_t bytes = static_cast<size_t>(fStop - fBase);
        return bytes;
    }
    size_t offset() const {
        if (fBase == nullptr) return 0;
        const size_t bytes = static_cast<size_t>(fCurr - fBase);
        return bytes;
    }
    bool eof() const {
        const bool atEnd = fCurr == fStop;
        return atEnd;
    }
    size_t available() const {
        if (fCurr == nullptr) return 0;
        const size_t bytes = static_cast<size_t>(fStop - fCurr);
        return bytes;
    }

    // Advances over four-byte padding; returns a borrow of the unpadded bytes.
    // A valid null empty input can return null for skip(0); isValid disambiguates.
    const void* skip(size_t size);
    const void* skip(size_t count, size_t elementSize);

    // Typed access requires suitably aligned T objects in the caller's input.
    template <typename T> const T* skipT() {
        const void* bytes = skip(sizeof(T));
        const T* values = static_cast<const T*>(bytes);
        return values;
    }
    template <typename T> const T* skipT(size_t count) {
        const void* bytes = skip(count, sizeof(T));
        const T* values = static_cast<const T*>(bytes);
        return values;
    }

    bool readBool();
    SkColor readColor();
    int32_t readInt();
    SkScalar readScalar();
    uint32_t readUInt();
    int32_t read32();

    template <typename T> T read32LE(T maximum) {
        uint32_t value = readUInt();
        const uint32_t limit = static_cast<uint32_t>(maximum);
        if (!validate(value <= limit)) value = 0;
        const T result = static_cast<T>(value);
        return result;
    }

    uint8_t peekByte();
    void readString(SkString* string);
    void readColor4f(SkColor4f* color);
    void readPoint(SkPoint* point);
    SkPoint readPoint() {
        SkPoint point{};
        readPoint(&point);
        return point;
    }
    void readPoint3(SkPoint3* point);
    void read(SkM44* matrix);
    void readMatrix(SkMatrix* matrix);
    void readIRect(SkIRect* rect);
    void readRect(SkRect* rect);
    SkRect readRect();
    void readRRect(SkRRect* rrect);
    void readRegion(SkRegion* region);
    std::optional<SkPath> readPath();

    // Only the null-effect marker is admitted. Any other token invalidates the reader.
    SkFlattenable* readRawFlattenable();
    SkFlattenable* readFlattenable(SkFlattenable::Type);
    template <typename T> sk_sp<T> readFlattenable() {
        const SkFlattenable::Type type = T::GetFlattenableType();
        SkFlattenable* raw = readFlattenable(type);
        sk_sp<T> effect(static_cast<T*>(raw));
        return effect;
    }
    sk_sp<SkColorFilter> readColorFilter() {
        sk_sp<SkColorFilter> effect = readFlattenable<SkColorFilterBase>();
        return effect;
    }
    sk_sp<SkBlender> readBlender() {
        sk_sp<SkBlender> effect = readFlattenable<SkBlenderBase>();
        return effect;
    }
    sk_sp<SkMaskFilter> readMaskFilter() {
        sk_sp<SkMaskFilter> effect = readFlattenable<SkMaskFilterBase>();
        return effect;
    }
    sk_sp<SkPathEffect> readPathEffect() {
        sk_sp<SkPathEffect> effect = readFlattenable<SkPathEffect>();
        return effect;
    }
    sk_sp<SkShader> readShader() {
        sk_sp<SkShader> effect = readFlattenable<SkShaderBase>();
        return effect;
    }

    // Destination has bytes writable bytes, disjoint from input. Zero bytes may
    // use a null destination. A failed bounded read leaves destination unchanged.
    bool readPad32(void* buffer, size_t bytes);
    bool readByteArray(void* value, size_t size);
    bool readColorArray(SkSpan<SkColor> colors);
    bool readColor4fArray(SkSpan<SkColor4f> colors);
    bool readIntArray(SkSpan<int32_t> values);
    bool readPointArray(SkSpan<SkPoint> points);
    bool readScalarArray(SkSpan<SkScalar> values);

    // Input borrow; size receives zero on failure when supplied.
    const void* skipByteArray(size_t* size);
    sk_sp<SkData> readByteArrayAsData();
    uint32_t getArrayCount();

    // Returns decoded callback output or a non-drawing placeholder when no image
    // is produced. Invalid byte-array records return an empty owner.
    sk_sp<SkImage> readImage();

    // Copies function/context addresses. Context stays borrowed until the last
    // readImage call; callback output owns or copies any bytes it retains.
    void setDeserialProcs(const SkDeserialProcs& procs);
    const SkDeserialProcs& getDeserialProcs() const & { return fProcs; }
    const SkDeserialProcs& getDeserialProcs() const && = delete;

    bool validate(bool condition) {
        if (!condition) setInvalid();
        const bool valid = !fError;
        return valid;
    }

    template <typename T> bool validateCanReadN(size_t count) {
        const size_t capacity = available() / sizeof(T);
        const bool valid = validate(count <= capacity);
        return valid;
    }
    bool isValid() const {
        const bool valid = !fError;
        return valid;
    }
    bool validateIndex(int index, int count) {
        const bool valid = validate(index >= 0 && index < count);
        return valid;
    }

    // Out-of-range values invalidate the reader and return minimum.
    int32_t checkInt(int32_t minimum, int32_t maximum);
    template <typename T> T checkRange(T minimum, T maximum) {
        const int32_t low = static_cast<int32_t>(minimum);
        const int32_t high = static_cast<int32_t>(maximum);
        const int32_t checked = checkInt(low, high);
        const T result = static_cast<T>(checked);
        return result;
    }
    SkLegacyFQ checkFilterQuality();
    SkSamplingOptions readSampling();

private:
    const char* readString(size_t* length);
    void setInvalid();
    bool readArray(void* value, size_t size, size_t elementSize);
    bool isAvailable(size_t bytes) const {
        const bool fits = bytes <= available();
        return fits;
    }

    // All three point into one four-byte-aligned extent, or are all null.
    const char* fCurr = nullptr;
    const char* fStop = nullptr;
    const char* fBase = nullptr;
    int fVersion = 0;
    SkDeserialProcs fProcs{};

    static bool IsPtrAlign4(const void* pointer) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
        const bool aligned = SkIsAlign4(address);
        return aligned;
    }

    bool fError = false;
};

#endif // SkReadBuffer_DEFINED
