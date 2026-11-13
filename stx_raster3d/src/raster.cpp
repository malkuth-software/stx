// The visibility-buffer renderer (raster.hpp): primitives in sample space,
// binned by bands of rows, rasterised and resolved one band per task. The
// edge functions, fill rule and perspective-correct weights follow
// conv-teapot's CPU rasteriser (PROVENANCE.md).

#include <stx_raster3d/raster.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace stx_raster3d {

namespace {

// Fixed point: 8 bits below one sample.
constexpr std::int64_t sub_sample = 256;
constexpr double sub_sample_scale = 256.0;
// Corners farther out than this (fixed units, 2^22 samples) are not drawn,
// so every edge-function product stays inside 64 bits. Clipping keeps every
// corner within a few pixels of the picture, so this is only a backstop.
constexpr double guard = 1073741824.0;
// Pixel rows per band: every band is one task, rasterised and resolved.
constexpr std::size_t band_pixel_rows = 8U;
// Lines and markers are drawn this much nearer than a surface at the same
// depth, so they win against the surface they lie on.
constexpr float overlay_bias = 1.0015f;
constexpr std::uint32_t no_primitive = 0xffffffffU;

// What a primitive came from.
constexpr std::uint8_t kind_mesh = 0U;
constexpr std::uint8_t kind_line = 1U;
constexpr std::uint8_t kind_marker = 2U;
constexpr std::uint8_t kind_mask = 0x0fU;
// Clipping keeps this many pixels beyond the picture's sides, so a clipped
// edge never shows; wide lines keep their width more.
constexpr double clip_margin = 2.0;
// The source weights of an unclipped triangle's corners.
constexpr std::array<float, 6> identity_weights{0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};

// Unit directions every 22.5 degrees, for circle markers (written out so
// every platform uses the same numbers).
constexpr std::array<double, 16> circle_x{1.0,
                                          0.92387953251128674,
                                          0.70710678118654757,
                                          0.38268343236508978,
                                          0.0,
                                          -0.38268343236508978,
                                          -0.70710678118654757,
                                          -0.92387953251128674,
                                          -1.0,
                                          -0.92387953251128674,
                                          -0.70710678118654757,
                                          -0.38268343236508978,
                                          0.0,
                                          0.38268343236508978,
                                          0.70710678118654757,
                                          0.92387953251128674};
constexpr std::array<double, 16> circle_y{0.0,
                                          0.38268343236508978,
                                          0.70710678118654757,
                                          0.92387953251128674,
                                          1.0,
                                          0.92387953251128674,
                                          0.70710678118654757,
                                          0.38268343236508978,
                                          0.0,
                                          -0.38268343236508978,
                                          -0.70710678118654757,
                                          -0.92387953251128674,
                                          -1.0,
                                          -0.92387953251128674,
                                          -0.70710678118654757,
                                          -0.38268343236508978};
constexpr std::size_t marker_pieces_most = 16U;

std::int64_t floor_divide(std::int64_t numerator, std::int64_t denominator) noexcept {
    std::int64_t quotient = numerator / denominator;
    if ((numerator % denominator != 0) && ((numerator < 0) != (denominator < 0))) quotient = quotient - 1;
    return quotient;
}

bool color_valid(const Color& color) noexcept {
    const bool valid = std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b) && std::isfinite(color.a);
    return valid;
}

double clamp_unit(double value) noexcept {
    const double clamped = std::min(1.0, std::max(0.0, value));
    return clamped;
}

std::uint8_t to_byte(double value) noexcept {
    const double scaled = std::floor(clamp_unit(value) * 255.0 + 0.5);
    return static_cast<std::uint8_t>(scaled);
}

// A premultiplied colour while shading.
struct Shade final {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 0.0;
};

Shade premultiplied(const Color& color) noexcept {
    const double alpha = clamp_unit(static_cast<double>(color.a));
    const Shade shade{clamp_unit(static_cast<double>(color.r)) * alpha, clamp_unit(static_cast<double>(color.g)) * alpha,
                      clamp_unit(static_cast<double>(color.b)) * alpha, alpha};
    return shade;
}

std::size_t marker_pieces(MarkerShape shape) noexcept {
    switch (shape) {
    case MarkerShape::circle:
        return 16U;
    case MarkerShape::square:
    case MarkerShape::diamond:
        return 2U;
    case MarkerShape::triangle_up:
        return 1U;
    case MarkerShape::plus:
    case MarkerShape::cross:
        return 4U;
    default:
        return 0U;
    }
}

bool material_valid(const Material& material) noexcept {
    if (!color_valid(material.color) || !color_valid(material.iso.color)) return false;
    if (!std::isfinite(material.value_minimum) || !std::isfinite(material.value_maximum)) return false;
    if (!finite(material.lighting.direction)) return false;
    const Lighting& light = material.lighting;
    if (!std::isfinite(light.ambient) || !std::isfinite(light.diffuse) || !std::isfinite(light.specular) ||
        !std::isfinite(light.shininess)) {
        return false;
    }
    const IsoLines& iso = material.iso;
    if (!std::isfinite(iso.u_step) || !std::isfinite(iso.v_step) || iso.u_step < 0.0 || iso.v_step < 0.0) return false;
    if (!std::isfinite(iso.u_origin) || !std::isfinite(iso.v_origin)) return false;
    if (!std::isfinite(iso.width) || iso.width < 0.0f) return false;
    for (const double level : iso.value_levels) {
        if (!std::isfinite(level)) return false;
    }
    for (const Color& entry : material.colormap) {
        if (!color_valid(entry)) return false;
    }
    return true;
}

// Coverage of an anti-aliased iso line `width` pixels wide at `distance`
// pixels from the pixel centre.
double line_coverage(double distance, double width) noexcept {
    const double coverage = clamp_unit(width * 0.5 + 0.5 - distance);
    return coverage;
}

// The coverage of the iso lines every `step` (from `origin`) for an
// attribute with value `a` and pixel gradient length `gradient`.
double step_coverage(double a, double origin, double step, double gradient, double width) noexcept {
    if (!(step > 0.0) || !(gradient > 0.0)) return 0.0;
    const double f = (a - origin) / step;
    const double off = std::fabs(f - std::floor(f + 0.5)) * step;
    const double coverage = line_coverage(off / gradient, width);
    return coverage;
}

// The coverage of the nearest of the ascending `levels`.
double level_coverage(double a, std::span<const double> levels, double gradient, double width) noexcept {
    if (levels.empty() || !(gradient > 0.0)) return 0.0;
    std::span<const double>::iterator above = std::lower_bound(levels.begin(), levels.end(), a);
    double off = std::numeric_limits<double>::infinity();
    if (above != levels.end()) off = std::fabs(*above - a);
    if (above != levels.begin()) off = std::min(off, std::fabs(a - *(above - 1)));
    const double coverage = line_coverage(off / gradient, width);
    return coverage;
}

// The source triangle's barycentric weights at a sample: the screen
// corners' weights (perspective-correct: screen weights times 1 / depth,
// the keys, normalized; orthographic: the screen weights), mapped through
// each corner's source weights.
std::array<double, 3> source_weights(const Renderer::ScreenTriangle& t, float l1, float l2,
                                     Projection projection) noexcept {
    const std::array<double, 3> screen{1.0 - static_cast<double>(l1) - static_cast<double>(l2), static_cast<double>(l1),
                                       static_cast<double>(l2)};
    std::array<double, 3> corner = screen;
    if (projection == Projection::perspective) {
        double total = 0.0;
        for (std::size_t k = 0U; k < 3U; ++k) {
            corner[k] = screen[k] * static_cast<double>(t.key[k]);
            total = total + corner[k];
        }
        if (total > 0.0) {
            for (std::size_t k = 0U; k < 3U; ++k) corner[k] = corner[k] / total;
        }
    }
    std::array<double, 3> source{};
    for (std::size_t k = 0U; k < 3U; ++k) {
        const double second = static_cast<double>(t.weights[k * 2U]);
        const double third = static_cast<double>(t.weights[k * 2U + 1U]);
        source[0] = source[0] + corner[k] * (1.0 - second - third);
        source[1] = source[1] + corner[k] * second;
        source[2] = source[2] + corner[k] * third;
    }
    return source;
}

} // namespace

// One frame's shared state, the context of its band tasks. Everything is
// read-only to the tasks except the samples and picture rows of their own
// band.
struct FrameWork final {
    Renderer& renderer;
    const Scene& scene;
    const View& view;
    std::size_t supersample = 1U;
    std::size_t sample_width = 0U;
    std::size_t sample_height = 0U;
    Shade background{};

    // -- band tasks -------------------------------------------------------------------

    static void run_band(void* context, std::size_t band) noexcept {
        FrameWork& work = *static_cast<FrameWork*>(context);
        work.band(band);
    }

    void band(std::size_t band_index) noexcept {
        const std::size_t band_rows = band_pixel_rows * supersample;
        const std::size_t row_first = band_index * band_rows;
        const std::size_t row_end = std::min(row_first + band_rows, sample_height);
        Renderer::Sample empty;
        for (std::size_t row = row_first; row < row_end; ++row) {
            for (std::size_t column = 0U; column < sample_width; ++column) {
                renderer.samples_[row * sample_width + column] = empty;
            }
        }
        const std::size_t first = renderer.band_starts_[band_index];
        const std::size_t end = renderer.band_starts_[band_index + 1U];
        for (std::size_t entry = first; entry < end; ++entry) {
            const std::uint32_t primitive = renderer.band_entries_[entry];
            draw(renderer.primitives_[primitive], primitive, row_first, row_end);
        }
        const std::size_t pixel_first = band_index * band_pixel_rows;
        const std::size_t pixel_end = std::min(pixel_first + band_pixel_rows, renderer.height_);
        for (std::size_t y = pixel_first; y < pixel_end; ++y) {
            for (std::size_t x = 0U; x < renderer.width_; ++x) resolve(x, y);
        }
    }

    // Writes every sample of rows [row_first, row_end) whose centre the
    // triangle covers (top-left rule) and where it is nearer than what is
    // there. Earlier primitives win ties.
    void draw(const Renderer::ScreenTriangle& t, std::uint32_t primitive, std::size_t row_first,
              std::size_t row_end) noexcept {
        const std::int64_t x0 = t.x[0];
        const std::int64_t y0 = t.y[0];
        const std::int64_t x1 = t.x[1];
        const std::int64_t y1 = t.y[1];
        const std::int64_t x2 = t.x[2];
        const std::int64_t y2 = t.y[2];
        const std::int64_t min_x = std::min(x0, std::min(x1, x2));
        const std::int64_t max_x = std::max(x0, std::max(x1, x2));
        const std::int64_t min_y = std::min(y0, std::min(y1, y2));
        const std::int64_t max_y = std::max(y0, std::max(y1, y2));
        const std::int64_t half = sub_sample / 2;
        // Samples whose centres (k * 256 + 128) lie in the bounding box.
        std::int64_t column_low = floor_divide(min_x - half + sub_sample - 1, sub_sample);
        std::int64_t column_high = floor_divide(max_x - half, sub_sample);
        std::int64_t row_low = floor_divide(min_y - half + sub_sample - 1, sub_sample);
        std::int64_t row_high = floor_divide(max_y - half, sub_sample);
        column_low = std::max<std::int64_t>(column_low, 0);
        column_high = std::min<std::int64_t>(column_high, static_cast<std::int64_t>(sample_width) - 1);
        row_low = std::max<std::int64_t>(row_low, static_cast<std::int64_t>(row_first));
        row_high = std::min<std::int64_t>(row_high, static_cast<std::int64_t>(row_end) - 1);
        if (column_low > column_high || row_low > row_high) return;
        // Edge k is opposite corner k: E(a, b, p) = (bx - ax)(py - ay) - (by - ay)(px - ax).
        const std::array<std::int64_t, 3> edge_dx{x2 - x1, x0 - x2, x1 - x0};
        const std::array<std::int64_t, 3> edge_dy{y2 - y1, y0 - y2, y1 - y0};
        const std::array<std::int64_t, 3> edge_ax{x1, x2, x0};
        const std::array<std::int64_t, 3> edge_ay{y1, y2, y0};
        const std::int64_t area = edge_dx[2] * (y2 - y0) - edge_dy[2] * (x2 - x0);
        const double inverse_area = 1.0 / static_cast<double>(area);
        // A top edge (horizontal, interior below) or a left edge (going up)
        // owns the samples on it; the others do not.
        std::array<std::int64_t, 3> threshold{};
        std::array<std::int64_t, 3> row_edge{};
        std::array<std::int64_t, 3> step_x{};
        std::array<std::int64_t, 3> step_y{};
        const std::int64_t start_x = column_low * sub_sample + half;
        const std::int64_t start_y = row_low * sub_sample + half;
        for (std::size_t k = 0U; k < 3U; ++k) {
            const bool top_left = (edge_dy[k] == 0 && edge_dx[k] > 0) || edge_dy[k] < 0;
            threshold[k] = top_left ? 0 : 1;
            row_edge[k] = edge_dx[k] * (start_y - edge_ay[k]) - edge_dy[k] * (start_x - edge_ax[k]);
            step_x[k] = -edge_dy[k] * sub_sample;
            step_y[k] = edge_dx[k] * sub_sample;
        }
        for (std::int64_t row = row_low; row <= row_high; ++row) {
            std::array<std::int64_t, 3> e = row_edge;
            Renderer::Sample* const line = &renderer.samples_[static_cast<std::size_t>(row) * sample_width];
            for (std::int64_t column = column_low; column <= column_high; ++column) {
                if (e[0] >= threshold[0] && e[1] >= threshold[1] && e[2] >= threshold[2]) {
                    const float l0 = static_cast<float>(static_cast<double>(e[0]) * inverse_area);
                    const float l1 = static_cast<float>(static_cast<double>(e[1]) * inverse_area);
                    const float l2 = static_cast<float>(static_cast<double>(e[2]) * inverse_area);
                    const float near0 = t.key[0] * l0;
                    const float near1 = t.key[1] * l1;
                    const float near2 = t.key[2] * l2;
                    const float key = near0 + near1 + near2;
                    Renderer::Sample& sample = line[static_cast<std::size_t>(column)];
                    if (key > sample.key) {
                        sample.key = key;
                        sample.primitive = primitive;
                        sample.l1 = l1;
                        sample.l2 = l2;
                    }
                }
                e[0] = e[0] + step_x[0];
                e[1] = e[1] + step_x[1];
                e[2] = e[2] + step_x[2];
            }
            row_edge[0] = row_edge[0] + step_y[0];
            row_edge[1] = row_edge[1] + step_y[1];
            row_edge[2] = row_edge[2] + step_y[2];
        }
    }

    // The pixel: each primitive under it shaded once (at its first sample),
    // weighted by the samples it covers, plus the background for the rest.
    void resolve(std::size_t x, std::size_t y) noexcept {
        constexpr std::size_t most = Renderer::maximum_supersample * Renderer::maximum_supersample;
        std::array<std::uint32_t, most> seen{};
        std::array<std::uint32_t, most> counts{};
        std::array<Renderer::Sample, most> firsts{};
        std::size_t distinct = 0U;
        std::size_t empty = 0U;
        for (std::size_t sy = 0U; sy < supersample; ++sy) {
            const std::size_t row = y * supersample + sy;
            for (std::size_t sx = 0U; sx < supersample; ++sx) {
                const Renderer::Sample& sample = renderer.samples_[row * sample_width + x * supersample + sx];
                if (sample.primitive == no_primitive) {
                    ++empty;
                    continue;
                }
                std::size_t at = 0U;
                while (at < distinct && seen[at] != sample.primitive) ++at;
                if (at == distinct) {
                    seen[at] = sample.primitive;
                    counts[at] = 0U;
                    firsts[at] = sample;
                    ++distinct;
                }
                ++counts[at];
            }
        }
        const double share = 1.0 / static_cast<double>(supersample * supersample);
        Shade sum;
        for (std::size_t k = 0U; k < distinct; ++k) {
            const Shade part = shade(seen[k], firsts[k]);
            const double weight = static_cast<double>(counts[k]) * share;
            sum.r = sum.r + part.r * weight;
            sum.g = sum.g + part.g * weight;
            sum.b = sum.b + part.b * weight;
            sum.a = sum.a + part.a * weight;
        }
        const double rest = static_cast<double>(empty) * share;
        sum.r = sum.r + background.r * rest;
        sum.g = sum.g + background.g * rest;
        sum.b = sum.b + background.b * rest;
        sum.a = sum.a + background.a * rest;
        std::uint8_t* const out = &renderer.picture_[(y * renderer.width_ + x) * 4U];
        out[0] = to_byte(sum.b);
        out[1] = to_byte(sum.g);
        out[2] = to_byte(sum.r);
        out[3] = to_byte(sum.a);
    }

    [[nodiscard]] Shade shade(std::uint32_t primitive, const Renderer::Sample& sample) const noexcept {
        const Renderer::ScreenTriangle& t = renderer.primitives_[primitive];
        const std::uint8_t kind = static_cast<std::uint8_t>(t.kind & kind_mask);
        switch (kind) {
        case kind_mesh:
            return shade_surface(t, sample);
        case kind_line:
            return premultiplied(scene.lines[t.source].color);
        case kind_marker:
            return premultiplied(scene.markers[t.source].color);
        default:
            return Shade{};
        }
    }

    [[nodiscard]] Shade shade_surface(const Renderer::ScreenTriangle& t, const Renderer::Sample& sample) const noexcept {
        const Mesh& mesh = *scene.mesh;
        const std::span<const MeshVertex> vertices = mesh.vertices();
        const std::span<const std::uint32_t> indices = mesh.indices();
        const std::uint32_t part = mesh.triangle_parts()[t.source];
        const Material fallback{};
        const Material& material = part < scene.materials.size() ? scene.materials[part] : fallback;
        const std::size_t base = static_cast<std::size_t>(t.source) * 3U;
        const std::array<std::uint32_t, 3> corner{indices[base], indices[base + 1U], indices[base + 2U]};
        const std::array<double, 3> weight = source_weights(t, sample.l1, sample.l2, renderer.projection_);
        Vec3 position{};
        Vec3 normal{};
        double value = 0.0;
        double u = 0.0;
        double v = 0.0;
        Shade vertex_color;
        for (std::size_t k = 0U; k < 3U; ++k) {
            const MeshVertex& vertex = vertices[corner[k]];
            position = position + vertex.position * weight[k];
            normal = normal + vertex.normal * weight[k];
            value = value + vertex.value * weight[k];
            u = u + vertex.u * weight[k];
            v = v + vertex.v * weight[k];
            vertex_color.r = vertex_color.r + static_cast<double>(vertex.color.r) * weight[k];
            vertex_color.g = vertex_color.g + static_cast<double>(vertex.color.g) * weight[k];
            vertex_color.b = vertex_color.b + static_cast<double>(vertex.color.b) * weight[k];
            vertex_color.a = vertex_color.a + static_cast<double>(vertex.color.a) * weight[k];
        }
        // The straight base colour.
        Shade base{static_cast<double>(material.color.r), static_cast<double>(material.color.g),
                   static_cast<double>(material.color.b), static_cast<double>(material.color.a)};
        if (material.source == ColorSource::vertex_color) {
            base = vertex_color;
        } else if (material.source == ColorSource::vertex_value && !material.colormap.empty()) {
            const double span = material.value_maximum - material.value_minimum;
            const double t_value = span > 0.0 ? clamp_unit((value - material.value_minimum) / span) : 0.0;
            const std::size_t entries = material.colormap.size();
            std::size_t entry = static_cast<std::size_t>(std::floor(t_value * static_cast<double>(entries)));
            if (entry >= entries) entry = entries - 1U;
            const Color& mapped = material.colormap[entry];
            base = Shade{static_cast<double>(mapped.r), static_cast<double>(mapped.g), static_cast<double>(mapped.b),
                         static_cast<double>(mapped.a)};
        }
        const Lighting& lighting = material.lighting;
        if (lighting.mode != LightingMode::none) {
            const Vec3 toward_eye =
                renderer.projection_ == Projection::perspective ? normalized(view.eye() - position) : view.forward() * -1.0;
            Vec3 n = normalized(normal);
            if (dot(n, toward_eye) < 0.0) n = n * -1.0;
            const Vec3 light = lighting.mode == LightingMode::headlight ? view.forward() * -1.0 : normalized(lighting.direction);
            const double diffuse = std::max(0.0, dot(n, light));
            const double factor = static_cast<double>(lighting.ambient) + static_cast<double>(lighting.diffuse) * diffuse;
            double highlight = 0.0;
            if (lighting.specular > 0.0f) {
                const Vec3 halfway = normalized(light + toward_eye);
                const double facing = std::max(0.0, dot(n, halfway));
                highlight = static_cast<double>(lighting.specular) * std::pow(facing, static_cast<double>(lighting.shininess));
            }
            base.r = base.r * factor + highlight;
            base.g = base.g * factor + highlight;
            base.b = base.b * factor + highlight;
        }
        const IsoLines& iso = material.iso;
        const bool any_iso = iso.width > 0.0f && (iso.u_step > 0.0 || iso.v_step > 0.0 || !iso.value_levels.empty());
        if (any_iso) {
            const double coverage = iso_coverage(t, corner, u, v, value, iso);
            const double mix = coverage * clamp_unit(static_cast<double>(iso.color.a));
            base.r = base.r + (static_cast<double>(iso.color.r) - base.r) * mix;
            base.g = base.g + (static_cast<double>(iso.color.g) - base.g) * mix;
            base.b = base.b + (static_cast<double>(iso.color.b) - base.b) * mix;
        }
        const Color result{static_cast<float>(base.r), static_cast<float>(base.g), static_cast<float>(base.b),
                           static_cast<float>(base.a)};
        return premultiplied(result);
    }

    // The iso-line coverage at a shading point. Each attribute's pixel
    // gradient is taken as affine across the triangle on the screen (exact
    // for orthographic views, a close estimate for perspective ones).
    // `corner` holds the source triangle's vertices in source order.
    [[nodiscard]] double iso_coverage(const Renderer::ScreenTriangle& t, const std::array<std::uint32_t, 3>& corner,
                                      double u, double v, double value, const IsoLines& iso) const noexcept {
        const std::span<const MeshVertex> vertices = (*scene.mesh).vertices();
        const double x0 = static_cast<double>(t.x[0]);
        const double y0 = static_cast<double>(t.y[0]);
        const double x1 = static_cast<double>(t.x[1]);
        const double y1 = static_cast<double>(t.y[1]);
        const double x2 = static_cast<double>(t.x[2]);
        const double y2 = static_cast<double>(t.y[2]);
        const double area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        // Barycentric change per pixel (one pixel is supersample samples).
        const double per_pixel = sub_sample_scale * static_cast<double>(supersample) / area;
        const std::array<double, 3> along_x{-(y2 - y1) * per_pixel, -(y0 - y2) * per_pixel, -(y1 - y0) * per_pixel};
        const std::array<double, 3> along_y{(x2 - x1) * per_pixel, (x0 - x2) * per_pixel, (x1 - x0) * per_pixel};
        double ux = 0.0;
        double uy = 0.0;
        double vx = 0.0;
        double vy = 0.0;
        double wx = 0.0;
        double wy = 0.0;
        const MeshVertex& first = vertices[corner[0]];
        const MeshVertex& second = vertices[corner[1]];
        const MeshVertex& third = vertices[corner[2]];
        for (std::size_t k = 0U; k < 3U; ++k) {
            // The attributes at screen corner k, from its source weights.
            const double w1 = static_cast<double>(t.weights[k * 2U]);
            const double w2 = static_cast<double>(t.weights[k * 2U + 1U]);
            const double w0 = 1.0 - w1 - w2;
            const double corner_u = first.u * w0 + second.u * w1 + third.u * w2;
            const double corner_v = first.v * w0 + second.v * w1 + third.v * w2;
            const double corner_value = first.value * w0 + second.value * w1 + third.value * w2;
            ux = ux + corner_u * along_x[k];
            uy = uy + corner_u * along_y[k];
            vx = vx + corner_v * along_x[k];
            vy = vy + corner_v * along_y[k];
            wx = wx + corner_value * along_x[k];
            wy = wy + corner_value * along_y[k];
        }
        const double width = static_cast<double>(iso.width);
        const double by_u = step_coverage(u, iso.u_origin, iso.u_step, std::sqrt(ux * ux + uy * uy), width);
        const double by_v = step_coverage(v, iso.v_origin, iso.v_step, std::sqrt(vx * vx + vy * vy), width);
        const double by_value = level_coverage(value, iso.value_levels, std::sqrt(wx * wx + wy * wy), width);
        const double coverage = std::max(by_u, std::max(by_v, by_value));
        return coverage;
    }
};

namespace {

// A point in view coordinates (along the view's right and up directions
// from the eye, and depth along the view direction), with the source
// triangle's barycentric weights of its second and third vertices there.
struct ClipVertex final {
    double right = 0.0;
    double up = 0.0;
    double depth = 0.0;
    double second = 0.0;
    double third = 0.0;
};

ClipVertex to_view(const View& view, Vec3 position) noexcept {
    const Vec3 offset = position - view.eye();
    ClipVertex point;
    point.right = dot(offset, view.right());
    point.up = dot(offset, view.upward());
    point.depth = dot(offset, view.forward());
    return point;
}

ClipVertex between(const ClipVertex& a, const ClipVertex& b, double t) noexcept {
    ClipVertex point;
    point.right = a.right + (b.right - a.right) * t;
    point.up = a.up + (b.up - a.up) * t;
    point.depth = a.depth + (b.depth - a.depth) * t;
    point.second = a.second + (b.second - a.second) * t;
    point.third = a.third + (b.third - a.third) * t;
    return point;
}

constexpr std::size_t clip_planes = 5U;
// A triangle cut by five planes keeps at most eight corners.
constexpr std::size_t clip_most = 8U;
using ClipPolygon = std::array<ClipVertex, clip_most + 1U>;

// The view frustum as five planes linear in view coordinates: the near
// plane, then the picture's left, right, top and bottom sides widened by a
// margin (pixels). A side condition such as x >= -margin is multiplied by
// the projective divisor W (depth for perspective, 1 for orthographic) to
// make it linear, which holds only where W > 0: the near plane is always
// cut first.
class Frustum final {
public:
    Frustum(const View& view, double margin) noexcept
        : projection_(view.projection()), scale_(view.scale()), centre_x_(0.5 * static_cast<double>(view.width())),
          centre_y_(0.5 * static_cast<double>(view.height())), near_(view.near()), margin_(margin) {}

    // Signed: zero on the plane, positive inside.
    [[nodiscard]] double distance(std::size_t plane, const ClipVertex& point) const noexcept {
        const double divisor = projection_ == Projection::perspective ? point.depth : 1.0;
        switch (plane) {
        case 0U:
            return point.depth - near_;
        case 1U:
            return (centre_x_ + margin_) * divisor + scale_ * point.right;
        case 2U:
            return (centre_x_ + margin_) * divisor - scale_ * point.right;
        case 3U:
            return (centre_y_ + margin_) * divisor - scale_ * point.up;
        case 4U:
            return (centre_y_ + margin_) * divisor + scale_ * point.up;
        default:
            return 0.0;
        }
    }

    [[nodiscard]] bool inside(const ClipVertex& point) const noexcept {
        for (std::size_t plane = 0U; plane < clip_planes; ++plane) {
            if (!(distance(plane, point) >= 0.0)) return false;
        }
        return true;
    }

    // The picture position (pixels) of a point in front of the near plane.
    void project(const ClipVertex& point, double& x, double& y) const noexcept {
        const double factor = projection_ == Projection::perspective ? scale_ / point.depth : scale_;
        x = centre_x_ + point.right * factor;
        y = centre_y_ - point.up * factor;
    }

    // Cuts a convex polygon by every plane in order (Sutherland-Hodgman) and
    // returns its corner count, below three when nothing is left.
    [[nodiscard]] std::size_t clip(ClipPolygon& polygon, std::size_t count) const noexcept {
        ClipPolygon kept{};
        for (std::size_t plane = 0U; plane < clip_planes; ++plane) {
            std::size_t out = 0U;
            for (std::size_t k = 0U; k < count; ++k) {
                const ClipVertex& a = polygon[k];
                const ClipVertex& b = polygon[(k + 1U) % count];
                const double da = distance(plane, a);
                const double db = distance(plane, b);
                const bool a_in = da >= 0.0;
                const bool b_in = db >= 0.0;
                if (a_in) {
                    kept[out] = a;
                    ++out;
                }
                if (a_in != b_in) {
                    kept[out] = between(a, b, da / (da - db));
                    ++out;
                }
            }
            polygon = kept;
            count = out;
            if (count < 3U) return 0U;
        }
        return count;
    }

    // Cuts the segment a-b to the frustum; false when nothing is left.
    [[nodiscard]] bool clip(ClipVertex& a, ClipVertex& b) const noexcept {
        for (std::size_t plane = 0U; plane < clip_planes; ++plane) {
            const double da = distance(plane, a);
            const double db = distance(plane, b);
            if (da < 0.0 && db < 0.0) return false;
            if (da < 0.0) {
                a = between(a, b, da / (da - db));
            } else if (db < 0.0) {
                b = between(a, b, da / (da - db));
            }
        }
        return true;
    }

private:
    Projection projection_;
    double scale_;
    double centre_x_;
    double centre_y_;
    double near_;
    double margin_;
};

// Builds a frame's primitives into the renderer's staging buffer, clipped
// to the frustum, and counts what it drew, clipped and skipped. A refused
// growth is remembered (failed) and the rest is not appended.
class PrimitiveBuilder final {
public:
    PrimitiveBuilder(Buffer<Renderer::ScreenTriangle>& out, RenderStatistics& statistics, const View& view,
                     double sample_scale, double near_depth, double far_depth) noexcept
        : out_(out), statistics_(statistics), view_(view), mesh_frustum_(view, clip_margin),
          projection_(view.projection()), sample_scale_(sample_scale), near_depth_(near_depth),
          far_depth_(far_depth) {}

    [[nodiscard]] bool failed() const noexcept { return failed_; }

    // The depth key of a view depth: larger is nearer, positive for
    // anything in front of the near plane.
    [[nodiscard]] float key(double depth) const noexcept {
        if (projection_ == Projection::perspective) return static_cast<float>(1.0 / depth);
        const double span = far_depth_ - near_depth_;
        const double share = span > 0.0 ? (far_depth_ - depth) / span : 1.0;
        const double mapped = share * 0.999 + 0.001;
        return static_cast<float>(mapped);
    }

    // A mesh triangle from its corners in view coordinates (source weights
    // (0, 0), (1, 0), (0, 1)): drawn whole, as its clipped part, or skipped.
    void mesh_triangle(const std::array<ClipVertex, 3>& corners, std::uint32_t source) noexcept {
        const bool whole = mesh_frustum_.inside(corners[0]) && mesh_frustum_.inside(corners[1]) &&
                           mesh_frustum_.inside(corners[2]);
        if (whole) {
            const bool drawn = view_triangle(corners[0], corners[1], corners[2], source);
            if (drawn) {
                ++statistics_.triangles_drawn;
            } else {
                ++statistics_.triangles_skipped;
            }
            return;
        }
        ClipPolygon polygon{};
        polygon[0] = corners[0];
        polygon[1] = corners[1];
        polygon[2] = corners[2];
        const std::size_t count = mesh_frustum_.clip(polygon, 3U);
        bool any = false;
        for (std::size_t k = 1U; k + 1U < count; ++k) {
            if (view_triangle(polygon[0], polygon[k], polygon[k + 1U], source)) any = true;
        }
        if (any) {
            ++statistics_.triangles_clipped;
        } else {
            ++statistics_.triangles_skipped;
        }
    }

    // A segment from p to q, `width` pixels wide with square ends, clipped
    // to the frustum widened by its width.
    void segment(ClipVertex p, ClipVertex q, double width, std::uint32_t strip, std::uint32_t index) noexcept {
        const Frustum frustum(view_, clip_margin + width);
        if (!frustum.clip(p, q)) return;
        double px = 0.0;
        double py = 0.0;
        double qx = 0.0;
        double qy = 0.0;
        frustum.project(p, px, py);
        frustum.project(q, qx, qy);
        px = px * sample_scale_;
        py = py * sample_scale_;
        qx = qx * sample_scale_;
        qy = qy * sample_scale_;
        const float pk = key(p.depth) * overlay_bias;
        const float qk = key(q.depth) * overlay_bias;
        const double half = width * 0.5 * sample_scale_;
        double dx = qx - px;
        double dy = qy - py;
        const double extent = std::sqrt(dx * dx + dy * dy);
        if (extent > 1e-9) {
            dx = dx / extent;
            dy = dy / extent;
        } else {
            dx = 1.0;
            dy = 0.0;
        }
        const double ax = dx * half;
        const double ay = dy * half;
        const double nx = -ay;
        const double ny = ax;
        const std::array<double, 4> x{px - ax - nx, qx + ax - nx, qx + ax + nx, px - ax + nx};
        const std::array<double, 4> y{py - ay - ny, qy + ay - ny, qy + ay + ny, py - ay + ny};
        const std::array<float, 4> keys{pk, qk, qk, pk};
        quad(x, y, keys, kind_line, strip, index);
        ++statistics_.segments_drawn;
    }

    // A screen-facing marker `radius` pixels round its centre; left out when
    // the centre is behind the near plane or the marker off the picture.
    void marker(const ClipVertex& centre, double radius, MarkerShape shape, std::uint32_t index) noexcept {
        const Frustum frustum(view_, clip_margin + radius);
        if (!frustum.inside(centre)) return;
        double centre_x = 0.0;
        double centre_y = 0.0;
        frustum.project(centre, centre_x, centre_y);
        const double cx = centre_x * sample_scale_;
        const double cy = centre_y * sample_scale_;
        const double r = radius * sample_scale_;
        const float k = key(centre.depth) * overlay_bias;
        const std::array<float, 3> keys3{k, k, k};
        const std::array<float, 4> keys4{k, k, k, k};
        switch (shape) {
        case MarkerShape::circle:
            for (std::size_t s = 0U; s < circle_x.size(); ++s) {
                const std::size_t next = (s + 1U) % circle_x.size();
                const std::array<double, 3> x{cx, cx + circle_x[s] * r, cx + circle_x[next] * r};
                const std::array<double, 3> y{cy, cy + circle_y[s] * r, cy + circle_y[next] * r};
                static_cast<void>(triangle(x, y, keys3, identity_weights, kind_marker, index, 0U));
            }
            break;
        case MarkerShape::square: {
            const std::array<double, 4> x{cx - r, cx + r, cx + r, cx - r};
            const std::array<double, 4> y{cy - r, cy - r, cy + r, cy + r};
            quad(x, y, keys4, kind_marker, index, 0U);
            break;
        }
        case MarkerShape::diamond: {
            const std::array<double, 4> x{cx, cx + r, cx, cx - r};
            const std::array<double, 4> y{cy - r, cy, cy + r, cy};
            quad(x, y, keys4, kind_marker, index, 0U);
            break;
        }
        case MarkerShape::triangle_up: {
            const double side = r * 0.86602540378443865;
            const std::array<double, 3> x{cx, cx + side, cx - side};
            const std::array<double, 3> y{cy - r, cy + r * 0.5, cy + r * 0.5};
            static_cast<void>(triangle(x, y, keys3, identity_weights, kind_marker, index, 0U));
            break;
        }
        case MarkerShape::plus:
        case MarkerShape::cross: {
            // Two bars; the cross's turned by 45 degrees.
            const double thickness = std::max(0.5, r * 0.125);
            const double turn = shape == MarkerShape::cross ? 0.70710678118654757 : 1.0;
            const double slant = shape == MarkerShape::cross ? 0.70710678118654757 : 0.0;
            for (std::size_t bar = 0U; bar < 2U; ++bar) {
                // The bar's direction (along) and its width direction.
                const double along_x = bar == 0U ? turn : -slant;
                const double along_y = bar == 0U ? slant : turn;
                const double across_x = -along_y;
                const double across_y = along_x;
                const double ex = along_x * r;
                const double ey = along_y * r;
                const double wx = across_x * thickness;
                const double wy = across_y * thickness;
                const std::array<double, 4> x{cx - ex - wx, cx + ex - wx, cx + ex + wx, cx - ex + wx};
                const std::array<double, 4> y{cy - ey - wy, cy + ey - wy, cy + ey + wy, cy - ey + wy};
                quad(x, y, keys4, kind_marker, index, 0U);
            }
            break;
        }
        default:
            return;
        }
        ++statistics_.markers_drawn;
    }

private:
    // A mesh triangle from three points inside the frustum.
    bool view_triangle(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c, std::uint32_t source) noexcept {
        std::array<double, 3> x{};
        std::array<double, 3> y{};
        mesh_frustum_.project(a, x[0], y[0]);
        mesh_frustum_.project(b, x[1], y[1]);
        mesh_frustum_.project(c, x[2], y[2]);
        for (std::size_t k = 0U; k < 3U; ++k) {
            x[k] = x[k] * sample_scale_;
            y[k] = y[k] * sample_scale_;
        }
        const std::array<float, 3> keys{key(a.depth), key(b.depth), key(c.depth)};
        const std::array<float, 6> weights{static_cast<float>(a.second), static_cast<float>(a.third),
                                           static_cast<float>(b.second), static_cast<float>(b.third),
                                           static_cast<float>(c.second), static_cast<float>(c.third)};
        const bool drawn = triangle(x, y, keys, weights, kind_mesh, source, 0U);
        return drawn;
    }

    // Appends one triangle from sample-space corners, turned so its area is
    // positive; false when it is degenerate, outside the guard band (which
    // clipping keeps it inside) or the growth is refused.
    bool triangle(const std::array<double, 3>& x, const std::array<double, 3>& y, const std::array<float, 3>& keys,
                  const std::array<float, 6>& weights, std::uint8_t kind, std::uint32_t source,
                  std::uint32_t detail) noexcept {
        if (failed_) return false;
        Renderer::ScreenTriangle t;
        for (std::size_t k = 0U; k < 3U; ++k) {
            const double fx = std::floor(x[k] * sub_sample_scale + 0.5);
            const double fy = std::floor(y[k] * sub_sample_scale + 0.5);
            if (!(std::fabs(fx) <= guard) || !(std::fabs(fy) <= guard)) return false;
            t.x[k] = static_cast<std::int32_t>(fx);
            t.y[k] = static_cast<std::int32_t>(fy);
            t.key[k] = keys[k];
        }
        t.weights = weights;
        const std::int64_t area = (static_cast<std::int64_t>(t.x[1]) - t.x[0]) * (static_cast<std::int64_t>(t.y[2]) - t.y[0]) -
                                  (static_cast<std::int64_t>(t.y[1]) - t.y[0]) * (static_cast<std::int64_t>(t.x[2]) - t.x[0]);
        if (area == 0) return false;
        if (area < 0) {
            std::swap(t.x[1], t.x[2]);
            std::swap(t.y[1], t.y[2]);
            std::swap(t.key[1], t.key[2]);
            std::swap(t.weights[2], t.weights[4]);
            std::swap(t.weights[3], t.weights[5]);
        }
        t.kind = kind;
        t.source = source;
        t.detail = detail;
        if (!out_.push_back(t)) {
            failed_ = true;
            return false;
        }
        return true;
    }

    // A quad a, b, c, d (in order round it) as two triangles.
    void quad(const std::array<double, 4>& x, const std::array<double, 4>& y, const std::array<float, 4>& keys,
              std::uint8_t kind, std::uint32_t source, std::uint32_t detail) noexcept {
        const std::array<double, 3> first_x{x[0], x[1], x[2]};
        const std::array<double, 3> first_y{y[0], y[1], y[2]};
        const std::array<float, 3> first_keys{keys[0], keys[1], keys[2]};
        const std::array<double, 3> second_x{x[0], x[2], x[3]};
        const std::array<double, 3> second_y{y[0], y[2], y[3]};
        const std::array<float, 3> second_keys{keys[0], keys[2], keys[3]};
        static_cast<void>(triangle(first_x, first_y, first_keys, identity_weights, kind, source, detail));
        static_cast<void>(triangle(second_x, second_y, second_keys, identity_weights, kind, source, detail));
    }

    Buffer<Renderer::ScreenTriangle>& out_;
    RenderStatistics& statistics_;
    const View& view_;
    Frustum mesh_frustum_;
    Projection projection_;
    double sample_scale_;
    double near_depth_;
    double far_depth_;
    bool failed_ = false;
};

Status validate_scene(const Scene& scene) noexcept {
    for (const Material& material : scene.materials) {
        if (!material_valid(material)) return Status::nonfinite_input;
    }
    if (!color_valid(scene.background)) return Status::nonfinite_input;
    for (const Vec3& point : scene.line_points) {
        if (!finite(point)) return Status::nonfinite_input;
    }
    for (const LineStrip& strip : scene.lines) {
        if (strip.first > scene.line_points.size() || strip.count > scene.line_points.size() - strip.first) {
            return Status::invalid_argument;
        }
        if (!color_valid(strip.color) || !std::isfinite(strip.width)) return Status::nonfinite_input;
        if (strip.width < 0.0f) return Status::invalid_argument;
    }
    for (const Marker& marker : scene.markers) {
        if (!finite(marker.position) || !color_valid(marker.color) || !std::isfinite(marker.size)) {
            return Status::nonfinite_input;
        }
        if (marker.size < 0.0f || marker_pieces(marker.shape) == 0U) return Status::invalid_argument;
    }
    return Status::ok;
}

} // namespace

Renderer::Renderer(const Allocator& allocator) noexcept
    : allocator_(allocator), samples_(allocator), primitives_(allocator), pick_line_items_(allocator),
      pick_marker_items_(allocator), picture_(allocator), vertex_right_(allocator), vertex_up_(allocator),
      vertex_depth_(allocator), staging_(allocator), band_starts_(allocator), band_cursor_(allocator),
      band_entries_(allocator) {}

std::size_t Renderer::storage_bytes() const noexcept {
    const std::size_t bytes = samples_.storage_bytes() + primitives_.storage_bytes() +
                              pick_line_items_.storage_bytes() + pick_marker_items_.storage_bytes() +
                              picture_.storage_bytes() + vertex_right_.storage_bytes() + vertex_up_.storage_bytes() +
                              vertex_depth_.storage_bytes() + staging_.storage_bytes() + band_starts_.storage_bytes() +
                              band_cursor_.storage_bytes() + band_entries_.storage_bytes();
    return bytes;
}

Status Renderer::render(const Scene& scene, const View& view, std::size_t supersample, const Executor& executor) noexcept {
    // -- validation: nothing is written --------------------------------------------
    if (executor.run == nullptr || !allocator_valid(allocator_)) return Status::invalid_argument;
    if (supersample < 1U || supersample > maximum_supersample) return Status::invalid_argument;
    const std::size_t width = view.width();
    const std::size_t height = view.height();
    if (width == 0U || height == 0U) return Status::invalid_argument;
    if (width > maximum_side || height > maximum_side) return Status::too_large;
    const Status scene_status = validate_scene(scene);
    if (scene_status != Status::ok) return scene_status;
    const std::size_t sample_width = width * supersample;
    const std::size_t sample_height = height * supersample;
    const std::size_t sample_total = sample_width * sample_height;
    const std::size_t mesh_vertices = scene.mesh != nullptr ? (*scene.mesh).vertex_count() : 0U;
    const std::size_t mesh_triangles = scene.mesh != nullptr ? (*scene.mesh).triangle_count() : 0U;
    std::size_t segments = 0U;
    for (const LineStrip& strip : scene.lines) {
        if (strip.count > 1U) segments = segments + (strip.count - 1U);
    }
    // The primitives before clipping (which may add some); the staging
    // buffer starts at this size and grows when clipping needs more.
    const std::size_t primitive_limit = no_primitive - 1U;
    if (mesh_triangles > primitive_limit || segments > (primitive_limit - mesh_triangles) / 2U) return Status::too_large;
    const std::size_t after_lines = mesh_triangles + segments * 2U;
    if (scene.markers.size() > (primitive_limit - after_lines) / marker_pieces_most) return Status::too_large;
    const std::size_t primitive_estimate = after_lines + scene.markers.size() * marker_pieces_most;
    const std::size_t bands = (height + band_pixel_rows - 1U) / band_pixel_rows;

    // -- working storage (picking does not read it) ---------------------------------
    if (!vertex_right_.resize_for_overwrite(mesh_vertices) || !vertex_up_.resize_for_overwrite(mesh_vertices) ||
        !vertex_depth_.resize_for_overwrite(mesh_vertices)) {
        return Status::out_of_memory;
    }
    staging_.clear();
    if (!staging_.reserve(primitive_estimate)) return Status::out_of_memory;
    if (!band_starts_.assign(bands + 1U, 0U) || !band_cursor_.resize_for_overwrite(bands)) return Status::out_of_memory;

    // View coordinates, and the depth range in front of the near plane.
    const double near = view.near();
    double depth_low = std::numeric_limits<double>::infinity();
    double depth_high = -std::numeric_limits<double>::infinity();
    if (scene.mesh != nullptr) {
        const std::span<const MeshVertex> vertices = (*scene.mesh).vertices();
        for (std::size_t k = 0U; k < mesh_vertices; ++k) {
            const ClipVertex point = to_view(view, vertices[k].position);
            vertex_right_[k] = point.right;
            vertex_up_[k] = point.up;
            vertex_depth_[k] = point.depth;
            if (point.depth > near) {
                depth_low = std::min(depth_low, point.depth);
                depth_high = std::max(depth_high, point.depth);
            }
        }
    }
    for (const Vec3& position : scene.line_points) {
        const ClipVertex point = to_view(view, position);
        if (point.depth > near) {
            depth_low = std::min(depth_low, point.depth);
            depth_high = std::max(depth_high, point.depth);
        }
    }
    for (const Marker& marker : scene.markers) {
        const ClipVertex point = to_view(view, marker.position);
        if (point.depth > near) {
            depth_low = std::min(depth_low, point.depth);
            depth_high = std::max(depth_high, point.depth);
        }
    }
    if (!(depth_low <= depth_high)) {
        depth_low = near;
        depth_high = near;
    }

    // Primitives, clipped, in a fixed order: mesh triangles, segments,
    // markers.
    RenderStatistics statistics;
    statistics.sample_count = sample_total;
    PrimitiveBuilder builder(staging_, statistics, view, static_cast<double>(supersample), depth_low, depth_high);
    if (scene.mesh != nullptr) {
        const std::span<const std::uint32_t> indices = (*scene.mesh).indices();
        for (std::size_t triangle = 0U; triangle < mesh_triangles; ++triangle) {
            const std::size_t base = triangle * 3U;
            std::array<ClipVertex, 3> corners{};
            for (std::size_t k = 0U; k < 3U; ++k) {
                const std::uint32_t index = indices[base + k];
                corners[k].right = vertex_right_[index];
                corners[k].up = vertex_up_[index];
                corners[k].depth = vertex_depth_[index];
            }
            corners[1].second = 1.0;
            corners[2].third = 1.0;
            builder.mesh_triangle(corners, static_cast<std::uint32_t>(triangle));
        }
    }
    for (std::size_t strip = 0U; strip < scene.lines.size(); ++strip) {
        const LineStrip& line = scene.lines[strip];
        for (std::uint32_t k = 0U; k + 1U < line.count; ++k) {
            const ClipVertex p = to_view(view, scene.line_points[line.first + k]);
            const ClipVertex q = to_view(view, scene.line_points[line.first + k + 1U]);
            builder.segment(p, q, static_cast<double>(line.width), static_cast<std::uint32_t>(strip), k);
        }
    }
    for (std::size_t index = 0U; index < scene.markers.size(); ++index) {
        const Marker& marker = scene.markers[index];
        builder.marker(to_view(view, marker.position), static_cast<double>(marker.size) * 0.5, marker.shape,
                       static_cast<std::uint32_t>(index));
    }
    if (builder.failed()) return Status::out_of_memory;
    if (staging_.size() > primitive_limit) return Status::too_large;

    // Binning: each primitive is listed under every band its rows touch, in
    // primitive order (counts, offsets, then the entries).
    const std::int64_t band_samples = static_cast<std::int64_t>(band_pixel_rows * supersample);
    const std::int64_t last_band = static_cast<std::int64_t>(bands) - 1;
    for (std::size_t p = 0U; p < staging_.size(); ++p) {
        const ScreenTriangle& t = staging_[p];
        const std::int64_t low = std::min<std::int64_t>(t.y[0], std::min<std::int64_t>(t.y[1], t.y[2]));
        const std::int64_t high = std::max<std::int64_t>(t.y[0], std::max<std::int64_t>(t.y[1], t.y[2]));
        const std::int64_t first = std::max<std::int64_t>(0, floor_divide(floor_divide(low, sub_sample), band_samples));
        const std::int64_t last = std::min(last_band, floor_divide(floor_divide(high, sub_sample), band_samples));
        for (std::int64_t b = first; b <= last; ++b) ++band_starts_[static_cast<std::size_t>(b) + 1U];
    }
    for (std::size_t b = 0U; b < bands; ++b) {
        if (band_starts_[b + 1U] > std::numeric_limits<std::uint32_t>::max() - band_starts_[b]) return Status::too_large;
        band_starts_[b + 1U] = band_starts_[b + 1U] + band_starts_[b];
        band_cursor_[b] = band_starts_[b];
    }
    if (!band_entries_.resize_for_overwrite(band_starts_[bands])) return Status::out_of_memory;
    for (std::size_t p = 0U; p < staging_.size(); ++p) {
        const ScreenTriangle& t = staging_[p];
        const std::int64_t low = std::min<std::int64_t>(t.y[0], std::min<std::int64_t>(t.y[1], t.y[2]));
        const std::int64_t high = std::max<std::int64_t>(t.y[0], std::max<std::int64_t>(t.y[1], t.y[2]));
        const std::int64_t first = std::max<std::int64_t>(0, floor_divide(floor_divide(low, sub_sample), band_samples));
        const std::int64_t last = std::min(last_band, floor_divide(floor_divide(high, sub_sample), band_samples));
        for (std::int64_t b = first; b <= last; ++b) {
            const std::size_t band = static_cast<std::size_t>(b);
            band_entries_[band_cursor_[band]] = static_cast<std::uint32_t>(p);
            ++band_cursor_[band];
        }
    }

    // -- storage picking reads: grown, with the previous frame kept ------------------
    if (!samples_.reserve(sample_total) || !picture_.reserve(width * height * 4U) ||
        !pick_line_items_.reserve(scene.lines.size()) || !pick_marker_items_.reserve(scene.markers.size())) {
        return Status::out_of_memory;
    }

    // -- commit: nothing below fails ---------------------------------------------------
    Buffer<ScreenTriangle> previous(std::move(primitives_));
    primitives_ = std::move(staging_);
    staging_ = std::move(previous);
    static_cast<void>(samples_.resize_for_overwrite(sample_total));
    static_cast<void>(picture_.resize_for_overwrite(width * height * 4U));
    pick_line_items_.clear();
    for (const LineStrip& strip : scene.lines) static_cast<void>(pick_line_items_.push_back(strip.item));
    pick_marker_items_.clear();
    for (const Marker& marker : scene.markers) static_cast<void>(pick_marker_items_.push_back(marker.item));
    drawn_mesh_ = scene.mesh;
    mesh_triangles_ = mesh_triangles;
    width_ = width;
    height_ = height;
    supersample_ = supersample;
    projection_ = view.projection();
    eye_ = view.eye();
    forward_ = view.forward();
    near_depth_ = depth_low;
    far_depth_ = depth_high;
    statistics_ = statistics;

    FrameWork work{*this, scene, view, supersample, sample_width, sample_height, premultiplied(scene.background)};
    const Task task{&work, &FrameWork::run_band};
    executor.run(executor.context, bands, task);
    return Status::ok;
}

Pick Renderer::pick(std::size_t x, std::size_t y, const Mesh* mesh) const noexcept {
    Pick result;
    if (x >= width_ || y >= height_) return result;
    const std::size_t sample_width = width_ * supersample_;
    const std::size_t row = y * supersample_ + supersample_ / 2U;
    const std::size_t column = x * supersample_ + supersample_ / 2U;
    const Sample& sample = samples_[row * sample_width + column];
    if (sample.primitive == no_primitive) return result;
    const ScreenTriangle& t = primitives_[sample.primitive];
    const std::uint8_t kind = static_cast<std::uint8_t>(t.kind & kind_mask);
    // The view depth back from the key.
    const double key = static_cast<double>(sample.key);
    if (kind == kind_line || kind == kind_marker) {
        const double unbiased = key / static_cast<double>(overlay_bias);
        double depth = 0.0;
        if (projection_ == Projection::perspective) {
            depth = unbiased > 0.0 ? 1.0 / unbiased : 0.0;
        } else {
            depth = far_depth_ - (unbiased - 0.001) / 0.999 * (far_depth_ - near_depth_);
        }
        result.depth = depth;
        result.position = eye_ + forward_ * depth;
    }
    switch (kind) {
    case kind_line:
        result.kind = PickKind::line;
        result.item = pick_line_items_[t.source];
        result.index = t.source;
        result.segment = t.detail;
        return result;
    case kind_marker:
        result.kind = PickKind::marker;
        result.item = pick_marker_items_[t.source];
        result.index = t.source;
        return result;
    case kind_mesh:
        break;
    default:
        return result;
    }
    if (mesh == nullptr || mesh != drawn_mesh_ || (*mesh).triangle_count() != mesh_triangles_) return result;
    const std::span<const MeshVertex> vertices = (*mesh).vertices();
    const std::span<const std::uint32_t> indices = (*mesh).indices();
    const std::size_t base = static_cast<std::size_t>(t.source) * 3U;
    const std::array<std::uint32_t, 3> corner{indices[base], indices[base + 1U], indices[base + 2U]};
    const std::array<double, 3> weight = source_weights(t, sample.l1, sample.l2, projection_);
    result.kind = PickKind::surface;
    result.item = (*mesh).triangle_parts()[t.source];
    result.index = t.source;
    for (std::size_t k = 0U; k < 3U; ++k) {
        const MeshVertex& vertex = vertices[corner[k]];
        result.u = result.u + vertex.u * weight[k];
        result.v = result.v + vertex.v * weight[k];
        result.value = result.value + vertex.value * weight[k];
        result.position = result.position + vertex.position * weight[k];
    }
    result.depth = dot(result.position - eye_, forward_);
    return result;
}

} // namespace stx_raster3d
