/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkGraphics.h"

#include "include/private/SkAssert.h"
#include "src/core/SkBlitMask.h"
#include "src/core/SkBlitRow.h"
#include "src/core/SkCpu.h"
#include "src/core/SkGraphicsLifetime.h"
#include "src/core/SkMemset.h"
#include "src/core/SkOpts.h"
#include "src/core/SkResourceCache.h"
#include "src/core/SkResourceCacheInvalidation.h"
#include "src/core/SkSwizzlePriv.h"

#include <atomic>
#include <cstdint>

namespace {

enum class Lifetime : uint8_t {
    kRunning,
    kShuttingDown,
    kShutDown,
};

// Process lifetime: read by every cache and singleton accessor, written by Shutdown() alone.
std::atomic<Lifetime> gLifetime{Lifetime::kRunning};

}  // namespace

void SkGraphicsCheckRunning() {
    // Shutdown's own release functions run while kShuttingDown, and may reach an accessor (the
    // cache's inbox unregisters from the registry), so only the finished state stops.
    SkASSERT_RELEASE(gLifetime.load(std::memory_order_acquire) != Lifetime::kShutDown);
}

void SkGraphics::Shutdown() {
    Lifetime expected = Lifetime::kRunning;
    const bool first = gLifetime.compare_exchange_strong(expected, Lifetime::kShuttingDown,
                                                         std::memory_order_acq_rel);
    SkASSERT_RELEASE(first);
    // Order: the cache first, since deleting it unregisters its inbox from the registry and
    // drops its records' references; then the registry; the recorder before the context that
    // made it; then the reference-counted singletons, the gamma filters (which hold the color
    // spaces) before the color spaces.
    SkResourceCacheRelease();
    SkResourceCacheInvalidationInbox::ReleaseRegistry();
    SkCPURecorderRelease();
    SkCPUContextRelease();
    SkDataReleaseEmpty();
    SkDataTableReleaseEmpty();
    SkPathReleaseErrorSingleton();
    SkPathDataReleaseEmpty();
    SkColorFiltersReleaseGamma();
    SkColorSpaceReleaseSingletons();
    gLifetime.store(Lifetime::kShutDown, std::memory_order_release);
}

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
