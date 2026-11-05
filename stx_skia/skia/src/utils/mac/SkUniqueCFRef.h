/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkUniqueCFRef_DEFINED
#define SkUniqueCFRef_DEFINED

#include "include/core/SkTypes.h"
#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)

#include <CoreFoundation/CoreFoundation.h>
#include <memory>
#include <type_traits>

// The unique owner holds one retained Core Foundation reference. Observers do
// not retain it; release transfers that obligation to the caller.
struct SkReleaseCFObject {
    void operator()(CFTypeRef object) const { CFRelease(object); }
};

template <typename CFRef> using SkUniqueCFRef =
    std::unique_ptr<std::remove_pointer_t<CFRef>, SkReleaseCFObject>;

#endif
#endif
