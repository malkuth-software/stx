/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkAutoPixmapStorage_DEFINED
#define SkAutoPixmapStorage_DEFINED

#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "include/private/SkMalloc.h"

#include <cstddef>
#include <optional>

class SkData;
struct SkImageInfo;
struct SkMask;

class [[nodiscard]] SkAutoPixmapStorage : public SkPixmap {
public:
    SkAutoPixmapStorage();
    ~SkAutoPixmapStorage();

    SkAutoPixmapStorage(SkAutoPixmapStorage&& other);
    SkAutoPixmapStorage(const SkAutoPixmapStorage&) = delete;
    SkAutoPixmapStorage& operator=(const SkAutoPixmapStorage&) = delete;

    /**
    * Release the previous owned allocation and transfer the source's view and owner.
    * Leave the source empty. Self-move leaves the object unchanged. A borrowed
    * source view must not refer into this destination's current owned allocation.
    */
    SkAutoPixmapStorage& operator=(SkAutoPixmapStorage&& other);

    /**
    *  Try to allocate memory for the pixels needed to match the specified Info. On success
    *  return true and fill out the pixmap to point to that memory. The storage will be freed
    *  when this object is destroyed, or if another call to tryAlloc() or alloc() is made.
    *
    *  On invalid dimensions/types, size overflow or allocation failure, return
    *  false and reset the pixmap to empty. info may alias this owner's metadata.
    */
    bool tryAlloc(const SkImageInfo& info);

    /**
    *  Allocate memory for the pixels needed to match the specified Info and fill out the pixmap
    *  to point to that memory. The storage will be freed when this object is destroyed,
    *  or if another call to tryAlloc() or alloc() is made.
    *
    *  If the memory cannot be allocated, calls SK_ABORT().
    */
    void alloc(const SkImageInfo& info);

    /**
    * Gets the size and optionally the rowBytes that would be allocated by SkAutoPixmapStorage if
    * alloc/tryAlloc was called. The size has no value when it exceeds the pixel-buffer limit.
    */
    static std::optional<size_t> AllocSize(const SkImageInfo& info, size_t* rowBytes);

    /**
    * Returns a void* of the allocated pixel memory and resets the pixmap. If the storage hasn't
    * been allocated, the result is nullptr. The caller is responsible for calling sk_free to free
    * the returned memory.
    */
    [[nodiscard]] void* detachPixels();

    /**
    *  Returns an SkData object wrapping the allocated pixels memory, and resets the pixmap.
    *  If the storage hasn't been allocated, the result is nullptr.
    */
    [[nodiscard]] sk_sp<SkData> detachPixelsAsData();

    // Reset releases owned storage, then borrows the new pixels. The new view
    // must not point into the allocation being released. Do not mutate this
    // owner's view through a separate SkPixmap base alias.

    void reset() {
        freeStorage();
        INHERITED::reset();
    }
    void reset(const SkImageInfo& info, const void* addr, size_t rb) {
        freeStorage();
        INHERITED::reset(info, addr, rb);
    }

    [[nodiscard]] bool reset(const SkMask& mask) {
        freeStorage();
        const bool accepted = INHERITED::reset(mask);
        return accepted;
    }

private:
    void*   fStorage;

    void freeStorage() {
        sk_free(fStorage);
        fStorage = nullptr;
    }

    using INHERITED = SkPixmap;
};

#endif
