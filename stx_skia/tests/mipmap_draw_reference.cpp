// The pinned optional implementation predates these two CPU renderer interfaces.
// Adapt names/signature only; include its unchanged algorithm below.
#include "src/core/SkDraw.h"
#include "src/core/SkMipmap.h"

class SkDraw : public skcpu::Draw {
public:
    void drawBitmap(const SkBitmap& bitmap, const SkMatrix& matrix, const SkRect* bounds,
                    const SkSamplingOptions& sampling, const SkPaint& paint) const {
        skcpu::Draw::drawBitmap(bitmap, matrix, bounds, sampling, paint, nullptr);
    }
};

// The reference profile puts the pin's baseline/skia first on the include path, so this
// resolves to the retained original rather than the candidate's skia/ copy.
#include "src/core/SkMipmapDrawDownSampler.cpp"
