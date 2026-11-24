// Cameras, the matplotlib-convention orbit and the fitted view.

#include <stx_raster3d/camera.hpp>

#include <cmath>
#include <cstddef>

namespace stx_raster3d {

namespace {

constexpr double degrees_to_radians = 3.14159265358979323846 / 180.0;

// v turned by `angle` about the unit axis (Rodrigues).
Vec3 turned_about(Vec3 v, Vec3 axis, double angle) noexcept {
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const Vec3 along = axis * (dot(axis, v) * (1.0 - c));
    const Vec3 turned = v * c + cross(axis, v) * s + along;
    return turned;
}

} // namespace

Camera orbit_camera(Vec3 target, double distance, double elevation_degrees, double azimuth_degrees,
                    double roll_degrees, Projection projection) noexcept {
    const double elevation = elevation_degrees * degrees_to_radians;
    const double azimuth = azimuth_degrees * degrees_to_radians;
    const double ce = std::cos(elevation);
    const double se = std::sin(elevation);
    const double ca = std::cos(azimuth);
    const double sa = std::sin(azimuth);
    const Vec3 outward{ce * ca, ce * sa, se};
    Camera camera;
    camera.target = target;
    camera.eye = target + outward * distance;
    camera.projection = projection;
    // Up is the world's z tilted back with the eye: the derivative of the
    // outward direction with respect to elevation. It is never along the
    // view, so the poles need no special case.
    Vec3 up{-se * ca, -se * sa, ce};
    const Vec3 forward = normalized(outward * -1.0);
    up = turned_about(up, forward, -roll_degrees * degrees_to_radians);
    camera.up = up;
    return camera;
}

Status make_view(const Camera& camera, std::size_t width, std::size_t height, View& view) noexcept {
    if (!finite(camera.eye) || !finite(camera.target) || !finite(camera.up)) return Status::nonfinite_input;
    if (!std::isfinite(camera.focal_length) || !std::isfinite(camera.orthographic_height) || !std::isfinite(camera.near)) {
        return Status::nonfinite_input;
    }
    if (width == 0U || height == 0U) return Status::invalid_argument;
    if (!(camera.focal_length > 0.0) || !(camera.orthographic_height > 0.0) || !(camera.near > 0.0)) {
        return Status::invalid_argument;
    }
    // An eye-to-target difference that overflows normalizes to NaN, which
    // compares unequal to zero; only a finite, non-zero basis is accepted.
    const Vec3 forward = normalized(camera.target - camera.eye);
    if (!finite(forward) || !(length(forward) > 0.0)) return Status::invalid_argument;
    const Vec3 right = normalized(cross(forward, camera.up));
    if (!finite(right) || !(length(right) > 0.0)) return Status::invalid_argument;
    const Vec3 up = cross(right, forward);
    View fitted;
    fitted.eye_ = camera.eye;
    fitted.forward_ = forward;
    fitted.right_ = right;
    fitted.up_ = up;
    fitted.width_ = width;
    fitted.height_ = height;
    fitted.projection_ = camera.projection;
    fitted.near_ = camera.near;
    const double half_height = 0.5 * static_cast<double>(height);
    // Perspective: tan(fov / 2) = 1 / focal_length, so a unit of height at
    // depth 1 spans half_height * focal_length pixels.
    fitted.scale_ = camera.projection == Projection::perspective ? half_height * camera.focal_length
                                                                 : half_height / (0.5 * camera.orthographic_height);
    view = fitted;
    return Status::ok;
}

ProjectedPoint View::project(Vec3 point) const noexcept {
    const Vec3 offset = point - eye_;
    ProjectedPoint projected;
    projected.depth = dot(offset, forward_);
    double factor = scale_;
    if (projection_ == Projection::perspective) {
        const double depth = projected.depth > 1e-12 ? projected.depth : 1e-12;
        factor = scale_ / depth;
    }
    projected.x = 0.5 * static_cast<double>(width_) + dot(offset, right_) * factor;
    projected.y = 0.5 * static_cast<double>(height_) - dot(offset, up_) * factor;
    return projected;
}

void View::ray(double x, double y, Vec3& origin, Vec3& direction) const noexcept {
    const double across = (x - 0.5 * static_cast<double>(width_)) / scale_;
    const double down = (0.5 * static_cast<double>(height_) - y) / scale_;
    if (projection_ == Projection::perspective) {
        origin = eye_;
        direction = forward_ + right_ * across + up_ * down;
        return;
    }
    origin = eye_ + right_ * across + up_ * down;
    direction = forward_;
}

} // namespace stx_raster3d
