// stx_raster3d: buffers under a refusing allocator, the camera conventions,
// shared edges without overlap or gap, depth order independent of draw
// order, the same bytes under any executor order, anti-aliased coverage,
// the adaptive mesher (flat cells, tolerance, no cracks, holes), grids,
// lines and markers against surfaces, picking and its Newton refinement,
// iso-lines, refusals that keep the previous frame, and clipping to the
// near plane and the picture.

#include <stx_raster3d/camera.hpp>
#include <stx_raster3d/executor.hpp>
#include <stx_raster3d/geometry.hpp>
#include <stx_raster3d/memory.hpp>
#include <stx_raster3d/mesh.hpp>
#include <stx_raster3d/picking.hpp>
#include <stx_raster3d/raster.hpp>

#include <stx_conv/surface.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace {

namespace r3 = stx_raster3d;
namespace cs = stx_conv::surface;

void require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

bool near_value(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

// -- allocators and executors -----------------------------------------------------------

// Grants `remaining` allocations, then refuses.
struct Rationed final {
    std::size_t remaining = 0U;
};

void* rationed_allocate(void* context, std::size_t bytes, std::size_t alignment) noexcept {
    Rationed& ration = *static_cast<Rationed*>(context);
    if (ration.remaining == 0U) return nullptr;
    --ration.remaining;
    const r3::Allocator system = r3::system_allocator();
    void* const address = system.allocate(system.context, bytes, alignment);
    return address;
}

void rationed_release(void*, void* address, std::size_t bytes, std::size_t alignment) noexcept {
    const r3::Allocator system = r3::system_allocator();
    system.release(system.context, address, bytes, alignment);
}

r3::Allocator rationed(Rationed& ration) {
    r3::Allocator allocator;
    allocator.context = &ration;
    allocator.allocate = &rationed_allocate;
    allocator.release = &rationed_release;
    return allocator;
}

void run_reversed(void*, std::size_t count, r3::Task task) noexcept {
    for (std::size_t index = count; index > 0U; --index) task.run(task.context, index - 1U);
}

r3::Executor reversed_executor() {
    r3::Executor executor;
    executor.run = &run_reversed;
    return executor;
}

// -- scenes -----------------------------------------------------------------------------

// Looking straight down at the origin: world x right, world y up, one
// world unit per pixel over a side by side picture.
r3::View top_down(std::size_t side) {
    r3::Camera camera;
    camera.eye = r3::Vec3{0.0, 0.0, 10.0};
    camera.target = r3::Vec3{};
    camera.up = r3::Vec3{0.0, 1.0, 0.0};
    camera.projection = r3::Projection::orthographic;
    camera.orthographic_height = static_cast<double>(side);
    r3::View view;
    require(r3::make_view(camera, side, side, view) == r3::Status::ok, "the top-down view is made");
    return view;
}

r3::MeshVertex vertex_at(double x, double y, double z) {
    r3::MeshVertex vertex;
    vertex.position = r3::Vec3{x, y, z};
    vertex.normal = r3::Vec3{0.0, 0.0, 1.0};
    return vertex;
}

// An axis-aligned square at height z as two triangles of one part.
void append_square(r3::Mesh& mesh, double low, double high, double z, std::uint32_t part) {
    const std::array<r3::MeshVertex, 4> corners{vertex_at(low, low, z), vertex_at(high, low, z),
                                                vertex_at(high, high, z), vertex_at(low, high, z)};
    const std::array<std::uint32_t, 6> indices{0U, 1U, 2U, 0U, 2U, 3U};
    require(mesh.append_triangles(corners, indices, part) == r3::Status::ok, "a square is appended");
}

r3::Material flat_material(float red, float green, float blue) {
    r3::Material material;
    material.color = r3::Color{red, green, blue, 1.0f};
    return material;
}

std::array<std::uint8_t, 4> pixel(const r3::Renderer& renderer, std::size_t x, std::size_t y) {
    const std::span<const std::uint8_t> picture = renderer.picture();
    const std::size_t at = (y * renderer.width() + x) * 4U;
    const std::array<std::uint8_t, 4> bgra{picture[at], picture[at + 1U], picture[at + 2U], picture[at + 3U]};
    return bgra;
}

// A curved CONV surface: x = u, y = v, z a smooth bump.
cs::Surface bump_surface(std::size_t nodes_u, std::size_t nodes_v) {
    cs::SurfaceShape shape;
    shape.nodes_u = nodes_u;
    shape.nodes_v = nodes_v;
    shape.channels = 3U;
    cs::Surface surface(shape);
    std::vector<double> nodes;
    for (std::size_t v = 0U; v < nodes_v; ++v) {
        for (std::size_t u = 0U; u < nodes_u; ++u) {
            const double x = static_cast<double>(u);
            const double y = static_cast<double>(v);
            nodes.push_back(x);
            nodes.push_back(y);
            nodes.push_back(0.4 * std::sin(0.9 * x) * std::cos(0.7 * y));
        }
    }
    require(surface.compile(nodes, {}, {}) == stx_conv::Status::ok, "the bump surface compiles");
    return surface;
}

cs::Surface plane_surface(std::size_t nodes_u, std::size_t nodes_v) {
    cs::SurfaceShape shape;
    shape.nodes_u = nodes_u;
    shape.nodes_v = nodes_v;
    shape.channels = 3U;
    cs::Surface surface(shape);
    std::vector<double> nodes;
    for (std::size_t v = 0U; v < nodes_v; ++v) {
        for (std::size_t u = 0U; u < nodes_u; ++u) {
            nodes.push_back(static_cast<double>(u));
            nodes.push_back(static_cast<double>(v));
            nodes.push_back(0.0);
        }
    }
    require(surface.compile(nodes, {}, {}) == stx_conv::Status::ok, "the plane compiles");
    return surface;
}

// -- tests ------------------------------------------------------------------------------

void test_buffer() {
    Rationed ration;
    ration.remaining = 1U;
    r3::Buffer<std::uint32_t> buffer(rationed(ration));
    for (std::uint32_t k = 0U; k < 16U; ++k) require(buffer.push_back(k), "the first block holds 16");
    require(!buffer.push_back(16U), "a refused growth is reported");
    require(buffer.size() == 16U && buffer.capacity() == 16U, "a refused growth changes nothing");
    require(buffer[15] == 15U, "a refused growth keeps the contents");
    r3::Buffer<std::uint32_t> moved(std::move(buffer));
    require(moved.size() == 16U && buffer.size() == 0U && buffer.capacity() == 0U, "a move transfers the storage");
    moved.release();
    require(moved.capacity() == 0U, "release gives the storage back");
}

void test_camera() {
    const r3::Camera side = r3::orbit_camera(r3::Vec3{}, 10.0, 0.0, 0.0, 0.0, r3::Projection::perspective);
    require(near_value(side.eye.x, 10.0, 1e-12) && near_value(side.eye.y, 0.0, 1e-12) && near_value(side.eye.z, 0.0, 1e-12),
            "elevation 0, azimuth 0 looks from +x");
    require(near_value(side.up.z, 1.0, 1e-12), "the side view's up is z");
    const r3::Camera quarter = r3::orbit_camera(r3::Vec3{}, 10.0, 0.0, 90.0, 0.0, r3::Projection::perspective);
    require(near_value(quarter.eye.y, 10.0, 1e-12), "azimuth 90 looks from +y");
    const r3::Camera above = r3::orbit_camera(r3::Vec3{}, 10.0, 90.0, 0.0, 0.0, r3::Projection::perspective);
    require(near_value(above.eye.z, 10.0, 1e-12) && near_value(above.up.x, -1.0, 1e-12), "the pole keeps its azimuth's up");
    const r3::Camera rolled = r3::orbit_camera(r3::Vec3{}, 10.0, 30.0, 45.0, 90.0, r3::Projection::perspective);
    const r3::Vec3 forward = r3::normalized(rolled.target - rolled.eye);
    require(near_value(r3::dot(rolled.up, forward), 0.0, 1e-12) && near_value(r3::length(rolled.up), 1.0, 1e-12),
            "roll keeps up a unit vector across the view");

    r3::View view;
    require(r3::make_view(side, 200U, 100U, view) == r3::Status::ok, "the side view is made");
    const r3::ProjectedPoint centre = view.project(r3::Vec3{});
    require(near_value(centre.x, 100.0, 1e-9) && near_value(centre.y, 50.0, 1e-9) && near_value(centre.depth, 10.0, 1e-9),
            "the target projects to the centre at its distance");
    // Focal length 1: a unit up at depth 10 is half_height / 10 pixels up.
    const r3::ProjectedPoint raised = view.project(r3::Vec3{0.0, 0.0, 1.0});
    require(near_value(raised.y, 50.0 - 5.0, 1e-9), "perspective scale follows the focal length");
    r3::Vec3 origin{};
    r3::Vec3 direction{};
    view.ray(raised.x, raised.y, origin, direction);
    const r3::Vec3 back = origin + direction * raised.depth;
    require(near_value(back.z, 1.0, 1e-9) && near_value(back.x, 0.0, 1e-9), "the ray returns through the point");

    const r3::View flat = top_down(16U);
    const r3::ProjectedPoint corner = flat.project(r3::Vec3{3.0, 2.0, 0.0});
    require(near_value(corner.x, 11.0, 1e-12) && near_value(corner.y, 6.0, 1e-12), "orthographic: one unit per pixel");

    r3::Camera degenerate;
    degenerate.target = degenerate.eye;
    r3::View untouched = view;
    require(r3::make_view(degenerate, 10U, 10U, untouched) == r3::Status::invalid_argument, "an eye on the target is refused");
    require(untouched.width() == 200U, "a refused view is unchanged");
}

// Coverage of every pixel of a frame, as surface picks of a given triangle.
std::vector<int> covered_by(const r3::Renderer& renderer, const r3::Mesh& mesh, std::size_t side) {
    std::vector<int> owner(side * side, -1);
    for (std::size_t y = 0U; y < side; ++y) {
        for (std::size_t x = 0U; x < side; ++x) {
            const r3::Pick pick = renderer.pick(x, y, &mesh);
            if (pick.kind == r3::PickKind::surface) owner[y * side + x] = static_cast<int>(pick.index);
        }
    }
    return owner;
}

void test_shared_edge() {
    const std::size_t side = 16U;
    const r3::View view = top_down(side);
    const r3::Allocator system = r3::system_allocator();
    // A square whose diagonal passes through pixel centres, split along it.
    const std::array<r3::MeshVertex, 4> corners{vertex_at(-6.0, -6.0, 0.0), vertex_at(6.0, -6.0, 0.0),
                                                vertex_at(6.0, 6.0, 0.0), vertex_at(-6.0, 6.0, 0.0)};
    const std::array<std::uint32_t, 3> first{0U, 1U, 2U};
    const std::array<std::uint32_t, 3> second{0U, 2U, 3U};
    const std::array<std::uint32_t, 6> both{0U, 1U, 2U, 0U, 2U, 3U};
    r3::Mesh mesh_a(system);
    r3::Mesh mesh_b(system);
    r3::Mesh mesh_ab(system);
    require(mesh_a.append_triangles(corners, first, 0U) == r3::Status::ok, "triangle a");
    require(mesh_b.append_triangles(corners, second, 0U) == r3::Status::ok, "triangle b");
    require(mesh_ab.append_triangles(corners, both, 0U) == r3::Status::ok, "both triangles");
    r3::Renderer renderer(system);
    r3::Scene scene;
    scene.mesh = &mesh_a;
    require(renderer.render(scene, view, 1U, r3::inline_executor()) == r3::Status::ok, "a renders");
    const std::vector<int> a = covered_by(renderer, mesh_a, side);
    scene.mesh = &mesh_b;
    require(renderer.render(scene, view, 1U, r3::inline_executor()) == r3::Status::ok, "b renders");
    const std::vector<int> b = covered_by(renderer, mesh_b, side);
    scene.mesh = &mesh_ab;
    require(renderer.render(scene, view, 1U, r3::inline_executor()) == r3::Status::ok, "both render");
    const std::vector<int> ab = covered_by(renderer, mesh_ab, side);
    std::size_t on_diagonal = 0U;
    for (std::size_t k = 0U; k < side * side; ++k) {
        require(!(a[k] >= 0 && b[k] >= 0), "triangles sharing an edge do not overlap");
        require((a[k] >= 0 || b[k] >= 0) == (ab[k] >= 0), "triangles sharing an edge leave no gap");
        const std::size_t x = k % side;
        const std::size_t y = k / side;
        if (x + y == 15U && x >= 2U && x < 14U) {
            ++on_diagonal;
            require(ab[k] >= 0, "a sample on the shared edge is drawn once");
        }
    }
    require(on_diagonal == 12U, "the diagonal crosses twelve pixel centres");
    // The square spans pixels 2 to 13 both ways: 144 pixels.
    std::size_t count = 0U;
    for (const int owner : ab) {
        if (owner >= 0) ++count;
    }
    require(count == 144U, "the square covers exactly its pixels");
}

void test_depth_order() {
    const r3::Allocator system = r3::system_allocator();
    const std::array<r3::Material, 2> materials{flat_material(1.0f, 0.0f, 0.0f), flat_material(0.0f, 0.0f, 1.0f)};
    const std::array<r3::Projection, 2> projections{r3::Projection::orthographic, r3::Projection::perspective};
    for (const r3::Projection projection : projections) {
        r3::Camera camera;
        camera.eye = r3::Vec3{0.0, 0.0, 10.0};
        camera.up = r3::Vec3{0.0, 1.0, 0.0};
        camera.projection = projection;
        camera.orthographic_height = 16.0;
        camera.focal_length = 2.0;
        r3::View view;
        require(r3::make_view(camera, 16U, 16U, view) == r3::Status::ok, "the depth view is made");
        for (std::size_t order = 0U; order < 2U; ++order) {
            r3::Mesh mesh(system);
            if (order == 0U) {
                append_square(mesh, -3.0, 3.0, 1.0, 0U);
                append_square(mesh, -4.0, 4.0, 0.0, 1U);
            } else {
                append_square(mesh, -4.0, 4.0, 0.0, 1U);
                append_square(mesh, -3.0, 3.0, 1.0, 0U);
            }
            r3::Scene scene;
            scene.mesh = &mesh;
            scene.materials = materials;
            r3::Renderer renderer(system);
            require(renderer.render(scene, view, 2U, r3::inline_executor()) == r3::Status::ok, "the depth scene renders");
            const std::array<std::uint8_t, 4> centre = pixel(renderer, 8U, 8U);
            require(centre[2] == 255U && centre[0] == 0U, "the nearer square wins whatever the order");
            const r3::Pick pick = renderer.pick(8U, 8U, &mesh);
            require(pick.kind == r3::PickKind::surface && pick.item == 0U, "the pick reports the nearer part");
            require(near_value(pick.depth, 9.0, 1e-6) && near_value(pick.position.z, 1.0, 1e-6), "the pick's depth");
        }
    }
}

void test_coverage() {
    const r3::Allocator system = r3::system_allocator();
    const r3::View view = top_down(16U);
    r3::Mesh mesh(system);
    // From x = -6 to x = 0.5: pixel 8 (world 0 to 1) is half covered.
    const std::array<r3::MeshVertex, 4> corners{vertex_at(-6.0, -6.0, 0.0), vertex_at(0.5, -6.0, 0.0),
                                                vertex_at(0.5, 6.0, 0.0), vertex_at(-6.0, 6.0, 0.0)};
    const std::array<std::uint32_t, 6> indices{0U, 1U, 2U, 0U, 2U, 3U};
    require(mesh.append_triangles(corners, indices, 0U) == r3::Status::ok, "the half square");
    const std::array<r3::Material, 1> materials{flat_material(1.0f, 1.0f, 1.0f)};
    r3::Scene scene;
    scene.mesh = &mesh;
    scene.materials = materials;
    r3::Renderer renderer(system);
    require(renderer.render(scene, view, 4U, r3::inline_executor()) == r3::Status::ok, "the coverage scene renders");
    const std::array<std::uint8_t, 4> full = pixel(renderer, 7U, 8U);
    const std::array<std::uint8_t, 4> half = pixel(renderer, 8U, 8U);
    const std::array<std::uint8_t, 4> none = pixel(renderer, 9U, 8U);
    require(full[3] == 255U && full[0] == 255U, "a covered pixel is opaque");
    require(half[3] == 128U && half[0] == 128U, "a half-covered pixel is half, premultiplied");
    require(none[3] == 0U, "an uncovered pixel is the background");
    require(renderer.statistics().sample_count == 16U * 16U * 16U, "sixteen samples per pixel");
}

// Every interior edge is shared by exactly two triangles; a boundary edge
// lies on the parameter boundary.
void check_crack_free(const r3::Mesh& mesh, double extent_u, double extent_v) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
    const std::span<const std::uint32_t> indices = mesh.indices();
    for (std::size_t t = 0U; t < mesh.triangle_count(); ++t) {
        for (std::size_t k = 0U; k < 3U; ++k) {
            std::uint32_t a = indices[t * 3U + k];
            std::uint32_t b = indices[t * 3U + (k + 1U) % 3U];
            if (a > b) std::swap(a, b);
            edges[std::make_pair(a, b)] += 1;
        }
    }
    const std::span<const r3::MeshVertex> vertices = mesh.vertices();
    for (const std::pair<const std::pair<std::uint32_t, std::uint32_t>, int>& edge : edges) {
        require(edge.second <= 2, "no edge has three triangles");
        if (edge.second == 2) continue;
        const r3::MeshVertex& a = vertices[edge.first.first];
        const r3::MeshVertex& b = vertices[edge.first.second];
        const bool on_u = (a.u == 0.0 && b.u == 0.0) || (a.u == extent_u && b.u == extent_u);
        const bool on_v = (a.v == 0.0 && b.v == 0.0) || (a.v == extent_v && b.v == extent_v);
        require(on_u || on_v, "an unshared edge lies on the boundary (no crack)");
    }
}

void test_mesher() {
    const r3::Allocator system = r3::system_allocator();
    const cs::Surface plane = plane_surface(4U, 3U);
    r3::Mesh flat(system);
    r3::TessellationOptions options;
    require(r3::tessellate_surface(plane, options, system, flat) == r3::Status::ok, "the plane tessellates");
    require(flat.triangle_count() == 12U && flat.vertex_count() == 12U, "a flat cell is two triangles");

    const cs::Surface bump = bump_surface(6U, 5U);
    r3::Mesh curved(system);
    options.tolerance = 1e-3;
    require(r3::tessellate_surface(bump, options, system, curved) == r3::Status::ok, "the bump tessellates");
    require(curved.triangle_count() > 40U, "a curved surface is refined");
    check_crack_free(curved, 5.0, 4.0);
    // The surface at each triangle's parametric centroid lies near its
    // flat centroid.
    const std::span<const r3::MeshVertex> vertices = curved.vertices();
    const std::span<const std::uint32_t> indices = curved.indices();
    double worst = 0.0;
    for (std::size_t t = 0U; t < curved.triangle_count(); ++t) {
        const r3::MeshVertex& a = vertices[indices[t * 3U]];
        const r3::MeshVertex& b = vertices[indices[t * 3U + 1U]];
        const r3::MeshVertex& c = vertices[indices[t * 3U + 2U]];
        std::array<double, 4> values{};
        require(bump.evaluate((a.u + b.u + c.u) / 3.0, (a.v + b.v + c.v) / 3.0, values) == stx_conv::Status::ok,
                "the centroid evaluates");
        const r3::Vec3 flat_centre = (a.position + b.position + c.position) * (1.0 / 3.0);
        const r3::Vec3 surface_point{values[0], values[1], values[2]};
        worst = std::fmax(worst, r3::length(surface_point - flat_centre));
    }
    require(worst < 4e-3, "the mesh stays within a small multiple of the tolerance");

    // A hidden cell removes its triangles and nothing else's.
    std::vector<std::uint8_t> hidden(5U * 4U, 0U);
    hidden[1U * 5U + 2U] = 1U;
    options.hidden_cells = hidden;
    r3::Mesh holed(system);
    require(r3::tessellate_surface(bump, options, system, holed) == r3::Status::ok, "the holed bump tessellates");
    require(holed.triangle_count() < curved.triangle_count(), "a hidden cell is a hole");

    // Refusals append nothing.
    r3::TessellationOptions bad;
    bad.maximum_depth = 7U;
    const std::size_t before = holed.triangle_count();
    require(r3::tessellate_surface(bump, bad, system, holed) == r3::Status::invalid_argument, "depth 7 is refused");
    bad = r3::TessellationOptions{};
    bad.maximum_triangles = 4U;
    require(r3::tessellate_surface(bump, bad, system, holed) == r3::Status::too_large, "the budget is kept");
    Rationed ration;
    ration.remaining = 2U;
    require(r3::tessellate_surface(bump, r3::TessellationOptions{}, rationed(ration), holed) == r3::Status::out_of_memory,
            "a refusing scratch allocator is reported");
    require(holed.triangle_count() == before, "a refused tessellation appends nothing");
}

void test_grid() {
    const r3::Allocator system = r3::system_allocator();
    std::vector<double> positions;
    for (std::size_t v = 0U; v < 3U; ++v) {
        for (std::size_t u = 0U; u < 3U; ++u) {
            positions.push_back(static_cast<double>(u));
            positions.push_back(static_cast<double>(v));
            positions.push_back(static_cast<double>(u * v));
        }
    }
    r3::GridInput grid;
    grid.positions = positions;
    grid.nodes_u = 3U;
    grid.nodes_v = 3U;
    r3::Mesh smooth(system);
    require(r3::triangulate_grid(grid, smooth) == r3::Status::ok, "the smooth grid");
    require(smooth.vertex_count() == 9U && smooth.triangle_count() == 8U, "smooth shares the nodes");
    grid.shading = r3::GridShading::flat;
    r3::Mesh flat(system);
    require(r3::triangulate_grid(grid, flat) == r3::Status::ok, "the flat grid");
    require(flat.vertex_count() == 24U && flat.triangle_count() == 8U, "flat gives each triangle its vertices");
    const r3::MeshVertex& first = flat.vertices()[0];
    const r3::MeshVertex& second = flat.vertices()[1];
    require(first.normal.x == second.normal.x && first.value == second.value, "a flat triangle has one normal and value");
    positions[4U * 3U + 2U] = std::nan("");
    grid.positions = positions;
    r3::Mesh holed(system);
    require(r3::triangulate_grid(grid, holed) == r3::Status::ok, "the holed grid");
    require(holed.triangle_count() == 0U, "a non-finite centre opens all four cells");
    grid.nodes_u = 4U;
    require(r3::triangulate_grid(grid, holed) == r3::Status::invalid_argument, "a short span is refused");

    // A mark and truncate keep a retained part and drop what followed it.
    const r3::MeshMark kept = smooth.mark();
    append_square(smooth, -1.0, 1.0, 2.0, 9U);
    require(smooth.triangle_count() == 10U && smooth.vertex_count() == 13U, "per-view geometry is appended");
    require(smooth.truncate(kept) == r3::Status::ok, "truncate to the mark");
    require(smooth.triangle_count() == 8U && smooth.vertex_count() == 9U && smooth.indices().size() == 24U,
            "truncate keeps exactly the marked part");
    r3::MeshMark past = kept;
    past.triangles = 99U;
    require(smooth.truncate(past) == r3::Status::invalid_argument && smooth.triangle_count() == 8U,
            "a mark past the size is refused");
}

void test_lines_and_markers() {
    const r3::Allocator system = r3::system_allocator();
    const r3::View view = top_down(32U);
    r3::Mesh mesh(system);
    append_square(mesh, -12.0, 12.0, 0.0, 0U);
    const std::array<r3::Material, 1> materials{flat_material(0.0f, 0.0f, 1.0f)};
    // On the surface (row y = -5.5 world is pixel row 21) and under it.
    const std::array<r3::Vec3, 4> points{r3::Vec3{-10.0, -5.5, 0.0}, r3::Vec3{10.0, -5.5, 0.0}, r3::Vec3{-10.0, 5.5, -1.0},
                                         r3::Vec3{10.0, 5.5, -1.0}};
    std::array<r3::LineStrip, 2> lines{};
    lines[0].first = 0U;
    lines[0].count = 2U;
    lines[0].color = r3::Color{1.0f, 0.0f, 0.0f, 1.0f};
    lines[0].width = 2.0f;
    lines[0].item = 41U;
    lines[1].first = 2U;
    lines[1].count = 2U;
    lines[1].color = r3::Color{0.0f, 1.0f, 0.0f, 1.0f};
    lines[1].width = 2.0f;
    std::array<r3::Marker, 2> markers{};
    markers[0].position = r3::Vec3{-8.5, 8.5, 0.5};
    markers[0].color = r3::Color{1.0f, 1.0f, 0.0f, 1.0f};
    markers[0].size = 5.0f;
    markers[0].shape = r3::MarkerShape::square;
    markers[0].item = 7U;
    markers[1].position = r3::Vec3{8.5, 8.5, -1.0};
    markers[1].color = r3::Color{0.0f, 1.0f, 1.0f, 1.0f};
    markers[1].size = 5.0f;
    r3::Scene scene;
    scene.mesh = &mesh;
    scene.materials = materials;
    scene.line_points = points;
    scene.lines = lines;
    scene.markers = markers;
    r3::Renderer renderer(system);
    require(renderer.render(scene, view, 2U, r3::inline_executor()) == r3::Status::ok, "the overlay scene renders");
    const std::array<std::uint8_t, 4> on = pixel(renderer, 16U, 21U);
    require(on[2] == 255U && on[0] == 0U, "a line on the surface is seen");
    const std::array<std::uint8_t, 4> under = pixel(renderer, 16U, 10U);
    require(under[0] == 255U && under[1] == 0U, "a line under the surface is hidden");
    const std::array<std::uint8_t, 4> above = pixel(renderer, 7U, 7U);
    require(above[2] == 255U && above[1] == 255U && above[0] == 0U, "a marker above the surface is seen");
    const std::array<std::uint8_t, 4> below = pixel(renderer, 24U, 7U);
    require(below[0] == 255U && below[1] == 0U, "a marker under the surface is hidden");
    const r3::Pick line_pick = renderer.pick(16U, 21U, &mesh);
    require(line_pick.kind == r3::PickKind::line && line_pick.item == 41U && line_pick.index == 0U, "a line pick");
    const r3::Pick marker_pick = renderer.pick(7U, 7U, nullptr);
    require(marker_pick.kind == r3::PickKind::marker && marker_pick.item == 7U, "a marker pick");
    const r3::Pick no_mesh = renderer.pick(16U, 16U, nullptr);
    require(no_mesh.kind == r3::PickKind::none, "a surface pick needs the drawn mesh");
    require(renderer.statistics().segments_drawn == 2U && renderer.statistics().markers_drawn == 2U, "overlay statistics");
}

void test_determinism() {
    const r3::Allocator system = r3::system_allocator();
    const cs::Surface bump = bump_surface(6U, 5U);
    r3::Mesh mesh(system);
    r3::TessellationOptions options;
    options.value_channel = 2U;
    require(r3::tessellate_surface(bump, options, system, mesh) == r3::Status::ok, "the scene's mesh");
    const std::array<r3::Color, 4> colormap{r3::Color{0.1f, 0.0f, 0.4f, 1.0f}, r3::Color{0.2f, 0.4f, 0.6f, 1.0f},
                                            r3::Color{0.3f, 0.7f, 0.4f, 1.0f}, r3::Color{0.9f, 0.9f, 0.1f, 1.0f}};
    std::array<r3::Material, 1> materials{};
    materials[0].source = r3::ColorSource::vertex_value;
    materials[0].colormap = colormap;
    materials[0].value_minimum = -0.4;
    materials[0].value_maximum = 0.4;
    materials[0].lighting.mode = r3::LightingMode::headlight;
    materials[0].lighting.specular = 0.3f;
    materials[0].iso.u_step = 1.0;
    materials[0].iso.v_step = 1.0;
    const std::array<r3::Vec3, 3> points{r3::Vec3{0.0, 0.0, 0.5}, r3::Vec3{5.0, 4.0, 0.5}, r3::Vec3{5.0, 0.0, 0.0}};
    std::array<r3::LineStrip, 1> lines{};
    lines[0].count = 3U;
    lines[0].width = 1.5f;
    std::array<r3::Marker, 1> markers{};
    markers[0].position = r3::Vec3{2.5, 2.0, 0.6};
    markers[0].shape = r3::MarkerShape::circle;
    markers[0].size = 9.0f;
    r3::Scene scene;
    scene.mesh = &mesh;
    scene.materials = materials;
    scene.line_points = points;
    scene.lines = lines;
    scene.markers = markers;
    scene.background = r3::Color{1.0f, 1.0f, 1.0f, 1.0f};
    const r3::Camera camera = r3::orbit_camera(r3::Vec3{2.5, 2.0, 0.0}, 9.0, 30.0, -60.0, 0.0, r3::Projection::perspective);
    r3::View view;
    require(r3::make_view(camera, 120U, 90U, view) == r3::Status::ok, "the determinism view");
    r3::Renderer forward(system);
    r3::Renderer backward(system);
    require(forward.render(scene, view, 3U, r3::inline_executor()) == r3::Status::ok, "in order");
    require(backward.render(scene, view, 3U, reversed_executor()) == r3::Status::ok, "reversed");
    const std::span<const std::uint8_t> a = forward.picture();
    const std::span<const std::uint8_t> b = backward.picture();
    require(a.size() == b.size() && a.size() == 120U * 90U * 4U, "same size");
    bool same = true;
    bool drew = false;
    for (std::size_t k = 0U; k < a.size(); ++k) {
        if (a[k] != b[k]) same = false;
        if (a[k] != 255U) drew = true;
    }
    require(same, "the picture is the same bytes under any executor order");
    require(drew, "the determinism scene draws something");
    const r3::RenderStatistics& statistics = forward.statistics();
    require(statistics.triangles_drawn + statistics.triangles_clipped + statistics.triangles_skipped == mesh.triangle_count(),
            "every triangle is counted");
    require(statistics.triangles_drawn > 0U, "triangles are drawn");
}

void test_picking() {
    const r3::Allocator system = r3::system_allocator();
    const cs::Surface bump = bump_surface(6U, 5U);
    r3::Mesh mesh(system);
    r3::TessellationOptions options;
    options.tolerance = 2e-2;
    require(r3::tessellate_surface(bump, options, system, mesh) == r3::Status::ok, "the pick mesh");
    const r3::Camera camera = r3::orbit_camera(r3::Vec3{2.5, 2.0, 0.0}, 8.0, 50.0, 20.0, 0.0, r3::Projection::perspective);
    r3::View view;
    require(r3::make_view(camera, 64U, 64U, view) == r3::Status::ok, "the pick view");
    r3::Scene scene;
    scene.mesh = &mesh;
    r3::Renderer renderer(system);
    require(renderer.render(scene, view, 1U, r3::inline_executor()) == r3::Status::ok, "the pick scene renders");
    const r3::Pick pick = renderer.pick(32U, 32U, &mesh);
    require(pick.kind == r3::PickKind::surface, "the centre is on the surface");
    double u = pick.u;
    double v = pick.v;
    r3::Vec3 point{};
    require(r3::refine_surface_pick(bump, view, 32.5, 32.5, u, v, point) == r3::Status::ok, "the pick refines");
    std::array<double, 4> values{};
    require(bump.evaluate(u, v, values) == stx_conv::Status::ok, "the refined parameters evaluate");
    require(near_value(values[0], point.x, 1e-9) && near_value(values[2], point.z, 1e-9), "the hit is on the surface");
    r3::Vec3 origin{};
    r3::Vec3 direction{};
    view.ray(32.5, 32.5, origin, direction);
    const r3::Vec3 offset = point - origin;
    const r3::Vec3 unit = r3::normalized(direction);
    const r3::Vec3 across = offset - unit * r3::dot(offset, unit);
    require(r3::length(across) < 1e-8, "the hit is on the ray");
    require(near_value(u, pick.u, 0.2) && near_value(v, pick.v, 0.2), "the mesh pick seeds near the hit");
    double unchanged_u = std::nan("");
    double unchanged_v = 0.0;
    require(r3::refine_surface_pick(bump, view, 32.5, 32.5, unchanged_u, unchanged_v, point) == r3::Status::nonfinite_input,
            "a non-finite seed is refused");
}

void test_iso_lines() {
    const r3::Allocator system = r3::system_allocator();
    const r3::View view = top_down(16U);
    // Node u lies at world x = 4u - 6.5 (pixel centre 4u + 1.5).
    std::vector<double> positions;
    for (std::size_t v = 0U; v < 4U; ++v) {
        for (std::size_t u = 0U; u < 4U; ++u) {
            positions.push_back(4.0 * static_cast<double>(u) - 6.5);
            positions.push_back(4.0 * static_cast<double>(v) - 6.5);
            positions.push_back(0.0);
        }
    }
    r3::GridInput grid;
    grid.positions = positions;
    grid.nodes_u = 4U;
    grid.nodes_v = 4U;
    r3::Mesh mesh(system);
    require(r3::triangulate_grid(grid, mesh) == r3::Status::ok, "the iso grid");
    std::array<r3::Material, 1> materials{flat_material(1.0f, 1.0f, 1.0f)};
    materials[0].iso.u_step = 1.0;
    materials[0].iso.color = r3::Color{0.0f, 0.0f, 0.0f, 1.0f};
    r3::Scene scene;
    scene.mesh = &mesh;
    scene.materials = materials;
    r3::Renderer renderer(system);
    require(renderer.render(scene, view, 1U, r3::inline_executor()) == r3::Status::ok, "the iso scene renders");
    // u = 1 at pixel column 5's centre; u = 1.5 at column 7's.
    const std::array<std::uint8_t, 4> on = pixel(renderer, 5U, 8U);
    const std::array<std::uint8_t, 4> off = pixel(renderer, 7U, 8U);
    require(on[0] == 0U && on[3] == 255U, "a pixel on the iso line takes its colour");
    require(off[0] == 255U, "a pixel between iso lines keeps the surface colour");
}

void test_refusals() {
    Rationed ration;
    ration.remaining = 64U;
    const r3::Allocator allocator = rationed(ration);
    const r3::View view = top_down(16U);
    r3::Mesh mesh(r3::system_allocator());
    append_square(mesh, -4.0, 4.0, 0.0, 0U);
    r3::Scene scene;
    scene.mesh = &mesh;
    r3::Renderer renderer(allocator);
    require(renderer.render(scene, view, 1U, r3::inline_executor()) == r3::Status::ok, "the first frame");
    const std::vector<std::uint8_t> first(renderer.picture().begin(), renderer.picture().end());
    require(renderer.render(scene, view, 5U, r3::inline_executor()) == r3::Status::invalid_argument, "supersample 5");
    r3::Executor none;
    require(renderer.render(scene, view, 1U, none) == r3::Status::invalid_argument, "an executor without a target");
    const std::array<r3::Vec3, 1> points{r3::Vec3{}};
    std::array<r3::LineStrip, 1> lines{};
    lines[0].first = 0U;
    lines[0].count = 2U;
    scene.line_points = points;
    scene.lines = lines;
    require(renderer.render(scene, view, 1U, r3::inline_executor()) == r3::Status::invalid_argument, "a strip past its points");
    scene.lines = {};
    scene.line_points = {};
    // A larger frame needs more storage than the allocator still grants.
    ration.remaining = 0U;
    const r3::View larger = top_down(64U);
    require(renderer.render(scene, larger, 4U, r3::inline_executor()) == r3::Status::out_of_memory, "refused storage");
    require(renderer.width() == 16U, "a refused frame keeps the previous size");
    const std::span<const std::uint8_t> kept = renderer.picture();
    bool same = kept.size() == first.size();
    for (std::size_t k = 0U; same && k < kept.size(); ++k) same = kept[k] == first[k];
    require(same, "a refused frame keeps the previous picture");
    const r3::Pick pick = renderer.pick(8U, 8U, &mesh);
    require(pick.kind == r3::PickKind::surface, "a refused frame keeps the previous picking");
}

// Clipping: a floor reaching behind the eye is drawn as its visible part,
// a line from behind the eye as its part in front, a marker behind the eye
// not at all, and a triangle reaching far off screen as its part on it.
void test_clipping() {
    const r3::Allocator system = r3::system_allocator();
    r3::Camera camera;
    camera.eye = r3::Vec3{0.0, -1.0, 0.5};
    camera.target = r3::Vec3{0.0, 10.0, 0.5};
    camera.up = r3::Vec3{0.0, 0.0, 1.0};
    r3::View view;
    require(r3::make_view(camera, 32U, 32U, view) == r3::Status::ok, "the clipping view");
    r3::Mesh floor(system);
    append_square(floor, -50.0, 50.0, 0.0, 0U);
    const std::array<r3::Material, 1> materials{flat_material(0.0f, 1.0f, 0.0f)};
    r3::Scene scene;
    scene.mesh = &floor;
    scene.materials = materials;
    r3::Renderer renderer(system);
    require(renderer.render(scene, view, 2U, r3::inline_executor()) == r3::Status::ok, "the floor renders");
    require(renderer.statistics().triangles_clipped > 0U, "the floor is clipped");
    const std::array<std::uint8_t, 4> below = pixel(renderer, 16U, 31U);
    require(below[1] == 255U && below[3] == 255U, "the floor in front of the eye is drawn");
    const std::array<std::uint8_t, 4> sky = pixel(renderer, 16U, 2U);
    require(sky[3] == 0U, "nothing is drawn above the horizon");
    const r3::Pick ground = renderer.pick(16U, 31U, &floor);
    require(ground.kind == r3::PickKind::surface, "the clipped floor is picked");
    require(near_value(ground.position.z, 0.0, 1e-6) && ground.position.y > -1.0 && ground.depth > 0.0,
            "a clipped pick lies on the floor in front of the eye");
    require(renderer.pick(16U, 2U, &floor).kind == r3::PickKind::none, "nothing is picked above the horizon");

    // A line from behind the eye to in front of it, and a marker behind.
    const std::array<r3::Vec3, 2> points{r3::Vec3{0.0, -5.0, 0.4}, r3::Vec3{0.0, 3.0, 0.4}};
    std::array<r3::LineStrip, 1> lines{};
    lines[0].count = 2U;
    lines[0].width = 2.0f;
    lines[0].color = r3::Color{1.0f, 0.0f, 0.0f, 1.0f};
    lines[0].item = 5U;
    std::array<r3::Marker, 1> markers{};
    markers[0].position = r3::Vec3{0.0, -3.0, 0.5};
    markers[0].size = 40.0f;
    r3::Scene overlay;
    overlay.line_points = points;
    overlay.lines = lines;
    overlay.markers = markers;
    require(renderer.render(overlay, view, 2U, r3::inline_executor()) == r3::Status::ok, "the overlay renders");
    require(renderer.statistics().segments_drawn == 1U, "the segment's front part is drawn");
    require(renderer.statistics().markers_drawn == 0U, "a marker behind the eye is left out");
    // The far end (depth 4) is at row 16.4; the line runs down from there.
    const std::array<std::uint8_t, 4> near_part = pixel(renderer, 16U, 30U);
    const std::array<std::uint8_t, 4> middle = pixel(renderer, 16U, 18U);
    require(near_part[2] == 255U && middle[2] == 255U, "the line in front of the eye is drawn");
    require(pixel(renderer, 16U, 10U)[3] == 0U, "nothing of the line is drawn beyond its far end");
    const r3::Pick line_pick = renderer.pick(16U, 30U, nullptr);
    require(line_pick.kind == r3::PickKind::line && line_pick.item == 5U, "the clipped line is picked");

    // Corners far beyond the old guard band (2^22 samples).
    const r3::View flat = top_down(16U);
    r3::Mesh huge(system);
    append_square(huge, -1e7, 1e7, 0.0, 0U);
    r3::Scene wide;
    wide.mesh = &huge;
    wide.materials = materials;
    require(renderer.render(wide, flat, 1U, r3::inline_executor()) == r3::Status::ok, "the huge square renders");
    require(pixel(renderer, 8U, 8U)[1] == 255U && pixel(renderer, 0U, 15U)[1] == 255U, "a huge square fills the picture");
    require(renderer.statistics().triangles_clipped == 2U, "both huge triangles are clipped");
}

} // namespace

int main() {
    test_buffer();
    test_camera();
    test_shared_edge();
    test_depth_order();
    test_coverage();
    test_mesher();
    test_grid();
    test_lines_and_markers();
    test_determinism();
    test_picking();
    test_iso_lines();
    test_refusals();
    test_clipping();
    std::puts("stx_raster3d: all tests passed");
    return 0;
}
