// Meshes: caller triangles, grids taken as they stand, and the adaptive
// tessellation of a CONV surface (the conv-teapot tree layout, PROVENANCE.md).

#include <stx_raster3d/mesh.hpp>

#include <stx_conv/surface.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>

namespace stx_raster3d {

// Appends to a mesh's three buffers as one transaction: every growth is
// made first, and a refused one truncates the buffers back to where they
// were.
class MeshAppender final {
public:
    explicit MeshAppender(Mesh& mesh) noexcept
        : mesh_(mesh), vertices_(mesh.vertices_.size()), indices_(mesh.indices_.size()), parts_(mesh.parts_.size()) {}

    // Room for `vertices` more vertices and `triangles` more triangles.
    [[nodiscard]] Status reserve(std::size_t vertices, std::size_t triangles) noexcept {
        const std::size_t vertex_limit = std::numeric_limits<std::uint32_t>::max();
        if (vertices > vertex_limit || vertices_ > vertex_limit - vertices) return Status::too_large;
        if (triangles > std::numeric_limits<std::size_t>::max() / 3U - indices_) return Status::too_large;
        if (!mesh_.vertices_.reserve(vertices_ + vertices)) return Status::out_of_memory;
        if (!mesh_.indices_.reserve(indices_ + triangles * 3U)) return Status::out_of_memory;
        if (!mesh_.parts_.reserve(parts_ + triangles)) return Status::out_of_memory;
        return Status::ok;
    }
    // The index the next appended vertex gets.
    [[nodiscard]] std::uint32_t next_vertex() const noexcept {
        return static_cast<std::uint32_t>(mesh_.vertices_.size());
    }
    // Within the reserved room these never fail.
    void vertex(const MeshVertex& vertex) noexcept { static_cast<void>(mesh_.vertices_.push_back(vertex)); }
    void triangle(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t part) noexcept {
        static_cast<void>(mesh_.indices_.push_back(a));
        static_cast<void>(mesh_.indices_.push_back(b));
        static_cast<void>(mesh_.indices_.push_back(c));
        static_cast<void>(mesh_.parts_.push_back(part));
    }
    // Gives back everything appended since construction.
    void roll_back() noexcept {
        static_cast<void>(mesh_.vertices_.resize_for_overwrite(vertices_));
        static_cast<void>(mesh_.indices_.resize_for_overwrite(indices_));
        static_cast<void>(mesh_.parts_.resize_for_overwrite(parts_));
    }

private:
    Mesh& mesh_;
    std::size_t vertices_;
    std::size_t indices_;
    std::size_t parts_;
};

Mesh::Mesh(const Allocator& allocator) noexcept : vertices_(allocator), indices_(allocator), parts_(allocator) {}

void Mesh::clear() noexcept {
    vertices_.clear();
    indices_.clear();
    parts_.clear();
}

MeshMark Mesh::mark() const noexcept {
    MeshMark now;
    now.vertices = vertices_.size();
    now.triangles = parts_.size();
    return now;
}

Status Mesh::truncate(MeshMark earlier) noexcept {
    if (earlier.vertices > vertices_.size() || earlier.triangles > parts_.size()) return Status::invalid_argument;
    static_cast<void>(vertices_.resize_for_overwrite(earlier.vertices));
    static_cast<void>(indices_.resize_for_overwrite(earlier.triangles * 3U));
    static_cast<void>(parts_.resize_for_overwrite(earlier.triangles));
    return Status::ok;
}

std::size_t Mesh::storage_bytes() const noexcept {
    const std::size_t bytes = vertices_.storage_bytes() + indices_.storage_bytes() + parts_.storage_bytes();
    return bytes;
}

namespace {

// Whether two views share any element. Growing the mesh releases its old
// storage, so an input that views the mesh itself would dangle.
template <typename Element>
bool views_overlap(std::span<const Element> first, std::span<const Element> second) noexcept {
    if (first.empty() || second.empty()) return false;
    const std::less<const Element*> before{};
    const bool starts_before_end = before(first.data(), second.data() + second.size());
    const bool ends_after_start = before(second.data(), first.data() + first.size());
    const bool overlap = starts_before_end && ends_after_start;
    return overlap;
}

} // namespace

Status Mesh::append_triangles(std::span<const MeshVertex> vertices, std::span<const std::uint32_t> indices,
                              std::uint32_t part) noexcept {
    if (indices.size() % 3U != 0U) return Status::invalid_argument;
    if (views_overlap(vertices, vertices_.view()) || views_overlap(indices, indices_.view()) ||
        views_overlap(indices, parts_.view())) {
        return Status::invalid_argument;
    }
    for (const std::uint32_t index : indices) {
        if (index >= vertices.size()) return Status::invalid_argument;
    }
    for (const MeshVertex& vertex : vertices) {
        if (!finite(vertex.position) || !finite(vertex.normal)) return Status::nonfinite_input;
    }
    MeshAppender appender(*this);
    const Status reserved = appender.reserve(vertices.size(), indices.size() / 3U);
    if (reserved != Status::ok) return reserved;
    const std::uint32_t base = appender.next_vertex();
    for (const MeshVertex& vertex : vertices) appender.vertex(vertex);
    for (std::size_t corner = 0U; corner < indices.size(); corner += 3U) {
        appender.triangle(base + indices[corner], base + indices[corner + 1U], base + indices[corner + 2U], part);
    }
    return Status::ok;
}

// -- grids ------------------------------------------------------------------------------

namespace {

Vec3 grid_point(std::span<const double> positions, std::size_t nodes_u, std::size_t u, std::size_t v) noexcept {
    const std::size_t at = (v * nodes_u + u) * 3U;
    const Vec3 point{positions[at], positions[at + 1U], positions[at + 2U]};
    return point;
}

bool cell_finite(std::span<const double> positions, std::size_t nodes_u, std::size_t u, std::size_t v) noexcept {
    const bool all = finite(grid_point(positions, nodes_u, u, v)) && finite(grid_point(positions, nodes_u, u + 1U, v)) &&
                     finite(grid_point(positions, nodes_u, u, v + 1U)) &&
                     finite(grid_point(positions, nodes_u, u + 1U, v + 1U));
    return all;
}

double grid_value(const GridInput& grid, std::size_t u, std::size_t v) noexcept {
    const std::size_t node = v * grid.nodes_u + u;
    if (!grid.values.empty()) return grid.values[node];
    const double z = grid.positions[node * 3U + 2U];
    return z;
}

// The difference across a node along one direction, from the finite
// neighbours (one-sided at an edge or a hole); zero when neither is finite.
Vec3 grid_tangent(std::span<const double> positions, std::size_t nodes_u, std::size_t nodes_v, std::size_t u,
                  std::size_t v, bool along_u) noexcept {
    const Vec3 here = grid_point(positions, nodes_u, u, v);
    Vec3 before = here;
    Vec3 after = here;
    if (along_u) {
        if (u > 0U && finite(grid_point(positions, nodes_u, u - 1U, v))) before = grid_point(positions, nodes_u, u - 1U, v);
        if (u + 1U < nodes_u && finite(grid_point(positions, nodes_u, u + 1U, v))) {
            after = grid_point(positions, nodes_u, u + 1U, v);
        }
    } else {
        if (v > 0U && finite(grid_point(positions, nodes_u, u, v - 1U))) before = grid_point(positions, nodes_u, u, v - 1U);
        if (v + 1U < nodes_v && finite(grid_point(positions, nodes_u, u, v + 1U))) {
            after = grid_point(positions, nodes_u, u, v + 1U);
        }
    }
    const Vec3 tangent = after - before;
    return tangent;
}

} // namespace

Status triangulate_grid(const GridInput& grid, Mesh& mesh) noexcept {
    if (grid.nodes_u < 2U || grid.nodes_v < 2U) return Status::invalid_argument;
    if (grid.nodes_u > std::numeric_limits<std::uint32_t>::max() / grid.nodes_v) return Status::too_large;
    const std::size_t nodes = grid.nodes_u * grid.nodes_v;
    if (grid.positions.size() != nodes * 3U) return Status::invalid_argument;
    if (!grid.values.empty() && grid.values.size() != nodes) return Status::invalid_argument;
    const std::size_t cells_u = grid.nodes_u - 1U;
    const std::size_t cells_v = grid.nodes_v - 1U;
    std::size_t shown = 0U;
    for (std::size_t v = 0U; v < cells_v; ++v) {
        for (std::size_t u = 0U; u < cells_u; ++u) {
            if (cell_finite(grid.positions, grid.nodes_u, u, v)) ++shown;
        }
    }
    const bool flat = grid.shading == GridShading::flat;
    const std::size_t vertex_count = flat ? shown * 6U : nodes;
    MeshAppender appender(mesh);
    const Status reserved = appender.reserve(vertex_count, shown * 2U);
    if (reserved != Status::ok) return reserved;
    const std::uint32_t base = appender.next_vertex();
    if (!flat) {
        // One vertex per node; a non-finite node is kept at the origin, used
        // by no triangle, so the indices stay regular.
        for (std::size_t v = 0U; v < grid.nodes_v; ++v) {
            for (std::size_t u = 0U; u < grid.nodes_u; ++u) {
                MeshVertex vertex;
                const Vec3 point = grid_point(grid.positions, grid.nodes_u, u, v);
                if (finite(point)) {
                    vertex.position = point;
                    const Vec3 along_u = grid_tangent(grid.positions, grid.nodes_u, grid.nodes_v, u, v, true);
                    const Vec3 along_v = grid_tangent(grid.positions, grid.nodes_u, grid.nodes_v, u, v, false);
                    const Vec3 normal = normalized(cross(along_u, along_v));
                    vertex.normal = length(normal) > 0.0 ? normal : Vec3{0.0, 0.0, 1.0};
                    vertex.value = grid_value(grid, u, v);
                }
                vertex.u = static_cast<double>(u);
                vertex.v = static_cast<double>(v);
                vertex.color = grid.color;
                appender.vertex(vertex);
            }
        }
    }
    for (std::size_t v = 0U; v < cells_v; ++v) {
        for (std::size_t u = 0U; u < cells_u; ++u) {
            if (!cell_finite(grid.positions, grid.nodes_u, u, v)) continue;
            // Corners counter-clockwise in (u, v): a, b, c, d.
            const std::array<std::size_t, 4> corner_u{u, u + 1U, u + 1U, u};
            const std::array<std::size_t, 4> corner_v{v, v, v + 1U, v + 1U};
            const Vec3 a = grid_point(grid.positions, grid.nodes_u, u, v);
            const Vec3 b = grid_point(grid.positions, grid.nodes_u, u + 1U, v);
            const Vec3 c = grid_point(grid.positions, grid.nodes_u, u + 1U, v + 1U);
            const Vec3 d = grid_point(grid.positions, grid.nodes_u, u, v + 1U);
            const bool along_ac = length(a - c) <= length(b - d);
            // The two triangles as corner numbers.
            const std::array<std::size_t, 6> order = along_ac ? std::array<std::size_t, 6>{0U, 1U, 2U, 0U, 2U, 3U}
                                                              : std::array<std::size_t, 6>{0U, 1U, 3U, 1U, 2U, 3U};
            for (std::size_t half = 0U; half < 2U; ++half) {
                if (!flat) {
                    std::array<std::uint32_t, 3> index{};
                    for (std::size_t k = 0U; k < 3U; ++k) {
                        const std::size_t corner = order[half * 3U + k];
                        const std::size_t node = corner_v[corner] * grid.nodes_u + corner_u[corner];
                        index[k] = base + static_cast<std::uint32_t>(node);
                    }
                    appender.triangle(index[0], index[1], index[2], grid.part);
                    continue;
                }
                std::array<Vec3, 3> points{};
                double value_sum = 0.0;
                for (std::size_t k = 0U; k < 3U; ++k) {
                    const std::size_t corner = order[half * 3U + k];
                    points[k] = grid_point(grid.positions, grid.nodes_u, corner_u[corner], corner_v[corner]);
                    value_sum = value_sum + grid_value(grid, corner_u[corner], corner_v[corner]);
                }
                const Vec3 face = normalized(cross(points[1] - points[0], points[2] - points[0]));
                const double mean = value_sum / 3.0;
                const std::uint32_t first = appender.next_vertex();
                for (std::size_t k = 0U; k < 3U; ++k) {
                    const std::size_t corner = order[half * 3U + k];
                    MeshVertex vertex;
                    vertex.position = points[k];
                    vertex.normal = length(face) > 0.0 ? face : Vec3{0.0, 0.0, 1.0};
                    vertex.u = static_cast<double>(corner_u[corner]);
                    vertex.v = static_cast<double>(corner_v[corner]);
                    vertex.value = mean;
                    vertex.color = grid.color;
                    appender.vertex(vertex);
                }
                appender.triangle(first, first + 1U, first + 2U, grid.part);
            }
        }
    }
    return Status::ok;
}

// -- the adaptive tessellation ----------------------------------------------------------

namespace {

// Integer parameter units per lattice cell: pieces are halved down to
// resolution >> depth (depth at most 6).
constexpr std::int64_t resolution = 64;
constexpr std::size_t deepest = 6U;

struct Piece final {
    std::int64_t u0 = 0;
    std::int64_t v0 = 0;
    std::int64_t u1 = 0;
    std::int64_t v1 = 0;
};

// Keys order integer parameter points by row (v, then u) or by column.
std::uint64_t row_key(std::int64_t u, std::int64_t v) noexcept {
    const std::uint64_t key = (static_cast<std::uint64_t>(v) << 32U) | static_cast<std::uint64_t>(u);
    return key;
}
std::uint64_t column_key(std::int64_t u, std::int64_t v) noexcept { return row_key(v, u); }
std::int64_t key_high(std::uint64_t key) noexcept { return static_cast<std::int64_t>(key >> 32U); }
std::int64_t key_low(std::uint64_t key) noexcept { return static_cast<std::int64_t>(key & 0xffffffffULL); }

void sort_unique(Buffer<std::uint64_t>& keys) noexcept {
    std::span<std::uint64_t> view = keys.view();
    std::sort(view.begin(), view.end());
    std::span<std::uint64_t>::iterator end = std::unique(view.begin(), view.end());
    const std::size_t kept = static_cast<std::size_t>(end - view.begin());
    static_cast<void>(keys.resize_for_overwrite(kept));
}

// The index of `key` in sorted unique `keys` (it is there by construction).
std::size_t key_index(std::span<const std::uint64_t> keys, std::uint64_t key) noexcept {
    std::span<const std::uint64_t>::iterator found = std::lower_bound(keys.begin(), keys.end(), key);
    const std::size_t index = static_cast<std::size_t>(found - keys.begin());
    return index;
}

class Tessellator final {
public:
    Tessellator(const stx_conv::surface::Surface& surface, const TessellationOptions& options,
                const Allocator& scratch) noexcept
        : surface_(surface), options_(options), pieces_(scratch), stack_(scratch), rows_(scratch), columns_(scratch),
          vertex_keys_(scratch), ring_(scratch), ring_sizes_(scratch) {
        cells_u_ = static_cast<std::int64_t>(surface.cells_u());
        cells_v_ = static_cast<std::int64_t>(surface.cells_v());
        periodic_ = surface.shape().periodic_u;
        channels_ = surface.shape().channels;
        smallest_ = resolution >> options.maximum_depth;
        angle_limit_ = options.angle_degrees > 0.0 ? std::cos(options.angle_degrees * 3.14159265358979323846 / 180.0) : -2.0;
    }

    [[nodiscard]] Status run(Mesh& mesh) noexcept {
        // The triangle budget left in the mesh bounds the splitting itself
        // (A-stx-codecs-01-F2): every kept piece becomes at least two
        // triangles, so the split stops as soon as its pieces could not fit.
        const std::size_t existing = mesh.triangle_count();
        if (existing > options_.maximum_triangles) return Status::too_large;
        piece_budget_ = (options_.maximum_triangles - existing) / 2U;
        const Status split = split_cells();
        if (split != Status::ok) return split;
        const Status filed = file_corners();
        if (filed != Status::ok) return filed;
        const Status counted = count_rings();
        if (counted != Status::ok) return counted;
        return emit(mesh);
    }

private:
    [[nodiscard]] Vec3 position(std::int64_t u, std::int64_t v) const noexcept {
        std::array<double, 4> values{};
        const double pu = static_cast<double>(u) / static_cast<double>(resolution);
        const double pv = static_cast<double>(v) / static_cast<double>(resolution);
        static_cast<void>(surface_.evaluate(pu, pv, values));
        const Vec3 point{values[0], values[1], values[2]};
        return point;
    }

    // The unit normal at integer parameters; at a pole (where every u is
    // one point) the mean of the normals a little way along v all round.
    [[nodiscard]] Vec3 normal_at(double u, double v) const noexcept {
        stx_conv::surface::SurfaceSample sample;
        static_cast<void>(surface_.evaluate_with_derivatives(u, v, sample));
        const Vec3 along_u{sample.along_u[0], sample.along_u[1], sample.along_u[2]};
        const Vec3 along_v{sample.along_v[0], sample.along_v[1], sample.along_v[2]};
        const Vec3 normal = cross(along_u, along_v);
        if (length(normal) >= 1e-9) return normalized(normal);
        const double extent_v = static_cast<double>(cells_v_);
        const double inward = v < 0.5 * extent_v ? v + 0.05 : v - 0.05;
        Vec3 sum{};
        for (std::size_t k = 0U; k < 8U; ++k) {
            const double around = static_cast<double>(cells_u_) * static_cast<double>(k) / 8.0;
            static_cast<void>(surface_.evaluate_with_derivatives(around, inward, sample));
            const Vec3 du{sample.along_u[0], sample.along_u[1], sample.along_u[2]};
            const Vec3 dv{sample.along_v[0], sample.along_v[1], sample.along_v[2]};
            sum = sum + normalized(cross(du, dv));
        }
        const Vec3 mean = normalized(sum);
        return mean;
    }

    [[nodiscard]] Vec3 corner_normal(std::int64_t u, std::int64_t v) const noexcept {
        const Vec3 normal = normal_at(static_cast<double>(u) / static_cast<double>(resolution),
                                      static_cast<double>(v) / static_cast<double>(resolution));
        return normal;
    }

    [[nodiscard]] bool hidden(std::int64_t cell_u, std::int64_t cell_v) const noexcept {
        if (options_.hidden_cells.empty()) return false;
        const std::size_t at = static_cast<std::size_t>(cell_v * cells_u_ + cell_u);
        return options_.hidden_cells[at] != 0U;
    }

    // Splits every shown cell (depth first, in a fixed order) into pieces.
    [[nodiscard]] Status split_cells() noexcept {
        for (std::int64_t j = 0; j < cells_v_; ++j) {
            for (std::int64_t i = 0; i < cells_u_; ++i) {
                if (hidden(i, j)) continue;
                const Piece cell{i * resolution, j * resolution, (i + 1) * resolution, (j + 1) * resolution};
                stack_.clear();
                if (!stack_.push_back(cell)) return Status::out_of_memory;
                while (!stack_.empty()) {
                    const Piece piece = stack_[stack_.size() - 1U];
                    static_cast<void>(stack_.resize_for_overwrite(stack_.size() - 1U));
                    const Status placed = split_one(piece);
                    if (placed != Status::ok) return placed;
                }
            }
        }
        return Status::ok;
    }

    // Decides one piece: kept, or its halves or quarters pushed so that they
    // come off the stack in reading order.
    [[nodiscard]] Status split_one(const Piece& r) noexcept {
        const std::int64_t mu = (r.u0 + r.u1) / 2;
        const std::int64_t mv = (r.v0 + r.v1) / 2;
        const bool can_u = r.u1 - r.u0 > smallest_;
        const bool can_v = r.v1 - r.v0 > smallest_;
        const Vec3 p00 = position(r.u0, r.v0);
        const Vec3 p10 = position(r.u1, r.v0);
        const Vec3 p01 = position(r.u0, r.v1);
        const Vec3 p11 = position(r.u1, r.v1);
        double error_u = 0.0;
        double error_v = 0.0;
        double error_centre = 0.0;
        if (can_u) {
            const double low = length(position(mu, r.v0) - (p00 + p10) * 0.5);
            const double high = length(position(mu, r.v1) - (p01 + p11) * 0.5);
            error_u = std::max(low, high);
        }
        if (can_v) {
            const double low = length(position(r.u0, mv) - (p00 + p01) * 0.5);
            const double high = length(position(r.u1, mv) - (p10 + p11) * 0.5);
            error_v = std::max(low, high);
        }
        // The centre against the diagonal emit draws through a four-cornered
        // piece (the shorter one), not against the bilinear mean of the
        // corners: the mean hides the surface's twist, which the two flat
        // triangles do not follow. With the edge and diagonal midpoints all
        // within the tolerance, a quadratic surface keeps every triangle's
        // centroid within 28/9 of it.
        if (can_u && can_v) {
            const Vec3 diagonal_mid = length(p00 - p11) <= length(p10 - p01) ? (p00 + p11) * 0.5 : (p10 + p01) * 0.5;
            error_centre = length(position(mu, mv) - diagonal_mid);
        }
        bool cut_u = error_u > options_.tolerance;
        bool cut_v = error_v > options_.tolerance;
        if (angle_limit_ > -1.5) {
            const Vec3 n00 = corner_normal(r.u0, r.v0);
            const Vec3 n10 = corner_normal(r.u1, r.v0);
            const Vec3 n01 = corner_normal(r.u0, r.v1);
            const Vec3 n11 = corner_normal(r.u1, r.v1);
            if (can_u && (dot(n00, n10) < angle_limit_ || dot(n01, n11) < angle_limit_)) cut_u = true;
            if (can_v && (dot(n00, n01) < angle_limit_ || dot(n10, n11) < angle_limit_)) cut_v = true;
        }
        if (!cut_u && !cut_v && error_centre > options_.tolerance) {
            cut_u = true;
            cut_v = true;
        }
        if (cut_u && cut_v) {
            const std::array<Piece, 4> quarters{Piece{mu, mv, r.u1, r.v1}, Piece{r.u0, mv, mu, r.v1},
                                                Piece{mu, r.v0, r.u1, mv}, Piece{r.u0, r.v0, mu, mv}};
            for (const Piece& quarter : quarters) {
                if (!stack_.push_back(quarter)) return Status::out_of_memory;
            }
            return Status::ok;
        }
        if (cut_u) {
            if (!stack_.push_back(Piece{mu, r.v0, r.u1, r.v1})) return Status::out_of_memory;
            if (!stack_.push_back(Piece{r.u0, r.v0, mu, r.v1})) return Status::out_of_memory;
            return Status::ok;
        }
        if (cut_v) {
            if (!stack_.push_back(Piece{r.u0, mv, r.u1, r.v1})) return Status::out_of_memory;
            if (!stack_.push_back(Piece{r.u0, r.v0, r.u1, mv})) return Status::out_of_memory;
            return Status::ok;
        }
        if (pieces_.size() >= piece_budget_) return Status::too_large;
        if (!pieces_.push_back(r)) return Status::out_of_memory;
        return Status::ok;
    }

    // Files a corner under its row and its column. A corner on the first or
    // closing column of a periodic surface is on both.
    [[nodiscard]] bool file(std::int64_t u, std::int64_t v) noexcept {
        if (!rows_.push_back(row_key(u, v)) || !columns_.push_back(column_key(u, v))) return false;
        const std::int64_t turn = cells_u_ * resolution;
        if (periodic_ && (u == 0 || u == turn)) {
            const std::int64_t other = turn - u;
            if (!rows_.push_back(row_key(other, v)) || !columns_.push_back(column_key(other, v))) return false;
        }
        return true;
    }

    [[nodiscard]] Status file_corners() noexcept {
        for (std::size_t k = 0U; k < pieces_.size(); ++k) {
            const Piece r = pieces_[k];
            const bool filed = file(r.u0, r.v0) && file(r.u1, r.v0) && file(r.u0, r.v1) && file(r.u1, r.v1);
            if (!filed) return Status::out_of_memory;
        }
        sort_unique(rows_);
        sort_unique(columns_);
        return Status::ok;
    }

    // Appends the piece's border (counter-clockwise in (u, v), every vertex on
    // it) to ring_ as row keys.
    [[nodiscard]] bool border(const Piece& r) noexcept {
        const std::span<const std::uint64_t> rows = rows_.view();
        const std::span<const std::uint64_t> columns = columns_.view();
        // Bottom: v0, u0 <= u < u1.
        std::size_t at = key_index(rows, row_key(r.u0, r.v0));
        while (at < rows.size() && key_high(rows[at]) == r.v0 && key_low(rows[at]) < r.u1) {
            if (!ring_.push_back(rows[at])) return false;
            ++at;
        }
        // Right: u1, v0 <= v < v1.
        at = key_index(columns, column_key(r.u1, r.v0));
        while (at < columns.size() && key_high(columns[at]) == r.u1 && key_low(columns[at]) < r.v1) {
            if (!ring_.push_back(row_key(r.u1, key_low(columns[at])))) return false;
            ++at;
        }
        // Top: v1, from u1 down to just past u0.
        at = key_index(rows, row_key(r.u1, r.v1));
        while (at < rows.size() && key_high(rows[at]) == r.v1 && key_low(rows[at]) > r.u0) {
            if (!ring_.push_back(rows[at])) return false;
            if (at == 0U) break;
            --at;
        }
        // Left: u0, from v1 down to just past v0.
        at = key_index(columns, column_key(r.u0, r.v1));
        while (at < columns.size() && key_high(columns[at]) == r.u0 && key_low(columns[at]) > r.v0) {
            if (!ring_.push_back(row_key(r.u0, key_low(columns[at])))) return false;
            if (at == 0U) break;
            --at;
        }
        return true;
    }

    // Every piece's ring size, the vertex set (corners and the centres of
    // pieces whose ring has more than four vertices) and the triangle count.
    [[nodiscard]] Status count_rings() noexcept {
        if (!ring_sizes_.reserve(pieces_.size())) return Status::out_of_memory;
        if (!vertex_keys_.reserve(rows_.size())) return Status::out_of_memory;
        for (std::size_t k = 0U; k < rows_.size(); ++k) static_cast<void>(vertex_keys_.push_back(rows_[k]));
        triangles_ = 0U;
        for (std::size_t k = 0U; k < pieces_.size(); ++k) {
            const Piece r = pieces_[k];
            ring_.clear();
            if (!border(r)) return Status::out_of_memory;
            const std::uint32_t size = static_cast<std::uint32_t>(ring_.size());
            static_cast<void>(ring_sizes_.push_back(size));
            if (size == 4U) {
                triangles_ = triangles_ + 2U;
            } else {
                triangles_ = triangles_ + size;
                if (!vertex_keys_.push_back(row_key((r.u0 + r.u1) / 2, (r.v0 + r.v1) / 2))) return Status::out_of_memory;
            }
        }
        sort_unique(vertex_keys_);
        return Status::ok;
    }

    [[nodiscard]] MeshVertex make_vertex(std::uint64_t key) const noexcept {
        const std::int64_t iu = key_low(key);
        const std::int64_t iv = key_high(key);
        const double u = static_cast<double>(iu) / static_cast<double>(resolution);
        const double v = static_cast<double>(iv) / static_cast<double>(resolution);
        std::array<double, 4> values{};
        static_cast<void>(surface_.evaluate(u, v, values));
        MeshVertex vertex;
        vertex.position = Vec3{values[0], values[1], values[2]};
        vertex.normal = normal_at(u, v);
        vertex.u = u;
        vertex.v = v;
        vertex.value = options_.value_channel < channels_ ? values[options_.value_channel] : values[2];
        vertex.color = options_.color;
        return vertex;
    }

    [[nodiscard]] Status emit(Mesh& mesh) noexcept {
        const std::size_t existing = mesh.triangle_count();
        if (triangles_ > options_.maximum_triangles || existing > options_.maximum_triangles - triangles_) {
            return Status::too_large;
        }
        MeshAppender appender(mesh);
        const Status reserved = appender.reserve(vertex_keys_.size(), triangles_);
        if (reserved != Status::ok) return reserved;
        const std::uint32_t base = appender.next_vertex();
        for (std::size_t k = 0U; k < vertex_keys_.size(); ++k) {
            const MeshVertex vertex = make_vertex(vertex_keys_[k]);
            if (!finite(vertex.position) || !finite(vertex.normal)) {
                appender.roll_back();
                return Status::nonfinite_input;
            }
            appender.vertex(vertex);
        }
        const std::span<const std::uint64_t> keys = vertex_keys_.view();
        for (std::size_t k = 0U; k < pieces_.size(); ++k) {
            const Piece r = pieces_[k];
            ring_.clear();
            if (!border(r)) {
                appender.roll_back();
                return Status::out_of_memory;
            }
            const std::size_t size = ring_.size();
            if (size == 4U) {
                std::array<std::uint32_t, 4> corner{};
                for (std::size_t c = 0U; c < 4U; ++c) {
                    corner[c] = base + static_cast<std::uint32_t>(key_index(keys, ring_[c]));
                }
                // The shorter diagonal.
                const std::span<const MeshVertex> vertices = mesh.vertices();
                const Vec3 a = vertices[corner[0]].position;
                const Vec3 b = vertices[corner[1]].position;
                const Vec3 c = vertices[corner[2]].position;
                const Vec3 d = vertices[corner[3]].position;
                if (length(a - c) <= length(b - d)) {
                    appender.triangle(corner[0], corner[1], corner[2], options_.part);
                    appender.triangle(corner[0], corner[2], corner[3], options_.part);
                } else {
                    appender.triangle(corner[0], corner[1], corner[3], options_.part);
                    appender.triangle(corner[1], corner[2], corner[3], options_.part);
                }
                continue;
            }
            const std::uint64_t centre_key = row_key((r.u0 + r.u1) / 2, (r.v0 + r.v1) / 2);
            const std::uint32_t centre = base + static_cast<std::uint32_t>(key_index(keys, centre_key));
            for (std::size_t c = 0U; c < size; ++c) {
                const std::uint32_t from = base + static_cast<std::uint32_t>(key_index(keys, ring_[c]));
                const std::uint32_t to = base + static_cast<std::uint32_t>(key_index(keys, ring_[(c + 1U) % size]));
                appender.triangle(centre, from, to, options_.part);
            }
        }
        return Status::ok;
    }

    const stx_conv::surface::Surface& surface_;
    const TessellationOptions& options_;
    Buffer<Piece> pieces_;
    Buffer<Piece> stack_;
    Buffer<std::uint64_t> rows_;
    Buffer<std::uint64_t> columns_;
    Buffer<std::uint64_t> vertex_keys_;
    Buffer<std::uint64_t> ring_;
    Buffer<std::uint32_t> ring_sizes_;
    std::int64_t cells_u_ = 0;
    std::int64_t cells_v_ = 0;
    std::int64_t smallest_ = 1;
    std::size_t channels_ = 0U;
    std::size_t triangles_ = 0U;
    // The most pieces the split may keep: half the triangles the mesh has
    // room for under options_.maximum_triangles (set by run).
    std::size_t piece_budget_ = 0U;
    double angle_limit_ = -2.0;
    bool periodic_ = false;
};

} // namespace

Status tessellate_surface(const stx_conv::surface::Surface& surface, const TessellationOptions& options,
                          const Allocator& scratch, Mesh& mesh) noexcept {
    if (!surface.ready() || surface.shape().channels < 3U) return Status::invalid_argument;
    if (!(options.tolerance > 0.0) || !std::isfinite(options.tolerance)) return Status::invalid_argument;
    if (!std::isfinite(options.angle_degrees) || options.maximum_depth > deepest) return Status::invalid_argument;
    if (!allocator_valid(scratch)) return Status::invalid_argument;
    const std::size_t cells = surface.cells_u() * surface.cells_v();
    if (!options.hidden_cells.empty() && options.hidden_cells.size() != cells) return Status::invalid_argument;
    // Integer parameters must fit the 32-bit halves of the keys.
    if (surface.cells_u() + 1U > (std::size_t{1} << 24U) || surface.cells_v() + 1U > (std::size_t{1} << 24U)) {
        return Status::too_large;
    }
    Tessellator tessellator(surface, options, scratch);
    const Status result = tessellator.run(mesh);
    return result;
}

} // namespace stx_raster3d
