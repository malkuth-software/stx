/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkImageGenerator.h"

#include "include/core/SkColorType.h"
#include "include/private/SkAssert.h"
#include "src/core/SkNextID.h"
#include "src/core/SkImageInfoPriv.h"

SkImageGenerator::SkImageGenerator(const SkImageInfo& info, uint32_t uniqueID)
    : fInfo(info)
    , fUniqueID(kNeedNewImageUniqueID == uniqueID ? SkNextID::ImageID() : uniqueID)
{}

bool SkImageGenerator::getPixels(const SkImageInfo& info, void* pixels, size_t rowBytes) {
    if (!SkImageInfoIsValid(info) || pixels == nullptr) {
        return false;
    }
    if (!info.validRowBytes(rowBytes)) {
        return false;
    }
    const std::optional<size_t> extent = info.checkedByteSize(rowBytes);
    if (!extent) {
        return false;
    }

    const Options defaultOptions{};
    const bool generated = onGetPixels(info, pixels, rowBytes, defaultOptions);
    return generated;
}
