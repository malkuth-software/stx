#pragma once
// Projections of map positions onto a unit world, as declared choices. The
// result's x runs 0 to 1 from 180 degrees west eastward and y 0 to 1 from
// the top (north) down. Equirectangular maps 90 north to 90 south onto
// y 0 to 1 (its world is twice as wide as tall: scale y by half the width);
// Web Mercator is square, latitudes clamped to +-85.05112878 degrees.
// Binary64 arithmetic; nothing here takes part in decoding.

#include <cstdint>

namespace stx_map {

enum class Projection : std::uint8_t {
    equirectangular,
    web_mercator
};

struct WorldPoint final {
    double x = 0.0;
    double y = 0.0;
};

[[nodiscard]] WorldPoint project(Projection projection, std::int32_t x_units, std::int32_t y_units) noexcept;

} // namespace stx_map
