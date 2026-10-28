/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkGraphics_DEFINED
#define SkGraphics_DEFINED

#include "include/core/SkRefCnt.h"
#include "include/private/SkAPI.h"

#include <cstddef>
#include <cstdint>
#include <memory>

class SkData;
class SkImageGenerator;
class SkTraceMemoryDump;

class SK_API SkGraphics {
public:
    /**
     *  Call this at process initialization time if your environment does not
     *  permit static global initializers that execute code.
     *  Init() is thread-safe and idempotent.
     */
    static void Init();

    /**
     *  This function returns the memory used for temporary images and other resources.
     */
    static size_t GetResourceCacheTotalBytesUsed();

    /**
     *  These functions get/set the memory usage limit for the resource cache, used for temporary
     *  bitmaps and other resources. Entries are purged from the cache when the memory useage
     *  exceeds this limit.
     */
    static size_t GetResourceCacheTotalByteLimit();
    static size_t SetResourceCacheTotalByteLimit(size_t newLimit);

    /**
     *  For debugging purposes, this will attempt to purge the resource cache. It
     *  does not change the limit.
     */
    static void PurgeResourceCache();

    /**
     *  When the cachable entry is very lage (e.g. a large scaled bitmap), adding it to the cache
     *  can cause most/all of the existing entries to be purged. To avoid the, the client can set
     *  a limit for a single allocation. If a cacheable entry would have been cached, but its size
     *  exceeds this limit, then we do not attempt to cache it at all.
     *
     *  Zero is the default value, meaning we always attempt to cache entries.
     */
    static size_t GetResourceCacheSingleAllocationByteLimit();
    static size_t SetResourceCacheSingleAllocationByteLimit(size_t newLimit);

    /**
     *  Dumps memory usage of caches using the SkTraceMemoryDump interface. See SkTraceMemoryDump
     *  for usage of this method.
     */
    static void DumpMemoryStatistics(SkTraceMemoryDump* dump);

    /**
     *  Free as much globally cached memory as possible. This will purge all private caches in Skia,
     *  including temporary image caches.
     *
     *  If there are caches associated with GPU context, those will not be affected by this call.
     */
    static void PurgeAllCaches();

    using ImageGeneratorFromEncodedDataFactory =
            std::unique_ptr<SkImageGenerator> (*)(sk_sp<const SkData>);

    /**
     *  To instantiate images from encoded data, first looks at this runtime function-ptr. If it
     *  exists, it is called to create an SkImageGenerator from SkData. If there is no function-ptr
     *  or there is, but it returns NULL, then skia will call its internal default implementation.
     *
     *  Returns the previous factory (which could be NULL).
     */
    static ImageGeneratorFromEncodedDataFactory
                    SetImageGeneratorFromEncodedDataFactory(ImageGeneratorFromEncodedDataFactory);


};

#endif
