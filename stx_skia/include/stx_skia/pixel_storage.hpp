#pragma once
#include "include/core/SkData.h"
#include <cstddef>
#include <vector>

namespace stx_skia {
// Transfers the vector allocation into immutable SkData without copying pixels.
// Success leaves pixels empty. The allocation lives until the final SkData or
// shared subset releases it. A null result leaves pixels unchanged. Callers must
// surrender writable aliases before publishing the returned immutable storage.
[[nodiscard]] sk_sp<SkData> take_pixel_data(std::vector<std::byte>& pixels);
}
