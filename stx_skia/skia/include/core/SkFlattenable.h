/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkFlattenable_DEFINED
#define SkFlattenable_DEFINED

#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"

#include <cstddef>

class SkData;
class SkReadBuffer;
class SkWriteBuffer;
struct SkDeserialProcs;
struct SkSerialProcs;

// Shared CPU effect objects retain their ordinary serialization hooks. The owned
// library has no process-wide serialized-effect registry or automatic factories.
class SK_API SkFlattenable : public SkRefCnt {
public:
    // Preserve historical tag values even for categories the CPU library excludes.
    enum Type {
        kSkColorFilter_Type = 0,
        kSkBlender_Type = 1,
        kSkDrawLooper_Type = 3,
        kSkImageFilter_Type = 4,
        kSkMaskFilter_Type = 5,
        kSkPathEffect_Type = 6,
        kSkShader_Type = 7,
    };
    using Factory = sk_sp<SkFlattenable> (*)(SkReadBuffer&);

    SkFlattenable() {}
    virtual Factory getFactory() const = 0;
    // Returns a process-lifetime, nonempty type-name string.
    virtual const char* getTypeName() const = 0;
    virtual void flatten(SkWriteBuffer&) const {}
    virtual Type getFlattenableType() const = 0;

    // The returned data owns its bytes. Procs and contexts are borrowed only
    // during this synchronous call; allocation follows the writer's abort policy.
    sk_sp<SkData> serialize(const SkSerialProcs* procs = nullptr) const;
    // Storage must be live, writable and four-byte aligned. Returns zero for
    // invalid storage or insufficient space. On insufficient space a prefix may
    // have been written and the writer may have allocated private fallback storage.
    size_t serialize(void* memory, size_t memorySize, const SkSerialProcs* procs = nullptr) const;
    // Default named-effect decoding is unsupported and returns an empty owner.
    // Input and optional callback contexts are borrowed for the call only.
    static sk_sp<SkFlattenable> Deserialize(Type type, const void* data, size_t length,
                                           const SkDeserialProcs* procs = nullptr);
};

#define SK_FLATTENABLE_HOOKS(type)                              \
    static sk_sp<SkFlattenable> CreateProc(SkReadBuffer&);         \
    Factory getFactory() const override { return nullptr; }      \
    const char* getTypeName() const override { return #type; }

#endif
