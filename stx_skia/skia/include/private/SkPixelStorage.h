/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkPixelStorage_DEFINED
#define SkPixelStorage_DEFINED

#include <cstdint>
#include <atomic>
#include "include/core/SkRefCnt.h"

/**
 * SkPixelStorage is the class representing the storage block for pixel data. It serves as the
 * abstract source of pixel memory—the 2D array of data—that is read from and written to by
 * higher-level Skia objects like SkImage and SkSurface.
 */
class SkPixelStorage : public SkRefCnt {
public:
    enum Type {
        kTextureProxy,
        kPixelRef,
    };

    explicit SkPixelStorage(Type type);
    ~SkPixelStorage() override = default;

    uint32_t getPixelStorageId() const { return fID; }
    Type type() const { return fType; }

    uint32_t getContentId() const {
        const uint32_t content = fContentID.load(std::memory_order_relaxed);
        return content;
    }
    uint32_t incrementContentId() {
        const uint32_t previous = fContentID.fetch_add(1, std::memory_order_relaxed);
        const uint32_t content = previous + 1;
        return content;
    }

private:
    Type fType;
    /**
     * fID is a static identifier that differentiates PixelStorages. This is assigned at
     * construction and does not change.
     */
    uint32_t fID;
    /**
     * fContentID is an identifier that tracks the current state of a PixelStorage. Gets incremented
     * and read from other objects that want to track the state of a PixelStorage at a given time.
     */
    std::atomic<uint32_t> fContentID = 0;

    static uint32_t NextId();
};

#endif
