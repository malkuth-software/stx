/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkBlitter_A8.h"

#include "include/core/SkBlendMode.h"
#include "include/core/SkColorType.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkShader.h" // IWYU pragma: keep
#include "include/core/SkTypes.h"
#include "include/private/SkDebug.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkDrawTypes.h"
#include "src/core/SkMask.h"

#include <cstddef>
#include <cstring>
#include <optional>
#include <utility>

SkA8_Coverage_Blitter::SkA8_Coverage_Blitter(const SkPixmap& device, const SkPaint& paint)
    : fDevice(device)
{
    SkASSERT(nullptr == paint.getShader());
    SkASSERT(nullptr == paint.getColorFilter());
}

void SkA8_Coverage_Blitter::blitAntiH(int x, int y, const SkAlpha antialias[],
                                      const int16_t runs[]) {
    uint8_t* device = fDevice.writable_addr8(x, y);

    int index = 0;
    while (true) {
        const int count = runs[index];
        SkASSERT(count >= 0);
        if (count == 0) {
            return;
        }
        if (antialias[index]) {
            memset(device + index, antialias[index], static_cast<size_t>(count));
        }
        index += count;
    }
}

void SkA8_Coverage_Blitter::blitH(int x, int y, int width) {
    memset(fDevice.writable_addr8(x, y), 0xFF, static_cast<size_t>(width));
}

void SkA8_Coverage_Blitter::blitV(int x, int y, int height, SkAlpha alpha) {
    if (0 == alpha) {
        return;
    }

    for (int row = y; row < y + height; ++row) {
        *fDevice.writable_addr8(x, row) = alpha;
    }
}

void SkA8_Coverage_Blitter::blitRect(int x, int y, int width, int height) {
    for (int row = y; row < y + height; ++row) {
        memset(fDevice.writable_addr8(x, row), 0xFF, static_cast<size_t>(width));
    }
}

void SkA8_Coverage_Blitter::blitMask(const SkMask& mask, const SkIRect& clip) {
    if (SkMask::kA8_Format != mask.fFormat) {
        (*this).SkBlitter::blitMask(mask, clip);
        return;
    }

    const int x = clip.fLeft;
    const size_t width = static_cast<size_t>(clip.width());

    for (int y = clip.fTop; y < clip.fBottom; ++y) {
        memcpy(fDevice.writable_addr8(x, y), mask.getAddr8(x, y), width);
    }
}

//////////////

static inline uint8_t div255(unsigned prod) {
    SkASSERT(prod <= 255*255);
    const uint8_t quotient = static_cast<uint8_t>((prod + 128) * 257 >> 16);
    return quotient;
}

static inline unsigned u8_lerp(uint8_t a, uint8_t b, uint8_t t) {
    const unsigned lerp = div255((255 - t) * a + t * b);
    return lerp;
}

using AlphaProc = uint8_t(*)(uint8_t src, uint8_t dst);

static uint8_t srcover_p(uint8_t src, uint8_t dst) {
    const uint8_t result = static_cast<uint8_t>(src + div255((255 - src) * dst));
    return result;
}

static uint8_t src_p(uint8_t src, uint8_t dst) {
    return src;
}

// Blends src into N opaque-coverage pixels.
template <AlphaProc kProc>
static void A8_row_bw(uint8_t dst[], uint8_t src, int N) {
    for (int i = 0; i < N; ++i) {
        dst[i] = kProc(src, dst[i]);
    }
}
using A8_RowBlitBW = void(*)(uint8_t[], uint8_t, int);

// Blends src into N pixels of partial coverage aa. When the blend allows it, the coverage is
// folded into src instead of interpolating each result.
template <AlphaProc kProc, bool kCanFoldAA>
static void A8_row_aa(uint8_t dst[], uint8_t src, int N, uint8_t aa) {
    if (kCanFoldAA) {
        const uint8_t scaled = div255(src * aa);
        for (int i = 0; i < N; ++i) {
            dst[i] = kProc(scaled, dst[i]);
        }
    } else {
        for (int i = 0; i < N; ++i) {
            dst[i] = static_cast<uint8_t>(u8_lerp(dst[i], kProc(src, dst[i]), aa));
        }
    }
}
using A8_RowBlitAA = void(*)(uint8_t[], uint8_t, int, uint8_t aa);

struct A8_RowBlitBWPair {
    SkBlendMode     mode;
    AlphaProc       oneProc;
    A8_RowBlitBW    bwProc;
    A8_RowBlitAA    aaProc;
};
constexpr A8_RowBlitBWPair gA8_RowBlitPairs[] = {
    {SkBlendMode::kSrcOver, srcover_p, A8_row_bw<srcover_p>, A8_row_aa<srcover_p, true>},
    {SkBlendMode::kSrc,     src_p,     A8_row_bw<src_p>,     A8_row_aa<src_p, false>},
};

static const A8_RowBlitBWPair* find_a8_rowproc_pair(SkBlendMode bm) {
    for (const A8_RowBlitBWPair& pair : gA8_RowBlitPairs) {
        if (pair.mode == bm) {
            return &pair;
        }
    }
    return nullptr;
}

class SkA8_Blitter : public SkBlitter {
public:
    SkA8_Blitter(const SkPixmap& device, const SkPaint& paint);
    void blitH(int x, int y, int width) override;
    void blitAntiH(int x, int y, const SkAlpha antialias[], const int16_t runs[]) override;
    void blitV(int x, int y, int height, SkAlpha alpha) override;
    void blitRect(int x, int y, int width, int height) override;
    void blitMask(const SkMask&, const SkIRect&) override;

private:
    const SkPixmap  fDevice;
    AlphaProc       fOneProc = nullptr;
    A8_RowBlitBW    fBWProc = nullptr;
    A8_RowBlitAA    fAAProc = nullptr;
    SkAlpha         fSrc = 0;

    using INHERITED = SkBlitter;
};

// The chooser admits only paints whose blend mode has a row procedure pair.
SkA8_Blitter::SkA8_Blitter(const SkPixmap& device,
                           const SkPaint& paint) : fDevice(device) {
    SkASSERT(nullptr == paint.getShader());
    SkASSERT(nullptr == paint.getColorFilter());
    const std::optional<SkBlendMode> mode = paint.asBlendMode();
    SkASSERT_RELEASE(mode.has_value());
    const A8_RowBlitBWPair* pair = find_a8_rowproc_pair(*mode);
    SkASSERT_RELEASE(pair);

    fOneProc = (*pair).oneProc;
    fBWProc  = (*pair).bwProc;
    fAAProc  = (*pair).aaProc;
    fSrc = paint.getAlpha();
}

void SkA8_Blitter::blitAntiH(int x, int y, const SkAlpha antialias[], const int16_t runs[]) {
    uint8_t* device = fDevice.writable_addr8(x, y);

    int index = 0;
    while (true) {
        const int count = runs[index];
        SkASSERT(count >= 0);
        if (count == 0) {
            return;
        }

        if (antialias[index] == 0xFF) {
            fBWProc(device + index, fSrc, count);
        } else if (antialias[index] != 0) {
            fAAProc(device + index, fSrc, count, antialias[index]);
        }

        index += count;
    }
}

void SkA8_Blitter::blitH(int x, int y, int width) {
    fBWProc(fDevice.writable_addr8(x, y), fSrc, width);
}

void SkA8_Blitter::blitV(int x, int y, int height, SkAlpha aa) {
    if (aa == 0xFF) {
        for (int row = y; row < y + height; ++row) {
            uint8_t* device = fDevice.writable_addr8(x, row);
            *device = fOneProc(fSrc, *device);
        }
    } else if (aa != 0) {
        for (int row = y; row < y + height; ++row) {
            fAAProc(fDevice.writable_addr8(x, row), fSrc, 1, aa);
        }
    }
}

void SkA8_Blitter::blitRect(int x, int y, int width, int height) {
    for (int row = y; row < y + height; ++row) {
        fBWProc(fDevice.writable_addr8(x, row), fSrc, width);
    }
}

void SkA8_Blitter::blitMask(const SkMask& mask, const SkIRect& clip) {
    if (SkMask::kA8_Format != mask.fFormat) {
        (*this).INHERITED::blitMask(mask, clip);
        return;
    }

    const int x = clip.fLeft;
    const int width = clip.width();

    for (int y = clip.fTop; y < clip.fBottom; ++y) {
        uint8_t* dst = fDevice.writable_addr8(x, y);
        const uint8_t* src = mask.getAddr8(x, y);
        for (int i = 0; i < width; ++i) {
            dst[i] = static_cast<uint8_t>(u8_lerp(dst[i], fOneProc(fSrc, dst[i]), src[i]));
        }
    }
}

//////////////////

SkBlitter* SkChooseA8Blitter(const SkPixmap& dst,
                             const SkMatrix& ctm,
                             const SkPaint& paint,
                             SkArenaAlloc* alloc,
                             SkDrawCoverage drawCoverage,
                             sk_sp<SkShader> clipShader) {
    if (dst.colorType() != SkColorType::kAlpha_8_SkColorType) {
        return nullptr;
    }
    if (paint.getShader() || paint.getColorFilter()) {
        return nullptr;
    }
    if (clipShader) {
        return nullptr; // would not be hard to support ...?
    }

    if (drawCoverage == SkDrawCoverage::kYes) {
        return (*alloc).make<SkA8_Coverage_Blitter>(dst, paint);
    }
    // we only support certain blendmodes...
    const std::optional<SkBlendMode> mode = paint.asBlendMode();
    if (mode.has_value() && find_a8_rowproc_pair(*mode)) {
        return (*alloc).make<SkA8_Blitter>(dst, paint);
    }
    return nullptr;
}

SkBlitter* SkA8Blitter_Choose(const SkPixmap& dst,
                              const SkMatrix& ctm,
                              const SkPaint& paint,
                              SkArenaAlloc* alloc,
                              SkDrawCoverage coverage,
                              sk_sp<SkShader> clipShader,
                              const SkSurfaceProps&,
                              const SkRect&) {
    SkBlitter* blitter = SkChooseA8Blitter(dst, ctm, paint, alloc, coverage, std::move(clipShader));
    return blitter;
}
