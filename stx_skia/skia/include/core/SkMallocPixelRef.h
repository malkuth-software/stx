/*
 * Copyright 2008 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkMallocPixelRef_DEFINED
#define SkMallocPixelRef_DEFINED

#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"

#include <cstddef>

class SkData;
class SkPixelRef;
struct SkImageInfo;

/** We explicitly use the same allocator for our pixels that SkMask does,
    so that we can freely assign memory allocated by one class to the other.
*/
namespace SkMallocPixelRef {
    /**
     *  Return a new SkMallocPixelRef, automatically allocating storage for the
     *  pixels. If rowBytes are 0, an optimal value will be chosen automatically.
     *  If rowBytes is > 0, then it will be respected, or nullptr will be returned
     *  if rowBytes is invalid for the specified info.
     *
     *  All pixel bytes are zeroed.
     *
     *  Returns nullptr on failure.
     */
    SK_API sk_sp<SkPixelRef> MakeAllocate(const SkImageInfo& info, size_t rowBytes);

    /**
     *  Return a new SkMallocPixelRef that will use the provided SkData and
     *  rowBytes as pixel storage.  The SkData will be ref()ed and on
     *  destruction of the PixelRef, the SkData will be unref()ed.
     *
     *  data must be non-null. The pixel reference retains its allocation and is
     *  immutable. Callers must surrender writable aliases before sharing it.
     *  Returns nullptr on invalid dimensions, stride or insufficient data.
     */
    SK_API sk_sp<SkPixelRef> MakeWithData(const SkImageInfo& info, size_t rowBytes, sk_sp<SkData> data);
}  // namespace SkMallocPixelRef
#endif
