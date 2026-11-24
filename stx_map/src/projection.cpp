// Equirectangular and Web Mercator projections (projection.hpp).

#include "stx_map/projection.hpp"

#include "stx_map/map.hpp"

#include <cmath>
#include <cstdint>

namespace stx_map {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double mercator_latitude_limit = 85.05112878;

} // namespace

WorldPoint project(Projection projection, std::int32_t x_units, std::int32_t y_units) noexcept {
    const double per_degree = static_cast<double>(units_per_degree);
    const double longitude_share = static_cast<double>(x_units) / (360.0 * per_degree);
    const double latitude = static_cast<double>(y_units) / per_degree - 90.0;
    WorldPoint point;
    point.x = longitude_share;
    switch (projection) {
    case Projection::equirectangular:
        point.y = (90.0 - latitude) / 180.0;
        break;
    case Projection::web_mercator: {
        const double clamped = std::fmax(-mercator_latitude_limit, std::fmin(mercator_latitude_limit, latitude));
        const double radians = clamped * pi / 180.0;
        const double stretched = std::log(std::tan(pi / 4.0 + radians / 2.0));
        point.y = 0.5 - stretched / (2.0 * pi);
        break;
    }
    default:
        // An enumerator this function does not know: equirectangular.
        point.y = (90.0 - latitude) / 180.0;
        break;
    }
    return point;
}

} // namespace stx_map
