/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkMipmapBuilder_DEFINED
#define SkMipmapBuilder_DEFINED

#include "include/core/SkRefCnt.h"

class SkImage;
class SkMipmap;
class SkPixmap;
struct SkImageInfo;

class SkMipmapBuilder {
public:
    explicit SkMipmapBuilder(const SkImageInfo&);
    ~SkMipmapBuilder();

    int countLevels() const;
    // Writable view borrowed until this builder and every attached image release
    // the levels. Fill every level before attachment; do not mutate after sharing.
    // Invalid indices or failed construction return an empty view.
    SkPixmap level(int index) const &;
    SkPixmap level(int index) const && = delete;

    // Shares completed levels with a compatible image. Null input is rejected;
    // neither input nor builder is consumed. Incompatible levels return the
    // original image; an empty builder permits default mipmap generation, as
    // withMipmaps does. Neither fallback is an attachment-success guarantee.
    sk_sp<SkImage> attachTo(const sk_sp<const SkImage>& src);

private:
    sk_sp<SkMipmap> fMM{};
};

#endif
