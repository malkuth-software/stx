#pragma once
// Triangle meshes: the renderer's geometry. A mesh is built by appending
// parts: an adaptive tessellation of a CONV surface, a grid triangulated
// as it stands (smooth or flat), or the caller's own triangles. Each
// triangle carries the part it came from, which selects its material and is
// what picking reports.

#include <stx_raster3d/geometry.hpp>
#include <stx_raster3d/memory.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace stx_conv::surface {
class Surface;
}

namespace stx_raster3d {

// A vertex: position, unit normal, the surface parameters it lies at (node
// units of its part; 0 for caller triangles that give none), the value its
// colour map reads, and a colour for parts coloured per vertex.
struct MeshVertex final {
    Vec3 position{};
    Vec3 normal{0.0, 0.0, 1.0};
    double u = 0.0;
    double v = 0.0;
    double value = 0.0;
    Color color{};
};

// A mesh's size at one moment (Mesh::mark): what truncate returns it to.
struct MeshMark final {
    std::size_t vertices = 0U;
    std::size_t triangles = 0U;
};

// Owns vertices, triangles (three vertex indices each, counter-clockwise
// seen from the side the normal points to) and each triangle's part.
class Mesh final {
public:
    explicit Mesh(const Allocator& allocator) noexcept;
    Mesh(Mesh&&) noexcept = default;
    Mesh& operator=(Mesh&&) noexcept = default;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // Forgets every part and keeps the storage.
    void clear() noexcept;
    // The current size, to come back to with truncate.
    [[nodiscard]] MeshMark mark() const noexcept;
    // Forgets everything appended since `earlier` was marked, keeping the
    // storage: a caller keeps a retained part (a surface) and replaces what
    // follows it (per-view geometry) without rebuilding the part. Since a
    // mesh only grows by whole appends, the kept triangles index only kept
    // vertices. invalid_argument, changing nothing, for a mark past the
    // current size.
    [[nodiscard]] Status truncate(MeshMark earlier) noexcept;
    [[nodiscard]] std::size_t vertex_count() const noexcept { return vertices_.size(); }
    [[nodiscard]] std::size_t triangle_count() const noexcept { return parts_.size(); }
    [[nodiscard]] std::span<const MeshVertex> vertices() const& noexcept { return vertices_.view(); }
    std::span<const MeshVertex> vertices() const&& = delete;
    [[nodiscard]] std::span<const std::uint32_t> indices() const& noexcept { return indices_.view(); }
    std::span<const std::uint32_t> indices() const&& = delete;
    [[nodiscard]] std::span<const std::uint32_t> triangle_parts() const& noexcept { return parts_.view(); }
    std::span<const std::uint32_t> triangle_parts() const&& = delete;
    [[nodiscard]] std::size_t storage_bytes() const noexcept;

    // Appends caller triangles: `indices` (three per triangle) index
    // `vertices`, which are appended after the mesh's own. invalid_argument
    // for an index past `vertices` or a count not a multiple of three,
    // nonfinite_input for a non-finite position or normal; out_of_memory or
    // too_large (past 2^32 - 1 vertices). On failure nothing is appended.
    [[nodiscard]] Status append_triangles(std::span<const MeshVertex> vertices, std::span<const std::uint32_t> indices,
                                          std::uint32_t part) noexcept;

private:
    friend class MeshAppender;
    Buffer<MeshVertex> vertices_;
    Buffer<std::uint32_t> indices_;
    Buffer<std::uint32_t> parts_;
};

// The adaptive tessellation of a CONV surface (the conv-teapot tree layout):
// every lattice cell is halved along u, v or both until the surface at the
// middle of each edge lies within `tolerance` (world units) of the edge and
// at the middle of the piece within it of the diagonal drawn across it
// (not of the corners' mean, which hides a twist), and, with
// `angle_degrees` above zero, until the
// normal turns no more than that along an edge. Neighbours of different
// depth meet without cracks: a piece is a fan over every vertex its
// neighbours left on its border.
struct TessellationOptions final {
    double tolerance = 2e-3;
    double angle_degrees = 0.0;
    // Halvings per cell and direction, at most 6 (64 pieces).
    std::size_t maximum_depth = 5U;
    // A budget for the whole mesh after this part.
    std::size_t maximum_triangles = 4000000U;
    // The channel holding each vertex's value, or 3 channels' z (channel 2)
    // when it is past the surface's channels.
    std::size_t value_channel = 3U;
    // Empty, or one entry per lattice cell ([v][u]), nonzero where the cell
    // is a hole: no triangle is made over it.
    std::span<const std::uint8_t> hidden_cells{};
    std::uint32_t part = 0U;
    Color color{};
};

// Appends the tessellation of a ready surface of three or more channels
// (x, y, z first). invalid_argument for an unprepared or narrow surface, a
// tolerance that is not positive, a depth past 6 or a hidden-cell table of
// the wrong length; too_large when the budget would be passed;
// out_of_memory. On failure nothing is appended. `scratch` provides the
// working storage, released before returning.
[[nodiscard]] Status tessellate_surface(const stx_conv::surface::Surface& surface, const TessellationOptions& options,
                                        const Allocator& scratch, Mesh& mesh) noexcept;

enum class GridShading : std::uint8_t {
    smooth, // shared vertices, normals from the neighbouring nodes, values interpolated
    flat    // each triangle its own vertices, its face normal and the mean of its corner values
};

// A grid of nodes taken as it stands: positions [v][u] (x, y, z), and
// optionally one value per node (empty: z). A cell with a non-finite
// corner is a hole.
struct GridInput final {
    std::span<const double> positions{};
    std::span<const double> values{};
    std::size_t nodes_u = 0U;
    std::size_t nodes_v = 0U;
    GridShading shading = GridShading::smooth;
    std::uint32_t part = 0U;
    Color color{};
};

// Appends the grid's cells, two triangles each (the shorter diagonal).
// invalid_argument for fewer than two nodes either way or spans of the
// wrong length; too_large past 2^32 - 1 vertices; out_of_memory. On failure
// nothing is appended.
[[nodiscard]] Status triangulate_grid(const GridInput& grid, Mesh& mesh) noexcept;

} // namespace stx_raster3d
