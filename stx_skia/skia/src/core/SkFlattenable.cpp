/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "include/core/SkFlattenable.h"

#include "include/core/SkData.h"
#include "include/core/SkSerialProcs.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"

#include <cstdint>

sk_sp<SkData> SkFlattenable::serialize(const SkSerialProcs* procs) const {
    SkSerialProcs procedures{};
    if (procs != nullptr) procedures = *procs;
    SkBinaryWriteBuffer writer(procedures);
    writer.writeFlattenable(this);
    const size_t size = writer.bytesWritten();
    sk_sp<SkData> data = SkData::MakeUninitialized(size);
    void* destination = (*data).writable_data();
    writer.writeToMemory(destination);
    return data;
}

size_t SkFlattenable::serialize(void* memory, size_t memorySize, const SkSerialProcs* procs) const {
    if (memory == nullptr || memorySize == 0) return 0;
    const uintptr_t address = reinterpret_cast<uintptr_t>(memory);
    if ((address & 3U) != 0) return 0;
    SkSerialProcs procedures{};
    if (procs != nullptr) procedures = *procs;
    SkBinaryWriteBuffer writer(memory, memorySize, procedures);
    writer.writeFlattenable(this);
    if (!writer.usingInitialStorage()) return 0;
    const size_t written = writer.bytesWritten();
    return written;
}

sk_sp<SkFlattenable> SkFlattenable::Deserialize(Type type, const void* data, size_t size,
                                             const SkDeserialProcs* procs) {
    if (data == nullptr || size == 0) return nullptr;
    SkReadBuffer buffer(data, size);
    if (procs != nullptr) buffer.setDeserialProcs(*procs);
    SkFlattenable* decoded = buffer.readFlattenable(type);
    sk_sp<SkFlattenable> result(decoded);
    return result;
}
