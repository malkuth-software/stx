/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkStream.h"

#include "include/core/SkTypes.h"
#include "src/core/SkStreamPriv.h"

#include <cstddef>

SkWStream::~SkWStream() = default;

void SkWStream::flush() {}

namespace SkStreamPriv {

bool RemainingLengthIsBelow(const SkStream& stream, size_t len) {
    if (stream.hasLength()) {
        if (stream.hasPosition()) {
            const size_t total = stream.getLength();
            const size_t position = stream.getPosition();
            if (position > total) return true;
            const size_t remainingBytes = total - position;
            return remainingBytes < len;
        }
        // We don't know the position, but we can still return true if the
        // stream's entire length is shorter than the requested length.
        return stream.getLength() < len;
    }
    return false;
}

}  // namespace SkStreamPriv
