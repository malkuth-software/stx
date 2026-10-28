// Encoded images are deliberately outside the stx_skia pixel-only boundary.
// This compatibility entry point is also referenced by upstream deserialization.
#include "include/core/SkAlphaType.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include <optional>

namespace SkImages {
sk_sp<SkImage> DeferredFromEncodedData(sk_sp<const SkData>,
                                      std::optional<SkAlphaType>) {
    return nullptr;
}
}
