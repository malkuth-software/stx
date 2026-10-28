/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkMask_DEFINED
#define SkMask_DEFINED

#include "include/core/SkRect.h"
#include "include/private/SkAssert.h"
#include "src/core/SkColorData.h"
#include "src/core/SkColorPriv.h"

#include <cstddef>
#include <cstdint>
#include <memory>

/** \class SkMask
    SkMask is used to describe alpha bitmaps, either 1bit, 8bit, or
    the 3-plane 3D format. These are passed to SkMaskFilter objects.
    Pixels are borrowed for the descriptor's lifetime. Callers establish valid
    bounds, row strides, allocation extents and natural typed alignment before
    address/iterator access. Bounds use image coordinates, not byte offsets.
    Iterators compare/traverse within one allocation; row advances retain typed
    alignment. Descriptors and pixel edits require external synchronization.
*/
struct SkMask {
    enum Format : uint8_t {
        kBW_Format, //!< 1bit per pixel mask (e.g. monochrome)
        kA8_Format, //!< 8bits per pixel mask (e.g. antialiasing)
        k3D_Format, //!< 3 8bit per pixl planes: alpha, mul, add
        kARGB32_Format,         //!< SkPMColor
        kLCD16_Format,          //!< 565 alpha for r/g/b
        kSDF_Format,            //!< 8bits representing signed distance field
    };

    enum {
        kCountMaskFormats = kSDF_Format + 1
    };

    SkMask(const uint8_t* img, const SkIRect& bounds, uint32_t rowBytes, Format format)
        : fImage(img), fBounds(bounds), fRowBytes(rowBytes), fFormat(format) {}
    // This descriptor borrows pixels. Its metadata is ordinary mutable data;
    // observation through const SkMask& keeps the descriptor read-only. A builder
    // additionally requires writable backing storage, owned separately by its caller.
    const uint8_t* fImage;
    SkIRect fBounds;
    uint32_t fRowBytes;
    Format fFormat;

    static bool IsValidFormat(uint8_t format) {
        const bool valid = format < kCountMaskFormats;
        return valid;
    }

    /** Returns true if the mask is empty: i.e. it has an empty bounds.
     */
    bool isEmpty() const {
        const bool empty = fBounds.isEmpty();
        return empty;
    }

    /** Return the byte size of the mask, assuming only 1 plane.
        Does not account for k3D_Format. For that, use computeTotalImageSize().
        Returns zero for a nonpositive height or a size exceeding INT32_MAX.
    */
    size_t computeImageSize() const;

    /** Return the byte size of the mask, taking into account
        any extra planes (e.g. k3D_Format).
        Returns zero for a nonpositive height or a size exceeding INT32_MAX.
    */
    size_t computeTotalImageSize() const;

    /** Returns the address of the byte that holds the specified bit.
        Asserts that the mask is kBW_Format, and that x,y are in range.
        x,y are in the same coordiate space as fBounds.
    */
    const uint8_t* getAddr1(int x, int y) const {
        SkASSERT(kBW_Format == fFormat);
        SkASSERT(fBounds.contains(x, y));
        SkASSERT(fImage != nullptr);
        const size_t column = static_cast<size_t>(static_cast<int64_t>(x) - fBounds.fLeft) >> 3;
        const size_t row = static_cast<size_t>(static_cast<int64_t>(y) - fBounds.fTop) * fRowBytes;
        const uint8_t* address = fImage + row + column;
        return address;
    }

    /** Returns the address of the specified byte.
        Asserts that the mask is kA8_Format, and that x,y are in range.
        x,y are in the same coordiate space as fBounds.
    */
    const uint8_t* getAddr8(int x, int y) const {
        SkASSERT(kA8_Format == fFormat || kSDF_Format == fFormat);
        SkASSERT(fBounds.contains(x, y));
        SkASSERT(fImage != nullptr);
        const size_t column = static_cast<size_t>(static_cast<int64_t>(x) - fBounds.fLeft);
        const size_t row = static_cast<size_t>(static_cast<int64_t>(y) - fBounds.fTop) * fRowBytes;
        const uint8_t* address = fImage + row + column;
        return address;
    }

    /**
     *  Return the address of the specified 16bit mask. In the debug build,
     *  this asserts that the mask's format is kLCD16_Format, and that (x,y)
     *  are contained in the mask's fBounds.
     */
    const uint16_t* getAddrLCD16(int x, int y) const {
        SkASSERT(kLCD16_Format == fFormat);
        SkASSERT(fBounds.contains(x, y));
        SkASSERT(fImage != nullptr);
        const size_t row_offset = static_cast<size_t>(static_cast<int64_t>(y) - fBounds.fTop) * fRowBytes;
        const size_t column = static_cast<size_t>(static_cast<int64_t>(x) - fBounds.fLeft);
        const uint16_t* row = reinterpret_cast<const uint16_t*>(fImage + row_offset);
        const uint16_t* address = row + column;
        return address;
    }

    /**
     *  Return the address of the specified 32bit mask. In the debug build,
     *  this asserts that the mask's format is 32bits, and that (x,y)
     *  are contained in the mask's fBounds.
     */
    const uint32_t* getAddr32(int x, int y) const {
        SkASSERT(kARGB32_Format == fFormat);
        SkASSERT(fBounds.contains(x, y));
        SkASSERT(fImage != nullptr);
        const size_t row_offset = static_cast<size_t>(static_cast<int64_t>(y) - fBounds.fTop) * fRowBytes;
        const size_t column = static_cast<size_t>(static_cast<int64_t>(x) - fBounds.fLeft);
        const uint32_t* row = reinterpret_cast<const uint32_t*>(fImage + row_offset);
        const uint32_t* address = row + column;
        return address;
    }

    /**
     *  Returns the address of the specified pixel, computing the pixel-size
     *  at runtime based on the mask format. This will be slightly slower than
     *  using one of the routines where the format is implied by the name
     *  e.g. getAddr8 or getAddr32.
     *
     *  x,y must be contained by the mask's bounds (this is asserted in the
     *  debug build, but not checked in the release build.)
     *
     *  Returns null for kBW_Format or an unknown format. Use getAddr1 for BW.
     */
    const void* getAddr(int x, int y) const;

    /** Iterates over the coverage values along a scanline in a given SkMask::Format. Provides
     *  constructor, copy constructor for creating
     *  operator++, operator-- for iterating over the coverage values on a scanline
     *  operator>>= to add row bytes
     *  operator* to get the coverage value at the current location
     *  operator< to compare two iterators
     */
    template <Format F> struct AlphaIter;
};

template <> struct SkMask::AlphaIter<SkMask::kBW_Format> {
    AlphaIter(const uint8_t* ptr, int offset) : fPtr(ptr), fOffset(7 - offset) {
        SkASSERT(offset >= 0 && offset < 8);
    }
    AlphaIter(const AlphaIter& that) : fPtr(that.fPtr), fOffset(that.fOffset) {}
    AlphaIter& operator++() {
        if (0 < fOffset ) {
            --fOffset;
        } else {
            ++fPtr;
            fOffset = 7;
        }
        return *this;
    }
    AlphaIter& operator--() {
        if (fOffset < 7) {
            ++fOffset;
        } else {
            --fPtr;
            fOffset = 0;
        }
        return *this;
    }
    AlphaIter& operator>>=(uint32_t rb) {
        fPtr += rb;
        return *this;
    }
    uint8_t operator*() const {
        const unsigned int bit = ((*fPtr) >> fOffset) & 1;
        const uint8_t coverage = bit != 0 ? 0xFF : 0;
        return coverage;
    }
    bool operator<(const AlphaIter& that) const {
        const bool before = fPtr < that.fPtr || (fPtr == that.fPtr && fOffset > that.fOffset);
        return before;
    }
    const uint8_t* fPtr;
    int fOffset;
};

template <> struct SkMask::AlphaIter<SkMask::kA8_Format> {
    AlphaIter(const uint8_t* ptr) : fPtr(ptr) {}
    AlphaIter(const AlphaIter& that) : fPtr(that.fPtr) {}
    AlphaIter& operator++() { ++fPtr; return *this; }
    AlphaIter& operator--() { --fPtr; return *this; }
    AlphaIter& operator>>=(uint32_t rb) {
        fPtr += rb;
        return *this;
    }
    uint8_t operator*() const { return *fPtr; }
    bool operator<(const AlphaIter& that) const {
        const bool before = fPtr < that.fPtr;
        return before;
    }
    const uint8_t* fPtr;
};

template <> struct SkMask::AlphaIter<SkMask::kARGB32_Format> {
    AlphaIter(const uint32_t* ptr) : fPtr(ptr) {}
    AlphaIter(const AlphaIter& that) : fPtr(that.fPtr) {}
    AlphaIter& operator++() { ++fPtr; return *this; }
    AlphaIter& operator--() { --fPtr; return *this; }
    AlphaIter& operator>>=(uint32_t rb) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(fPtr);
        fPtr = reinterpret_cast<const uint32_t*>(bytes + rb);
        return *this;
    }
    uint8_t operator*() const {
        const uint8_t coverage = static_cast<uint8_t>(SkGetPackedA32(*fPtr));
        return coverage;
    }
    bool operator<(const AlphaIter& that) const {
        const bool before = fPtr < that.fPtr;
        return before;
    }
    const uint32_t* fPtr;
};

template <> struct SkMask::AlphaIter<SkMask::kLCD16_Format> {
    AlphaIter(const uint16_t* ptr) : fPtr(ptr) {}
    AlphaIter(const AlphaIter& that) : fPtr(that.fPtr) {}
    AlphaIter& operator++() { ++fPtr; return *this; }
    AlphaIter& operator--() { --fPtr; return *this; }
    AlphaIter& operator>>=(uint32_t rb) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(fPtr);
        fPtr = reinterpret_cast<const uint16_t*>(bytes + rb);
        return *this;
    }
    uint8_t operator*() const {
        const unsigned int packed = *fPtr;
        const unsigned int r = SkPacked16ToR32(packed);
        const unsigned int g = SkPacked16ToG32(packed);
        const unsigned int b = SkPacked16ToB32(packed);
        const uint8_t coverage = static_cast<uint8_t>((r + g + b) / 3);
        return coverage;
    }
    bool operator<(const AlphaIter& that) const {
        const bool before = fPtr < that.fPtr;
        return before;
    }
    const uint16_t* fPtr;
};

///////////////////////////////////////////////////////////////////////////////

struct SkMaskBuilder : public SkMask {
    // A builder mutates a descriptor and its caller-owned writable pixels. It
    // never frees storage. The caller must retain an allocation owner separately,
    // including before overwriting an already populated destination descriptor.
    SkMaskBuilder() : SkMask(nullptr, {0, 0, 0, 0}, 0, SkMask::kBW_Format) {}
    SkMaskBuilder(const SkMaskBuilder&) = delete;
    SkMaskBuilder(SkMaskBuilder&& that)
        : SkMask(that.fImage, that.fBounds, that.fRowBytes, that.fFormat) {
        that.resetView();
    }
    SkMaskBuilder& operator=(const SkMaskBuilder&) = delete;
    SkMaskBuilder& operator=(SkMaskBuilder&& that) {
        if (this == &that) return *this;
        fImage = that.fImage;
        fBounds = that.fBounds;
        fRowBytes = that.fRowBytes;
        fFormat = that.fFormat;
        that.resetView();
        return *this;
    }

    SkMaskBuilder(uint8_t* img, const SkIRect& bounds, uint32_t rowBytes, Format format)
        : SkMask(img, bounds, rowBytes, format) {}

    // Restore the builder's writable-pixel capability. fImage itself is a
    // non-const pointer object; the cast changes pointee qualification only.
    uint8_t*& image() & {
        uint8_t*& writable = const_cast<uint8_t*&>(fImage);
        return writable;
    }
    uint8_t*& image() && = delete;
    SkIRect& bounds() & { return fBounds; }
    SkIRect& bounds() && = delete;
    uint32_t& rowBytes() & { return fRowBytes; }
    uint32_t& rowBytes() && = delete;
    Format& format() & { return fFormat; }
    Format& format() && = delete;

    /** Returns the address of the byte that holds the specified bit.
        Asserts that the mask is kBW_Format, and that x,y are in range.
        x,y are in the same coordiate space as fBounds.
    */
    uint8_t* getAddr1(int x, int y) {
        const uint8_t* borrowed = SkMask::getAddr1(x, y);
        uint8_t* writable = const_cast<uint8_t*>(borrowed);
        return writable;
    }

    /** Returns the address of the specified byte.
        Asserts that the mask is kA8_Format, and that x,y are in range.
        x,y are in the same coordiate space as fBounds.
    */
    uint8_t* getAddr8(int x, int y) {
        const uint8_t* borrowed = SkMask::getAddr8(x, y);
        uint8_t* writable = const_cast<uint8_t*>(borrowed);
        return writable;
    }

    /**
     *  Return the address of the specified 16bit mask. In the debug build,
     *  this asserts that the mask's format is kLCD16_Format, and that (x,y)
     *  are contained in the mask's fBounds.
     */
    uint16_t* getAddrLCD16(int x, int y) {
        const uint16_t* borrowed = SkMask::getAddrLCD16(x, y);
        uint16_t* writable = const_cast<uint16_t*>(borrowed);
        return writable;
    }

    /**
     *  Return the address of the specified 32bit mask. In the debug build,
     *  this asserts that the mask's format is 32bits, and that (x,y)
     *  are contained in the mask's fBounds.
     */
    uint32_t* getAddr32(int x, int y) {
        const uint32_t* borrowed = SkMask::getAddr32(x, y);
        uint32_t* writable = const_cast<uint32_t*>(borrowed);
        return writable;
    }

    /**
     *  Returns the address of the specified pixel, computing the pixel-size
     *  at runtime based on the mask format. This will be slightly slower than
     *  using one of the routines where the format is implied by the name
     *  e.g. getAddr8 or getAddr32.
     *
     *  x,y must be contained by the mask's bounds (this is asserted in the
     *  debug build, but not checked in the release build.)
     *
     *  Returns null for kBW_Format or an unknown format. Use getAddr1 for BW.
     */
    void* getAddr(int x, int y) {
        const void* borrowed = SkMask::getAddr(x, y);
        void* writable = const_cast<void*>(borrowed);
        return writable;
    }

    enum AllocType {
        kUninit_Alloc,
        kZeroInit_Alloc,
    };
    static uint8_t* AllocImage(size_t bytes, AllocType = kUninit_Alloc);
    static void FreeImage(void* image);

    enum CreateMode {
        kJustComputeBounds_CreateMode,      //!< compute bounds and return
        kJustRenderImage_CreateMode,        //!< render into preallocate mask
        kComputeBoundsAndRenderImage_CreateMode  //!< compute bounds, alloc image and render into it
    };

    /**
     *  Returns an A8 descriptor padded by nonnegative radiusX and radiusY.
     *  Invalid dimensions, coordinates or byte extent return an empty descriptor.
     *  A source with pixels acquires uninitialized destination storage; the caller
     *  owns it and must adopt it into SkAutoMaskFreeImage or transfer it explicitly.
     *  A metadata-only source produces metadata only. Allocation failure aborts.
     */
    static SkMaskBuilder PrepareDestination(int radiusX, int radiusY, const SkMask& src);

private:
    void resetView() {
        fImage = nullptr;
        fBounds.setEmpty();
        fRowBytes = 0;
        fFormat = SkMask::kBW_Format;
    }
};

/**
 *  \using SkAutoMaskImage
 *
 *  Stack class used to manage the fImage buffer in a SkMask.
 *  When this object loses scope, the buffer is freed with SkMaskBuilder::FreeImage().
 */
struct SkMaskImageDeleter {
    void operator()(uint8_t* image) const {
        SkMaskBuilder::FreeImage(image);
    }
};
using SkAutoMaskFreeImage = std::unique_ptr<uint8_t, SkMaskImageDeleter>;

#endif
