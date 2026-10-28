/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkImageGenerator_DEFINED
#define SkImageGenerator_DEFINED

#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "include/private/SkAPI.h"

#include <cstddef>
#include <cstdint>

class SkRecorder;

// A deferred CPU pixel provider. DeferredFromGenerator transfers ownership to
// the resulting images. Repeated generation for the same requested format must
// reproduce identical pixels.
// Deferred images serialize calls through their shared generator owner. Direct
// callers must supply their own synchronization and keep the generator alive.
class SK_API SkImageGenerator {
public:
    virtual ~SkImageGenerator() {}
    uint32_t uniqueID() const { return fUniqueID; }

    // Optional original encoded bytes, owned by the returned handle. The raster
    // library does not decode them; a provider may expose them for its caller.
    sk_sp<const SkData> refEncodedData() {
        sk_sp<const SkData> data = onRefEncodedData();
        return data;
    }
    const SkImageInfo& getInfo() const { return fInfo; }
    bool isValid(SkRecorder* recorder) const {
        const bool valid = onIsValid(recorder);
        return valid;
    }
    // The caller supplies writable pixels with the requested format/stride and
    // at least info.computeByteSize(rowBytes) bytes. Storage is borrowed for this
    // call only. Invalid shapes, formats, strides and overflowing byte extents are
    // rejected before calling the provider. A provider may reject conversions or
    // scaled dimensions. Failure
    // can leave destination pixels partially written; no valid image is published.
    bool getPixels(const SkImageInfo& info, void* pixels, size_t rowBytes);
    bool getPixels(const SkPixmap& pixels) {
        const bool generated = getPixels(pixels.info(), pixels.writable_addr(), pixels.rowBytes());
        return generated;
    }

protected:
    static constexpr int kNeedNewImageUniqueID = 0;
    SkImageGenerator(const SkImageInfo& info, uint32_t uniqueId = kNeedNewImageUniqueID);
    virtual sk_sp<const SkData> onRefEncodedData() { return nullptr; }
    struct Options {};
    virtual bool onGetPixels(const SkImageInfo&, void*, size_t, const Options&) { return false; }
    virtual bool onIsValid(SkRecorder*) const { return true; }
    const SkImageInfo fInfo;

private:
    const uint32_t fUniqueID;
    SkImageGenerator(SkImageGenerator&&) = delete;
    SkImageGenerator(const SkImageGenerator&) = delete;
    SkImageGenerator& operator=(SkImageGenerator&&) = delete;
    SkImageGenerator& operator=(const SkImageGenerator&) = delete;
};

#endif
