// Copyright (c) 2026 Joshuah Rainstar
// SPDX-License-Identifier: MIT
#ifndef SkGraphicsLifetime_DEFINED
#define SkGraphicsLifetime_DEFINED

// The process lifetime of stx_skia's caches and singletons, which SkGraphics::Shutdown() ends.
//
// Every accessor of a cache or singleton calls SkGraphicsCheckRunning() before it reads its
// static slot. Once Shutdown() has freed them, the check stops the program, so a use after
// shutdown cannot reach freed memory. Shutdown() itself runs the release functions below in
// the order it states; each frees what its slot holds (or drops the process's reference, for a
// reference-counted singleton that a live object may still share) and empties the slot, so
// leak checking sees any slot a release missed.
//
// Shutdown is one call on one thread, with no other stx_skia use in flight: the checks catch
// use after it, not use racing it.

// Stops the program once SkGraphics::Shutdown() has finished.
void SkGraphicsCheckRunning();

// The release functions, one per owning source file. Only SkGraphics::Shutdown() calls them.
// The invalidation registry's release is SkResourceCacheInvalidationInbox::ReleaseRegistry().
void SkResourceCacheRelease();         // SkResourceCache.cpp: the resource cache.
void SkCPURecorderRelease();           // SkCPURecorder.cpp: the process-wide recorder.
void SkCPUContextRelease();            // SkCPUContext.cpp: the process-wide context.
void SkDataReleaseEmpty();             // SkData.cpp: the empty SkData.
void SkDataTableReleaseEmpty();        // SkDataTable.cpp: the empty table.
void SkPathDataReleaseEmpty();         // SkPathData.cpp: the empty path data.
void SkPathReleaseErrorSingleton();    // SkPath.cpp: the error path data.
void SkColorFiltersReleaseGamma();     // SkColorSpaceXformColorFilter.cpp: the two gamma filters.
void SkColorSpaceReleaseSingletons();  // SkColorSpace.cpp: sRGB and linear sRGB.

#endif
