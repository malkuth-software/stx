// SkUnPreMultiply's scale table. The contract checks that GetScale reads the table for every
// alpha and that ApplyScale undoes premultiplication to the nearest level within one. The
// stop mode asks for the scale of alpha 256, past the 256-entry table, which GetScale's
// Release bound must stop.

#include "include/core/SkColor.h"
#include "include/core/SkUnPreMultiply.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

bool contract() {
    const SkUnPreMultiply::Scale* const table = SkUnPreMultiply::GetScaleTable();
    double worst = 0.0;
    for (unsigned alpha = 0; alpha <= 255; ++alpha) {
        const SkUnPreMultiply::Scale scale = SkUnPreMultiply::GetScale(alpha);
        if (scale != table[alpha]) return false;
        if (alpha == 0) continue;
        for (unsigned component = 0; component <= alpha; ++component) {
            const U8CPU restored = SkUnPreMultiply::ApplyScale(scale, component);
            const double model = static_cast<double>(component) * 255.0 / static_cast<double>(alpha);
            const double error = std::abs(static_cast<double>(restored) - model);
            if (error > 1.0) return false;
            if (error > worst) worst = error;
        }
    }
    std::printf("unpremultiply: every alpha reads its table entry; worst error %.3f level(s) (bound 1)\n",
                worst);
    return true;
}

// Stops in GetScale's Release bound; returning means the bound did not hold.
int stop_mode(const char* mode) {
    if (std::strcmp(mode, "--alpha-256") != 0) return 2;
    const SkUnPreMultiply::Scale scale = SkUnPreMultiply::GetScale(256);
    std::printf("GetScale(256) returned %u\n", static_cast<unsigned>(scale));
    return 3;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2) return stop_mode(argv[1]);
    if (argc != 1) return 2;
    if (!contract()) return 1;
    return 0;
}
