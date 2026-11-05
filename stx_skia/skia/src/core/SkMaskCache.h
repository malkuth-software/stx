/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkMaskCache_DEFINED
#define SkMaskCache_DEFINED

#include "include/core/SkScalar.h"
#include "include/core/SkSpan.h"

class SkCachedData;
class SkRRect;
class SkResourceCache;
enum SkBlurStyle : int;
struct SkMask;
struct SkRect;
template <typename T> class SkTLazy;

class SkMaskCache {
public:
    /**
     * On success, return a ref to the SkCachedData that holds the pixels, and have mask
     * already point to that memory. The caller adopts that client reference; the mask
     * borrows its pixels for as long as the reference is held.
     *
     * On failure, return nullptr and leave mask unchanged. A null localCache selects the
     * process-wide cache. The rectangle forms take one or two rectangles.
     */
    static SkCachedData* FindAndRef(SkScalar sigma, SkBlurStyle style,
                                    const SkRRect& rrect, SkTLazy<SkMask>* mask,
                                    SkResourceCache* localCache = nullptr);
    static SkCachedData* FindAndRef(SkScalar sigma,
                                    SkBlurStyle style,
                                    SkSpan<const SkRect> rects,
                                    SkTLazy<SkMask>* mask,
                                    SkResourceCache* localCache = nullptr);

    /**
     * Add a mask and its pixel-data to the cache. data must be non-null; the new record
     * takes its own cache reference, and the mask's image pointer is not retained.
     */
    static void Add(SkScalar sigma, SkBlurStyle style,
                    const SkRRect& rrect, const SkMask& mask, SkCachedData* data,
                    SkResourceCache* localCache = nullptr);
    static void Add(SkScalar sigma,
                    SkBlurStyle style,
                    SkSpan<const SkRect> rects,
                    const SkMask& mask,
                    SkCachedData* data,
                    SkResourceCache* localCache = nullptr);
};

#endif
