// Exact picking: Newton's method on S(u, v) - origin - t direction = 0.

#include <stx_raster3d/picking.hpp>

#include <stx_conv/surface.hpp>

#include <array>
#include <cmath>
#include <cstddef>

namespace stx_raster3d {

namespace {

constexpr std::size_t newton_steps = 12U;
// How far (node units, each way) the solve may move from its seed.
constexpr double neighbourhood = 2.0;
constexpr double tolerance = 1e-10;

double determinant(Vec3 a, Vec3 b, Vec3 c) noexcept {
    const double value = dot(a, cross(b, c));
    return value;
}

} // namespace

Status refine_surface_pick(const stx_conv::surface::Surface& surface, const View& view, double x, double y, double& u,
                           double& v, Vec3& point) noexcept {
    if (!surface.ready() || surface.shape().channels < 3U) return Status::invalid_argument;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(u) || !std::isfinite(v)) return Status::nonfinite_input;
    Vec3 origin{};
    Vec3 direction{};
    view.ray(x, y, origin, direction);
    const bool periodic = surface.shape().periodic_u;
    const double extent_u = static_cast<double>(surface.cells_u());
    const double extent_v = static_cast<double>(surface.cells_v());
    stx_conv::surface::SurfaceSample sample;
    if (surface.evaluate_with_derivatives(u, v, sample) != stx_conv::Status::ok) return Status::invalid_argument;
    Vec3 at{sample.value[0], sample.value[1], sample.value[2]};
    if (!finite(at)) return Status::nonfinite_input;
    const double scale = 1.0 + length(at);
    double current_u = u;
    double current_v = v;
    double t = dot(at - origin, direction);
    for (std::size_t step = 0U; step < newton_steps; ++step) {
        const Vec3 residual = at - origin - direction * t;
        if (length(residual) <= tolerance * scale) break;
        const Vec3 along_u{sample.along_u[0], sample.along_u[1], sample.along_u[2]};
        const Vec3 along_v{sample.along_v[0], sample.along_v[1], sample.along_v[2]};
        const Vec3 against = direction * -1.0;
        // J [du dv dt] = -residual, J = [S_u S_v -d], by Cramer's rule.
        const double jacobian = determinant(along_u, along_v, against);
        if (!(std::fabs(jacobian) > 0.0) || !std::isfinite(jacobian)) return Status::too_large;
        const Vec3 target = residual * -1.0;
        const double du = determinant(target, along_v, against) / jacobian;
        const double dv = determinant(along_u, target, against) / jacobian;
        const double dt = determinant(along_u, along_v, target) / jacobian;
        current_u = current_u + du;
        current_v = current_v + dv;
        t = t + dt;
        if (!std::isfinite(current_u) || !std::isfinite(current_v) || !std::isfinite(t)) return Status::too_large;
        if (std::fabs(current_u - u) > neighbourhood || std::fabs(current_v - v) > neighbourhood) return Status::too_large;
        if (current_v < 0.0 || current_v > extent_v) return Status::too_large;
        if (!periodic && (current_u < 0.0 || current_u > extent_u)) return Status::too_large;
        if (surface.evaluate_with_derivatives(current_u, current_v, sample) != stx_conv::Status::ok) {
            return Status::too_large;
        }
        at = Vec3{sample.value[0], sample.value[1], sample.value[2]};
    }
    const Vec3 residual = at - origin - direction * t;
    if (length(residual) > tolerance * scale) return Status::too_large;
    // A periodic hit is reported inside [0, extent_u).
    double wrapped = current_u;
    if (periodic) wrapped = wrapped - std::floor(wrapped / extent_u) * extent_u;
    u = wrapped;
    v = current_v;
    point = at;
    return Status::ok;
}

} // namespace stx_raster3d
