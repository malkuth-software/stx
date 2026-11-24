#pragma once
// The visibility-buffer renderer.
//
// A frame draws a mesh (shaded per material), polylines and markers into a
// supersampled visibility buffer: per sample, the nearest primitive and its
// screen-affine weights, with integer edge functions (8 bits below the
// sample) and a top-left fill rule, so triangles sharing an edge neither
// overlap nor leave a gap. Lines and markers are screen-space polygons with
// a depth along them, drawn into the same buffer a little nearer than a
// surface at the same depth, so they are hidden by what is in front of them
// and win against the surface they lie on. Shading then runs once per
// primitive and pixel, from the samples it covers, and the pixel is the
// coverage-weighted mean (premultiplied).
//
// Clipping: triangles and segments are clipped in view space to the near
// plane and to the picture's sides (a pixel beyond them, more for wide
// lines), so a triangle crossing the near plane or reaching far off screen
// is drawn as its visible part; a marker whose centre is behind the near
// plane or off the picture by more than its size is left out.
//
// Determinism: binary64 geometry and binary32 sample arithmetic in a fixed
// order, no contraction into fused multiply-add, triangles in index order
// within each band of rows, and every band written by one task. The picture
// is the same bytes whatever the executor does.

#include <stx_raster3d/camera.hpp>
#include <stx_raster3d/executor.hpp>
#include <stx_raster3d/geometry.hpp>
#include <stx_raster3d/memory.hpp>
#include <stx_raster3d/mesh.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace stx_raster3d {

enum class ColorSource : std::uint8_t {
    uniform,      // the material's colour
    vertex_color, // the vertices' colours, interpolated
    vertex_value  // the vertices' values, interpolated, through the colour map
};

enum class LightingMode : std::uint8_t {
    none,     // the colour as it is
    fixed,    // a light from a fixed world direction
    headlight // a light from the eye
};

// Lambert lighting with an optional Blinn highlight, two-sided (a surface
// seen from behind is lit as from in front): ambient + diffuse * max(0,
// n.l), applied to the colour, plus specular * max(0, n.h)^shininess.
struct Lighting final {
    LightingMode mode = LightingMode::none;
    // Towards the light, world units (fixed mode).
    Vec3 direction{-0.5, -0.5, 1.0};
    float ambient = 0.35f;
    float diffuse = 0.65f;
    float specular = 0.0f;
    float shininess = 32.0f;
};

// Lines drawn on a surface from its own parameters and values: every
// u_step and v_step (from the origins; 0 for none) and at every value
// level (ascending). Anti-aliased, `width` pixels wide.
struct IsoLines final {
    double u_step = 0.0;
    double v_step = 0.0;
    double u_origin = 0.0;
    double v_origin = 0.0;
    std::span<const double> value_levels{};
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
    float width = 1.0f;
};

// How one part is shaded. The colour map is a table of colours from the
// value minimum to the maximum; a value is clamped into that range and
// takes the nearest entry below it (as a matplotlib colormap of N entries).
struct Material final {
    ColorSource source = ColorSource::uniform;
    Color color{0.12f, 0.47f, 0.71f, 1.0f};
    std::span<const Color> colormap{};
    double value_minimum = 0.0;
    double value_maximum = 1.0;
    Lighting lighting{};
    IsoLines iso{};
};

// A polyline: line_points[first .. first + count), `width` pixels wide,
// with square ends. `item` is the caller's identity, reported by picking.
struct LineStrip final {
    std::uint32_t first = 0U;
    std::uint32_t count = 0U;
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
    float width = 1.0f;
    std::uint32_t item = 0U;
};

enum class MarkerShape : std::uint8_t { circle, square, diamond, triangle_up, plus, cross };

// A marker `size` pixels across, facing the screen, at a world position.
struct Marker final {
    Vec3 position{};
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
    float size = 6.0f;
    MarkerShape shape = MarkerShape::circle;
    std::uint32_t item = 0U;
};

// What one frame draws. Everything is borrowed for the render call only.
// A triangle's part indexes `materials`; a part past the end takes the
// default material.
struct Scene final {
    const Mesh* mesh = nullptr;
    std::span<const Material> materials{};
    std::span<const Vec3> line_points{};
    std::span<const LineStrip> lines{};
    std::span<const Marker> markers{};
    Color background{0.0f, 0.0f, 0.0f, 0.0f};
};

enum class PickKind : std::uint8_t { none, surface, line, marker };

// What lies under a pixel's centre. For a surface: the part, the triangle,
// the parameters, value, position and depth there (interpolated on the
// mesh; refine_surface_pick puts them on the surface itself). For a line:
// its item, the strip and segment. For a marker: its item and index.
struct Pick final {
    PickKind kind = PickKind::none;
    std::uint32_t item = 0U;
    std::uint32_t index = 0U;
    std::uint32_t segment = 0U;
    double u = 0.0;
    double v = 0.0;
    double value = 0.0;
    Vec3 position{};
    double depth = 0.0;
};

struct RenderStatistics final {
    std::size_t triangles_drawn = 0U;
    std::size_t triangles_skipped = 0U; // wholly outside the view frustum, or degenerate
    std::size_t triangles_clipped = 0U; // drawn as their part inside the frustum
    std::size_t segments_drawn = 0U;
    std::size_t markers_drawn = 0U;
    std::size_t sample_count = 0U;
};

// Owns the frame's working storage and its result: the picture (premultiplied
// BGRA8, rows top to bottom, width * 4 bytes each) and the visibility buffer
// picking reads. The storage grows to the largest frame and is reused.
// One renderer is used from one thread at a time (the executor's tasks run
// inside render).
class Renderer final {
public:
    // Samples per pixel along each direction, 1 to 4.
    static constexpr std::size_t maximum_supersample = 4U;
    // The largest picture side, in pixels.
    static constexpr std::size_t maximum_side = 16384U;

    explicit Renderer(const Allocator& allocator) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) noexcept = default;
    Renderer& operator=(Renderer&&) noexcept = default;

    // Draws a frame of view.width() by view.height() pixels. Validation
    // (sizes, spans, finite values) runs first and storage is grown next:
    // invalid_argument, nonfinite_input, too_large or out_of_memory leave
    // the previous frame (picture and picking) as it was. An executor without
    // a run target is invalid_argument.
    [[nodiscard]] Status render(const Scene& scene, const View& view, std::size_t supersample,
                                const Executor& executor) noexcept;

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t height() const noexcept { return height_; }
    [[nodiscard]] std::span<const std::uint8_t> picture() const& noexcept { return picture_.view(); }
    std::span<const std::uint8_t> picture() const&& = delete;
    // What lies under the centre of pixel (x, y) in the last frame; none
    // outside the picture or before the first frame. A surface pick reads the
    // mesh the frame drew, which the caller passes back unchanged: for any
    // other mesh (another address or triangle count) or none, a surface under
    // the pixel is reported as none. Lines and markers need no mesh.
    [[nodiscard]] Pick pick(std::size_t x, std::size_t y, const Mesh* mesh) const noexcept;
    [[nodiscard]] const RenderStatistics& statistics() const noexcept { return statistics_; }
    [[nodiscard]] std::size_t storage_bytes() const noexcept;

    // One sample of the visibility buffer: the depth key (larger is nearer;
    // 0 is nothing), the primitive and its screen-affine weights of the
    // second and third corners. Public for the frame's tasks only.
    struct Sample final {
        float key = 0.0f;
        std::uint32_t primitive = 0xffffffffU;
        float l1 = 0.0f;
        float l2 = 0.0f;
    };
    // A primitive in sample space: three corners in 24.8 fixed point, their
    // depth keys, and what it came from (a mesh triangle, a line segment or
    // a marker piece). For a mesh triangle, `weights` holds each corner's
    // barycentric weights of the source triangle's second and third
    // vertices (pairs, corner by corner; the first vertex takes the rest),
    // which are not the identity once it is clipped or turned over.
    struct ScreenTriangle final {
        std::array<std::int32_t, 3> x{};
        std::array<std::int32_t, 3> y{};
        std::array<float, 3> key{};
        std::array<float, 6> weights{};
        std::uint32_t source = 0U;
        std::uint32_t detail = 0U;
        std::uint8_t kind = 0U;
    };

private:
    friend struct FrameWork;
    Allocator allocator_{};
    // Read by picking: the visibility buffer, the frame's primitives and the
    // items of its strips and markers. A failed render leaves them alone; the
    // next frame's primitives are built in staging_ and swapped in only once
    // every growth has succeeded.
    Buffer<Sample> samples_;
    Buffer<ScreenTriangle> primitives_;
    Buffer<std::uint32_t> pick_line_items_;
    Buffer<std::uint32_t> pick_marker_items_;
    Buffer<std::uint8_t> picture_;
    // Working storage of one frame: mesh vertices in view coordinates
    // (along the view's right and up directions, and depth), the next primitives, and the primitives of each band of
    // rows as offsets (band_starts_, one past the bands) into band_entries_.
    Buffer<double> vertex_right_;
    Buffer<double> vertex_up_;
    Buffer<double> vertex_depth_;
    Buffer<ScreenTriangle> staging_;
    Buffer<std::size_t> band_starts_;
    Buffer<std::size_t> band_cursor_;
    Buffer<std::uint32_t> band_entries_;
    // The last frame: its mesh (observed for picking only, never read through
    // unless the caller passes it back), its size and its depth mapping.
    const Mesh* drawn_mesh_ = nullptr;
    std::size_t mesh_triangles_ = 0U;
    std::size_t width_ = 0U;
    std::size_t height_ = 0U;
    std::size_t supersample_ = 1U;
    Projection projection_ = Projection::perspective;
    Vec3 eye_{};
    Vec3 forward_{0.0, 1.0, 0.0};
    // Orthographic depth keys map view depth [near_depth_, far_depth_].
    double near_depth_ = 0.0;
    double far_depth_ = 1.0;
    RenderStatistics statistics_{};
};

} // namespace stx_raster3d
