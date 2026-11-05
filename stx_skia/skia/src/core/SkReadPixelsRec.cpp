/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/core/SkReadPixelsRec.h"

#include "include/core/SkRect.h"
#include "src/core/SkSafeMath.h"

bool SkReadPixelsRec::trim(int srcWidth, int srcHeight) {
    // fInfo.minRowBytes() returns 0 if the size doesn't fit in `size_t`.
    const size_t minRowBytes = fInfo.minRowBytes();
    if (fPixels == nullptr || fRowBytes < minRowBytes || minRowBytes == 0) {
        return false;
    }
    if (fInfo.width() <= 0 || fInfo.height() <= 0) {
        return false;
    }
    // negating the largest negative integer (below) is UB
    if (fX == INT_MIN || fY == INT_MIN) {
        return false;
    }

    int x = fX;
    int y = fY;
    SkIRect srcR = SkIRect::MakeXYWH(x, y, fInfo.width(), fInfo.height());
    if (!srcR.intersect({0, 0, srcWidth, srcHeight})) {
        return false;
    }

    // if x or y are negative, then we have to adjust pixels
    if (x > 0) {
        x = 0;
    }
    if (y > 0) {
        y = 0;
    }
    // x and y are nonpositive and cannot be INT_MIN. Convert their magnitudes
    // to byte offsets before changing the pointer or the published rectangle.
    SkSafeMath safeMath;
    const size_t skipped_rows = static_cast<size_t>(-y);
    const size_t skipped_columns = static_cast<size_t>(-x);
    const size_t pixel_bytes = static_cast<size_t>(fInfo.bytesPerPixel());
    const size_t y_offset = safeMath.mul(skipped_rows, fRowBytes);
    const size_t x_offset = safeMath.mul(skipped_columns, pixel_bytes);
    const size_t total = safeMath.add(y_offset, x_offset);
    if (!safeMath.ok()) {
        return false;
    }

    char* pixels = static_cast<char*>(fPixels);
    fPixels = pixels + total;
    // the intersect may have shrunk info's logical size
    fInfo = fInfo.makeDimensions(srcR.size());
    fX = srcR.x();
    fY = srcR.y();

    return true;
}
