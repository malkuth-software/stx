#pragma once
// Cameras and the view they give a picture of a given size.
//
// A camera looks from `eye` at `target` with `up` as the picture's upward
// direction. Perspective projection follows matplotlib's focal length: the
// vertical field of view is 2 atan(1 / focal_length), so the default 1 is 90
// degrees. Orthographic projection shows `orthographic_height` world units
// over the picture's height. The renderer clips everything to the view
// frustum: what lies nearer the eye than `near` along the view direction
// (behind the eye included) or outside the picture is neither drawn nor
// picked.

#include <stx_raster3d/geometry.hpp>
#include <stx_raster3d/memory.hpp>

#include <cstddef>
#include <cstdint>

namespace stx_raster3d {

enum class Projection : std::uint8_t { perspective, orthographic };

struct Camera final {
    Vec3 eye{1.0, -1.0, 1.0};
    Vec3 target{};
    Vec3 up{0.0, 0.0, 1.0};
    Projection projection = Projection::perspective;
    double focal_length = 1.0;
    double orthographic_height = 2.0;
    double near = 1e-3;
};

// An orbit about `target` in matplotlib's convention (z up): elevation and
// azimuth in degrees give the eye's direction from the target as
// (cos e cos a, cos e sin a, sin e); roll turns the picture about the view
// direction, counter-clockwise as seen. At the poles the up direction is
// the azimuth's own, so the orbit passes over them without a flip.
[[nodiscard]] Camera orbit_camera(Vec3 target, double distance, double elevation_degrees, double azimuth_degrees,
                                  double roll_degrees, Projection projection) noexcept;

// A projected point: picture position in pixels (x right, y down, pixel
// centres at k + 0.5) and depth along the view direction.
struct ProjectedPoint final {
    double x = 0.0;
    double y = 0.0;
    double depth = 0.0;
};

// A camera fitted to a picture. Built only through make_view, which refuses
// a degenerate camera or picture.
class View final {
public:
    View() noexcept = default;
    [[nodiscard]] ProjectedPoint project(Vec3 point) const noexcept;
    // The ray through picture position (x, y): its origin and its direction
    // (unit length along the view direction, so the ray parameter is depth).
    void ray(double x, double y, Vec3& origin, Vec3& direction) const noexcept;
    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t height() const noexcept { return height_; }
    [[nodiscard]] Projection projection() const noexcept { return projection_; }
    [[nodiscard]] double near() const noexcept { return near_; }
    [[nodiscard]] Vec3 eye() const noexcept { return eye_; }
    [[nodiscard]] Vec3 forward() const noexcept { return forward_; }
    [[nodiscard]] Vec3 right() const noexcept { return right_; }
    [[nodiscard]] Vec3 upward() const noexcept { return up_; }
    // Pixels per world unit at depth 1 (perspective) or everywhere
    // (orthographic).
    [[nodiscard]] double scale() const noexcept { return scale_; }

private:
    friend Status make_view(const Camera& camera, std::size_t width, std::size_t height, View& view) noexcept;
    Vec3 eye_{};
    Vec3 forward_{0.0, 1.0, 0.0};
    Vec3 right_{1.0, 0.0, 0.0};
    Vec3 up_{0.0, 0.0, 1.0};
    double scale_ = 1.0;
    double near_ = 1e-3;
    std::size_t width_ = 0U;
    std::size_t height_ = 0U;
    Projection projection_ = Projection::perspective;
};

// Fits a camera to a width by height picture. invalid_argument for an empty
// picture, an eye on the target, an up direction along the view, or a focal
// length, height or near distance that is not positive; nonfinite_input for
// a coordinate that is not finite. `view` is written only on success.
[[nodiscard]] Status make_view(const Camera& camera, std::size_t width, std::size_t height, View& view) noexcept;

} // namespace stx_raster3d
