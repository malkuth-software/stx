/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/core/SkMipmapBuilder.h"

#include "include/core/SkImage.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkTypes.h"
#include "src/core/SkMipmap.h"
SkMipmapBuilder::SkMipmapBuilder(const SkImageInfo& info) {
    const SkPixmap metadata(info, nullptr, 0);
    SkMipmap* levels = SkMipmap::Build(metadata, nullptr, false);
    fMM.reset(levels);
}

SkMipmapBuilder::~SkMipmapBuilder() {}

int SkMipmapBuilder::countLevels() const {
    if (!fMM) {
        return 0;
    }
    const int count = (*fMM).countLevels();
    return count;
}

SkPixmap SkMipmapBuilder::level(int index) const & {
    SkPixmap pixels{};
    SkMipmap::Level selected{};
    if (fMM && (*fMM).getLevel(index, &selected)) {
        pixels = selected.fPixmap;
    }
    return pixels;
}

sk_sp<SkImage> SkMipmapBuilder::attachTo(const sk_sp<const SkImage>& source) {
    if (!source) {
        return nullptr;
    }
    sk_sp<SkImage> image = (*source).withMipmaps(fMM);
    return image;
}
