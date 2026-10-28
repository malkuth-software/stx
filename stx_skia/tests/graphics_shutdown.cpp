// SkGraphics::Shutdown(), the explicit end of stx_skia's process lifetime. The contract uses every
// cache and singleton Shutdown() frees (the resource cache through a blurred draw and a scaled
// image, the CPU recorder through a surface, the empty data, table and path data, the error
// path data, the sRGB and linear sRGB color spaces and both gamma filters), keeps a path and a
// color space alive across Shutdown(), and releases them after it: an object that shares a
// singleton only drops its own reference. A sanitized build's leak check then sees the clean exit,
// since Shutdown() empties every slot it frees, so anything it missed would be unreachable.
//
// Each stop mode uses stx_skia after Shutdown(), which a Release check must stop:
//   --data-after     SkData::MakeEmpty();
//   --cache-after    a resource-cache query;
//   --surface-after  a new raster surface (the recorder and the color space);
//   --twice          a second Shutdown().

#include "include/core/SkBlurTypes.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkDataTable.h"
#include "include/core/SkGraphics.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSurface.h"
#include "blur_result.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// Draws through every cache and singleton that Shutdown() frees; returns false on a wrong
// result. Every object it makes is gone when it returns.
bool use_everything() {
    const SkImageInfo info = SkImageInfo::MakeN32Premul(64, 64, SkColorSpace::MakeSRGB());
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();

    SkPaint blurred;
    blurred.setMaskFilter(filter_of(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, 3.0f)));
    canvas.drawRect(SkRect::MakeXYWH(8.0f, 8.0f, 24.0f, 24.0f), blurred);

    SkPaint gamma;
    gamma.setColorFilter(SkColorFilters::LinearToSRGBGamma());
    canvas.drawRect(SkRect::MakeXYWH(0.0f, 40.0f, 8.0f, 8.0f), gamma);
    gamma.setColorFilter(SkColorFilters::SRGBToLinearGamma());
    canvas.drawRect(SkRect::MakeXYWH(8.0f, 40.0f, 8.0f, 8.0f), gamma);

    // A scaled, mipmapped draw puts the image's levels in the resource cache.
    sk_sp<SkImage> snapshot = (*surface).makeImageSnapshot();
    if (!snapshot) return false;
    const SkSamplingOptions mipmapped(SkFilterMode::kLinear, SkMipmapMode::kLinear);
    canvas.save();
    canvas.scale(0.25f, 0.25f);
    canvas.drawImage(snapshot, 0.0f, 0.0f, mipmapped);
    canvas.restore();

    const SkPath empty;
    const float nan = std::nanf("");
    SkPathBuilder builder;
    builder.moveTo(0.0f, 0.0f);
    builder.lineTo(nan, 1.0f);
    const SkPath error = builder.detach();
    if (!empty.isEmpty() || error.isFinite()) return false;

    const sk_sp<SkData> emptyData = SkData::MakeEmpty();
    const sk_sp<SkDataTable> emptyTable = SkDataTable::MakeEmpty();
    if ((*emptyData).size() != 0 || (*emptyTable).count() != 0) return false;
    if (SkColorSpace::MakeSRGBLinear() == nullptr) return false;
    std::printf("graphics shutdown: resource cache holds %zu bytes before shutdown\n",
                SkGraphics::GetResourceCacheTotalBytesUsed());
    return true;
}

bool contract() {
    if (!use_everything()) return false;
    // Objects that share a singleton may outlive Shutdown(); releasing them afterwards only
    // drops their own references (a sanitized build would report a use of freed memory).
    {
        const sk_sp<SkColorSpace> keptSpace = SkColorSpace::MakeSRGB();
        const SkPath keptPath = SkPath::Rect(SkRect::MakeXYWH(1.0f, 2.0f, 3.0f, 4.0f));
        const SkPath keptEmpty;
        SkGraphics::Shutdown();
        if (keptPath.getBounds() != SkRect::MakeXYWH(1.0f, 2.0f, 3.0f, 4.0f)) return false;
    }
    std::puts("graphics shutdown: every cache and singleton released; shared objects outlive it");
    return true;
}

// Stops in a Release check; returning means the use after shutdown was not caught.
int stop_mode(const char* mode) {
    if (!use_everything()) return 2;
    SkGraphics::Shutdown();
    if (std::strcmp(mode, "--data-after") == 0) {
        const sk_sp<SkData> data = SkData::MakeEmpty();
        std::printf("made empty data after shutdown (%zu bytes)\n", (*data).size());
    } else if (std::strcmp(mode, "--cache-after") == 0) {
        std::printf("queried the cache after shutdown (%zu bytes)\n",
                    SkGraphics::GetResourceCacheTotalBytesUsed());
    } else if (std::strcmp(mode, "--surface-after") == 0) {
        const sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(4, 4));
        std::printf("made a surface after shutdown (%s)\n", surface ? "made" : "refused");
    } else if (std::strcmp(mode, "--twice") == 0) {
        SkGraphics::Shutdown();
        std::puts("shut down twice");
    } else {
        return 2;
    }
    return 3;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2) return stop_mode(argv[1]);
    if (argc != 1) return 2;
    if (!contract()) {
        std::puts("graphics shutdown: a draw before shutdown gave the wrong result");
        return 1;
    }
    return 0;
}
