#pragma once
// Points, directions and colours for stx_raster3d. Geometry is binary64;
// colours are straight (not premultiplied) binary32 components in [0, 1].

#include <cmath>

namespace stx_raster3d {

struct Vec3 final {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) noexcept {
    const Vec3 sum{a.x + b.x, a.y + b.y, a.z + b.z};
    return sum;
}
inline Vec3 operator-(Vec3 a, Vec3 b) noexcept {
    const Vec3 difference{a.x - b.x, a.y - b.y, a.z - b.z};
    return difference;
}
inline Vec3 operator*(Vec3 a, double k) noexcept {
    const Vec3 scaled{a.x * k, a.y * k, a.z * k};
    return scaled;
}
inline double dot(Vec3 a, Vec3 b) noexcept {
    const double product = a.x * b.x + a.y * b.y + a.z * b.z;
    return product;
}
inline Vec3 cross(Vec3 a, Vec3 b) noexcept {
    const Vec3 product{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    return product;
}
inline double length(Vec3 a) noexcept {
    const double magnitude = std::sqrt(dot(a, a));
    return magnitude;
}
// The unit vector along a; the zero vector stays zero.
inline Vec3 normalized(Vec3 a) noexcept {
    const double magnitude = length(a);
    if (!(magnitude > 0.0)) return Vec3{};
    const Vec3 unit = a * (1.0 / magnitude);
    return unit;
}
inline bool finite(Vec3 a) noexcept { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

struct Color final {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

} // namespace stx_raster3d
