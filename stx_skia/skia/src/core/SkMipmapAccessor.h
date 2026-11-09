/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkMipmapAccessor_DEFINED
#define SkMipmapAccessor_DEFINED

#include "include/core/SkBitmap.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "include/private/SkAssert.h"
#include "include/private/SkNoncopyable.h"
#include "src/core/SkMipmap.h"

#include <utility>

class SkArenaAlloc;
class SkImage;
class SkImage_Base;
enum class SkMipmapMode;

// Selects CPU sampling levels. The arena owns the accessor and runs its destructor.
// Retained bitmap/mipmap storage keeps returned pixel views alive until arena reset;
// the input image and matrix need not outlive construction. Separate draws use
// separate accessors. An accessor does not mutate the retained pixels.
class SkMipmapAccessor : ::SkNoncopyable {
public:
    // Image and arena must be nonnull. Returns null if no readable upper level
    // can be acquired; any partial acquisition is released with the arena.
    static SkMipmapAccessor* Make(SkArenaAlloc*, const SkImage*, const SkMatrix& inverse,
                                  SkMipmapMode);

    // Existing sampling callers consume (pixel view, base-to-level scale).
    // These copied views borrow this accessor's retained storage.
    std::pair<SkPixmap, SkMatrix> level() const & {
        SkASSERT(fUpper.addr() != nullptr);
        const std::pair<SkPixmap, SkMatrix> selected{fUpper, fUpperInv};
        return selected;
    }
    std::pair<SkPixmap, SkMatrix> level() const && = delete;

    // Only request the lower level when lowerWeight() is positive.
    std::pair<SkPixmap, SkMatrix> lowerLevel() const & {
        SkASSERT(fLower.addr() != nullptr);
        const std::pair<SkPixmap, SkMatrix> selected{fLower, fLowerInv};
        return selected;
    }
    std::pair<SkPixmap, SkMatrix> lowerLevel() const && = delete;

    // lower * weight + upper * (1 - weight); zero means upper-only sampling.
    float lowerWeight() const { return fLowerWeight; }

    // Public only so SkArenaAlloc can construct it inside Make().
    SkMipmapAccessor(const SkImage_Base*, const SkMatrix& inverse, SkMipmapMode);

private:
    // Owners precede borrowed views so their storage outlives those views.
    SkBitmap fBaseStorage{};
    sk_sp<const SkMipmap> fCurrMip{};
    SkPixmap fUpper{};
    SkPixmap fLower{};
    float fLowerWeight = 0.0f;
    SkMatrix fUpperInv = SkMatrix::I();
    SkMatrix fLowerInv = SkMatrix::I();

    // Acquires base pixels at most once after success. Failure leaves an empty
    // upper view and permits a later fallback attempt, as in the original.
    void loadUpperFromBase(const SkImage_Base&);
};

#endif
