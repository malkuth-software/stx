#pragma once
// Exact picking on a CONV surface: the mesh says where a ray lands, and
// Newton's method on the surface itself puts the hit on it (the conv-teapot
// seeded solve).

#include <stx_raster3d/camera.hpp>
#include <stx_raster3d/geometry.hpp>
#include <stx_raster3d/memory.hpp>

namespace stx_conv::surface {
class Surface;
}

namespace stx_raster3d {

// Solves S(u, v) = origin + t direction for the ray through picture position
// (x, y), from the seed (u, v) (a Pick's), on a ready surface of three or
// more channels. On success u, v and point hold the hit (within 1e-10 of
// the ray, relative to the surface's size); on failure (invalid_argument
// for an unprepared or narrow surface, nonfinite_input for a non-finite
// seed or position, too_large when Newton does not converge within twelve
// steps or leaves the seed's neighbourhood) they are unchanged.
[[nodiscard]] Status refine_surface_pick(const stx_conv::surface::Surface& surface, const View& view, double x,
                                         double y, double& u, double& v, Vec3& point) noexcept;

} // namespace stx_raster3d
