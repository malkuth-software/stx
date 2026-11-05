#pragma once
// peekPixels returns std::optional<SkPixmap> in the candidate and fills an out-parameter in the
// reference profile; peek_pixels gives the optional either way, so a test that reads pixels
// compiles against both.

#include "include/core/SkPixmap.h"

#include <optional>

template <typename Source>
std::optional<SkPixmap> peek_pixels(Source& source) {
    if constexpr (requires(SkPixmap* pixmap) { source.peekPixels(pixmap); }) {
        SkPixmap pixmap;
        if (!source.peekPixels(&pixmap)) return std::nullopt;
        return pixmap;
    } else {
        std::optional<SkPixmap> pixmap = source.peekPixels();
        return pixmap;
    }
}
