#pragma once
// CONV surfaces: a tensor CONV interpolant through a lattice of nodes, with
// exact first derivatives, seams and a continuous bubble limiter.
//
// A surface is addressed in node units: u in [0, cells_u], v in [0, cells_v],
// with integer coordinates at the nodes. It passes through every node, and
// its value at (u, v) depends on the 6 by 6 block of nodes around the cell
// (8 by 8 next to a seam) and on nothing else. Each channel (a coordinate,
// a colour component, a scalar field) is interpolated on its own: u-lines
// first, then one v-line through the u-line values at the query.
//
// Seams cut every difference stencil along a whole node row or column; the
// values stay continuous across a seam. With the cubic closure a segment
// between seams reproduces a cubic exactly. The bubble adds the six-sample
// quintic's detail within a bound: `standard` is the stream kernels' one-
// sided bound (discontinuous in the data), `continuous` a symmetric bound
// that is continuous in the data, so a surface through moving nodes never
// cracks.
//
// Provenance: restated from the conv-teapot study (malkuth-software/
// conv-teapot, MIT; PROVENANCE.md). The line cell agrees with the stream
// model's base and standard-bubble lines (tests/surface/line_reference.txt).

#include <stx_conv/conv.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace stx_conv::surface {

enum class Bubble : std::uint8_t {
    none,
    standard,  // the stream kernels' one-sided sign bound
    continuous // the symmetric bound, continuous in the data
};

enum class Closure : std::uint8_t {
    paper, // second-order one-sided ends, as the stream kernels and the paper
    cubic  // cubic-exact ends: a cubic segment is reproduced exactly
};

// Which difference stencil gives a node its scaled jet (m, q). Decided by the
// node's place in its segment, never by data.
enum class Jet : std::uint8_t {
    interior,      // fourth-order centred, five samples (exact for quartics)
    paper_first,   // paper closure, first node of a segment
    paper_central, // paper closure, second and second-to-last nodes
    paper_last,    // paper closure, last node
    first4,        // cubic-exact, four samples to the right
    second4,       // cubic-exact, one to the left and two to the right
    penultimate4,  // cubic-exact, two to the left and one to the right
    last4,         // cubic-exact, four samples to the left
    first3,        // three-node segment: the quadratic through it
    central3,
    last3,
    first2,        // two-node segment: the secant
    last2
};

// A line of nodes with optional seams. The two ends of an open line are
// seams. `seams` is empty or holds one entry per node, nonzero at a seam; it
// is borrowed for the call it is passed to.
struct LineLayout final {
    std::size_t nodes = 0;
    bool periodic = false;
    Closure closure = Closure::cubic;
    std::span<const std::uint8_t> seams{};
};

// The structure of one cell. A node on a seam has two jets, one from each
// side: `left` and `right` are the jets of this cell's own segment,
// `previous` and `next` those with which the neighbouring cells see the
// shared nodes.
struct CellShape final {
    Jet left = Jet::interior;
    Jet right = Jet::interior;
    Jet previous = Jet::interior;
    Jet next = Jet::interior;
    bool has_previous = true;
    bool has_next = true;
    // The six-sample quintic term of the bubble applies.
    bool six = true;
};

// The cells of a layout: nodes for a periodic line, nodes - 1 otherwise; 0
// for a layout with fewer than two nodes (three when periodic).
[[nodiscard]] std::size_t cell_count(const LineLayout& layout) noexcept;
// The shape of cell `cell` (< cell_count(layout)); a layout without cells, a
// cell past the end or a seam table of the wrong length gives the shape of
// a two-node segment.
[[nodiscard]] CellShape cell_shape(const LineLayout& layout, std::size_t cell) noexcept;

// One admitted line cell: y[i], five currents summing to y[i+1] - y[i], and
// the bubble amplitude.
struct LineCell final {
    double anchor = 0.0;
    std::array<double, 5> currents{};
    double bubble = 0.0;
};

// Compiles one cell from its window y[i-3..i+4] (the cell lies between
// window[3] and window[4]); entries outside an open line are not read.
// Allocation-free. The window must be finite.
[[nodiscard]] LineCell compile_line_cell(std::span<const double, 8> window, const CellShape& shape,
                                         Bubble bubble) noexcept;
// The value of a compiled cell at t in [0, 1].
[[nodiscard]] double evaluate_line_cell(const LineCell& cell, double t) noexcept;

// A surface point: per channel the value and its two derivatives per node
// unit. Only the first `channels` entries are written.
struct SurfaceSample final {
    std::array<double, 4> value{};
    std::array<double, 4> along_u{};
    std::array<double, 4> along_v{};
};

// The fixed shape of a surface: node counts, channels (1 to 4), whether it
// closes around u, the end closure and the bubble.
struct SurfaceShape final {
    std::size_t nodes_u = 0;
    std::size_t nodes_v = 0;
    std::size_t channels = 1;
    bool periodic_u = false;
    Closure closure = Closure::cubic;
    Bubble bubble = Bubble::continuous;
};

// Owns the admitted u-cells of every row and the shape of every cell. The
// constructor allocates everything: an invalid shape (fewer than two nodes
// along v or along an open u, fewer than three around a periodic u, channels
// outside 1..4) throws invalid_argument, an oversized one length_error or
// bad_alloc, and nothing is left behind. Move-only; moving leaves the source
// empty, which every operation rejects.
//
// States: unprepared (constructed, or after a failed compile) and ready.
// compile and evaluation are allocation-free; evaluation is const and may
// run on several threads at once on a ready surface.
class Surface final {
public:
    static constexpr std::size_t maximum_channels = 4U;
    // The largest finite node magnitude accepted (as the line profiles).
    static constexpr double maximum_magnitude = 1e100;

    explicit Surface(const SurfaceShape& requested);
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    Surface(Surface&& other) noexcept;
    Surface& operator=(Surface&& other) noexcept;

    // Compiles the surface through `nodes`, [v][u][channel] contiguous
    // (nodes_u * nodes_v * channels values). `seams_u` and `seams_v` are
    // empty or hold one entry per node along that direction, nonzero at a
    // seam. Validation runs before anything is written: a wrong length is
    // invalid_shape, a non-finite node nonfinite_input, a magnitude over
    // maximum_magnitude input_out_of_range, and each leaves the surface as
    // it was (ready or not). Nothing borrowed is kept after the call.
    [[nodiscard]] Status compile(std::span<const double> nodes, std::span<const std::uint8_t> seams_u,
                                 std::span<const std::uint8_t> seams_v) noexcept;

    // The value of every channel at (u, v) into values[0 .. channels). u is
    // wrapped when periodic and clamped otherwise; v is clamped. A non-
    // finite coordinate is input_out_of_range, a short output
    // insufficient_storage, an unprepared surface unprepared_profile; on
    // failure the output is unchanged.
    [[nodiscard]] Status evaluate(double u, double v, std::span<double> values) const noexcept;
    // The value and both derivatives (per node unit). Failures as evaluate.
    [[nodiscard]] Status evaluate_with_derivatives(double u, double v, SurfaceSample& sample) const noexcept;

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const SurfaceShape& shape() const noexcept { return shape_; }
    [[nodiscard]] std::size_t cells_u() const noexcept;
    [[nodiscard]] std::size_t cells_v() const noexcept;
    [[nodiscard]] std::size_t storage_bytes() const noexcept;

private:
    // The cell and local parameter of a coordinate along u or v.
    struct Location final {
        std::size_t cell = 0;
        double t = 0.0;
    };
    [[nodiscard]] Location locate_u(double u) const noexcept;
    [[nodiscard]] Location locate_v(double v) const noexcept;
    void release() noexcept;

    SurfaceShape shape_{};
    bool ready_ = false;
    // [(row * cells_u + cell) * channels + channel].
    std::vector<LineCell> rows_;
    std::vector<CellShape> shapes_u_;
    std::vector<CellShape> shapes_v_;
    // Whether the v-cell reads the row three below / four above its first
    // node (only the four-sample end stencils do).
    std::vector<std::uint8_t> reads_low_;
    std::vector<std::uint8_t> reads_high_;
};

} // namespace stx_conv::surface
