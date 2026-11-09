// SkCanvas's save stack. The contract checks that restore, restoreToCount and restoreAll
// unwind exactly the saves made, that restoreAll is the named reset to the base state, and
// that restoreToCount accepts every count from 1 to the current save count. Each stop mode
// unbalances the stack, which a Release check must stop:
//   --restore-empty     restore() with no save to undo;
//   --restore-to-zero   restoreToCount(0), below the base state;
//   --restore-to-above  restoreToCount(n) above the current save count.

#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkSurface.h"

#include <cstdio>
#include <cstring>

namespace {

sk_sp<SkSurface> make_surface() {
    const SkImageInfo info = SkImageInfo::MakeN32Premul(8, 8);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    return surface;
}

bool contract() {
    sk_sp<SkSurface> surface = make_surface();
    if (!surface) return false;
    SkCanvas& canvas = *(*surface).getCanvas();
    if (canvas.getSaveCount() != 1) return false;
    // restoreAll on the base state changes nothing.
    canvas.restoreAll();
    if (canvas.getSaveCount() != 1 || !canvas.getTotalMatrix().isIdentity()) return false;
    for (int depth = 1; depth <= 5; ++depth) {
        canvas.save();
        canvas.translate(1.0f, 2.0f);
        if (canvas.getSaveCount() != depth + 1) return false;
    }
    canvas.restore();
    if (canvas.getSaveCount() != 5) return false;
    if (canvas.getTotalMatrix() != SkMatrix::Translate(4.0f, 8.0f)) return false;
    // Restoring to the current count is allowed and changes nothing.
    canvas.restoreToCount(5);
    if (canvas.getSaveCount() != 5) return false;
    canvas.restoreToCount(3);
    if (canvas.getSaveCount() != 3 || canvas.getTotalMatrix() != SkMatrix::Translate(2.0f, 4.0f)) {
        return false;
    }
    canvas.restoreAll();
    if (canvas.getSaveCount() != 1 || !canvas.getTotalMatrix().isIdentity()) return false;
    // A save deferred until the first change is restored as well.
    canvas.save();
    canvas.save();
    canvas.restoreToCount(1);
    if (canvas.getSaveCount() != 1) return false;
    std::puts("canvas restore: saves unwind exactly; restoreAll resets to the base state");
    return true;
}

// Stops in a Release check; returning means the stack was not checked.
int stop_mode(const char* mode) {
    sk_sp<SkSurface> surface = make_surface();
    if (!surface) return 2;
    SkCanvas& canvas = *(*surface).getCanvas();
    if (std::strcmp(mode, "--restore-empty") == 0) {
        canvas.restore();
    } else if (std::strcmp(mode, "--restore-to-zero") == 0) {
        canvas.save();
        canvas.restoreToCount(0);
    } else if (std::strcmp(mode, "--restore-to-above") == 0) {
        canvas.save();
        canvas.restoreToCount(3);
    } else {
        return 2;
    }
    std::printf("the canvas accepted an unbalanced restore (save count %d)\n", canvas.getSaveCount());
    return 3;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2) return stop_mode(argv[1]);
    if (argc != 1) return 2;
    if (!contract()) return 1;
    return 0;
}
