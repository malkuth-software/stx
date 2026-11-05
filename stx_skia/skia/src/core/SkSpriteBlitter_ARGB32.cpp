/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkColorType.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkTypes.h"
#include "include/private/SkCPUTypes.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkBlitRow.h"
#include "src/core/SkSpriteBlitter.h"

#include <cstddef>
#include <cstdint>

///////////////////////////////////////////////////////////////////////////////

class Sprite_D32_S32 : public SkSpriteBlitter {
public:
    Sprite_D32_S32(const SkPixmap& src, U8CPU alpha)  : INHERITED(src) {
        SkASSERT(src.colorType() == kN32_SkColorType);

        unsigned flags32 = 0;
        if (255 != alpha) {
            flags32 |= SkBlitRow::kGlobalAlpha_Flag32;
        }
        if (!src.isOpaque()) {
            flags32 |= SkBlitRow::kSrcPixelAlpha_Flag32;
        }

        fProc32 = SkBlitRow::Factory32(flags32);
        fAlpha = alpha;
    }

    // Each row's addresses come from the destination and the source.
    void blitRect(int x, int y, int width, int height) override {
        SkASSERT(width > 0 && height > 0);
        const SkBlitRow::Proc32 proc = fProc32;
        const U8CPU alpha = fAlpha;

        for (int row = y; row < y + height; ++row) {
            uint32_t* dst = fDst.writable_addr32(x, row);
            const uint32_t* src = fSource.addr32(x - fLeft, row - fTop);
            proc(dst, src, width, alpha);
        }
    }

private:
    SkBlitRow::Proc32   fProc32 = nullptr;
    U8CPU               fAlpha = 0;

    using INHERITED = SkSpriteBlitter;
};

///////////////////////////////////////////////////////////////////////////////

SkSpriteBlitter* SkSpriteBlitter::ChooseL32(const SkPixmap& source, const SkPaint& paint,
                                            SkArenaAlloc* allocator) {
    SkASSERT(allocator != nullptr);

    if (paint.getColorFilter() != nullptr) {
        return nullptr;
    }
    if (paint.getMaskFilter() != nullptr) {
        return nullptr;
    }
    if (source.colorType() == kN32_SkColorType && paint.isSrcOver()) {
        // this can handle alpha, but not xfermode
        return (*allocator).make<Sprite_D32_S32>(source, paint.getAlpha());
    }
    return nullptr;
}
