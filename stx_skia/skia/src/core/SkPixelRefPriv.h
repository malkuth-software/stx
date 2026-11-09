// Copyright 2019 Google LLC
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SkPixelRefPriv_DEFINED
#define SkPixelRefPriv_DEFINED

#include "include/core/SkRefCnt.h"

#include <cstddef>

class SkPixelRef;

/**
 *  Return a new pixel reference with the provided pixel storage and
 *  rowBytes. On destruction, ReleaseProc will be called.
 *
 *  If ReleaseProc is null, the pixels will never be released. This
 *  can be useful if the pixels were stack allocated. However, such an
 *  pixel reference must not live beyond its pixels (e.g. by copying
 *  an SkBitmap pointing to it, or drawing to an SkPicture).
 *
 *  Nonnegative dimensions and a valid pixel extent are caller preconditions.
 *  A supplied callback owns release of the allocation; a null callback borrows
 *  pixels until the final reference is destroyed. Object allocation follows
 *  Skia's fail-fast policy.
 */
sk_sp<SkPixelRef> SkMakePixelRefWithProc(int width, int height, size_t row_bytes, void* pixels,
                                      void (*release)(void* pixels, void* context), void* context);

#endif  // SkPixelRefPriv_DEFINED
