/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkGraphics.h"

#include "src/core/SkBlitMask.h"
#include "src/core/SkBlitRow.h"
#include "src/core/SkCpu.h"
#include "src/core/SkMemset.h"
#include "src/core/SkOpts.h"
#include "src/core/SkResourceCache.h"
#include "src/core/SkSwizzlePriv.h"

void SkGraphics::Init() {
    // SkGraphics::Init() must be thread-safe and idempotent.
    SkCpu::CacheRuntimeFeatures();
    SkOpts::Init();
    SkOpts::Init_BlitMask();
    SkOpts::Init_BlitRow();
    SkOpts::Init_Memset();
    SkOpts::Init_Swizzler();
}

///////////////////////////////////////////////////////////////////////////////

void SkGraphics::DumpMemoryStatistics(SkTraceMemoryDump* dump) {
    SkResourceCache::DumpMemoryStatistics(dump);
}

void SkGraphics::PurgeAllCaches() {
    SkGraphics::PurgeResourceCache();
}

///////////////////////////////////////////////////////////////////////////////

size_t SkGraphics::GetResourceCacheTotalBytesUsed() {
    const size_t bytes = SkResourceCache::GetTotalBytesUsed();
    return bytes;
}

size_t SkGraphics::GetResourceCacheTotalByteLimit() {
    const size_t bytes = SkResourceCache::GetTotalByteLimit();
    return bytes;
}

size_t SkGraphics::SetResourceCacheTotalByteLimit(size_t newLimit) {
    const size_t bytes = SkResourceCache::SetTotalByteLimit(newLimit);
    return bytes;
}

size_t SkGraphics::GetResourceCacheSingleAllocationByteLimit() {
    const size_t bytes = SkResourceCache::GetSingleAllocationByteLimit();
    return bytes;
}

size_t SkGraphics::SetResourceCacheSingleAllocationByteLimit(size_t newLimit) {
    const size_t bytes = SkResourceCache::SetSingleAllocationByteLimit(newLimit);
    return bytes;
}

void SkGraphics::PurgeResourceCache() {
    SkResourceCache::PurgeAll();
}
