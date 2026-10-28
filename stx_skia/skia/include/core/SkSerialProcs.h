/*
 * Copyright 2017 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkSerialProcs_DEFINED
#define SkSerialProcs_DEFINED

#include "include/core/SkRefCnt.h"
#include "include/private/SkAPI.h"

#include <cstddef>
#include <optional>

class SkData;
class SkImage;
enum SkAlphaType : int;

using SkSerialReturnType = sk_sp<const SkData>;
// Callbacks retain their context only for the synchronous serialization call.
// A decoder must own or copy any bytes needed after its callback returns.
using SkSerialImageProc = SkSerialReturnType (*)(SkImage*, void* context);
#if !defined(SK_LEGACY_DESERIAL_IMAGE_PROC)
using SkDeserialImageProc = sk_sp<SkImage> (*)(const void* data, size_t length, void* context);
#else
using SkDeserialImageProc = sk_sp<SkImage> (*)(const void* data, size_t length,
                                            std::optional<SkAlphaType>, void* context);
#endif
using SkDeserialImageFromDataProc = sk_sp<SkImage> (*)(sk_sp<SkData>,
                                                     std::optional<SkAlphaType>, void* context);

struct SK_API SkSerialProcs {
    SkSerialImageProc fImageProc = nullptr;
    void* fImageCtx = nullptr;
};

struct SK_API SkDeserialProcs {
    SkDeserialImageProc fImageProc = nullptr;
    SkDeserialImageFromDataProc fImageDataProc = nullptr;
    void* fImageCtx = nullptr;
};

#endif
