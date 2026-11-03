#pragma once
// Warp of a tensor-quintic RGBA atlas through an inverse homography, one
// target row at a time. Target pixel (column c, row r) takes the atlas sample
// at inverse(c, r): pixel for pixel the result of map_points followed by
// sample_rgba at (c, r), except that a zero result's sign may differ.
//
// It is a reformulation of that point path, not a vectorized copy:
// - Lanes are target pixels for the mapping, the clamp and both quintic
//   bases; for the 36-control contraction the RGBA channels are the lanes.
// - patch_rgba's grid-line and node reductions are gone. With finite
//   controls the full contraction reproduces them: on a line or node the
//   weights are exactly 0 and 1, so only the sign of a zero sum can differ.
// - The footprint test is a lane mask (keep_or_zero) only where a projective
//   row's rounding bound leaves the side uncertain (projective_row); columns
//   certainly outside are +0 and those certainly inside run unmasked.
//   For affine maps it is an exact column interval found per row before the
//   loop (affine_footprint), and the loop has no test at all.
#include "conv_mapping_kernel.hpp"
#include "conv_tensor_kernel.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace stx_conv::kernels::detail {
inline namespace STX_CONV_ISA {

// One warp's fixed inputs, validated by stx_conv::kernels::warp_rgba.
template<class Scalar>
struct WarpGeometry {
    const Scalar* controls = nullptr;
    // Scalar elements between lattice rows, RGBA included.
    std::size_t lattice_row_stride = 0;
    // At least 2 each.
    std::size_t source_width = 0;
    std::size_t source_height = 0;
    std::array<Scalar, 9> inverse{};
};

// Target columns [begin, end) of one row.
struct ColumnInterval {
    std::size_t begin = 0;
    std::size_t end = 0;
};

// Premultiplied RGBA sums clamped in place: alpha into [0, 1], then every
// channel into [0, alpha]; the alpha lane's own result is alpha.
template<class Scalar>
STX_CONV_INLINE void clamp_premultiplied(std::array<typename NativeOps<Scalar>::Value, rgba_groups<Scalar>>& sums) noexcept {
    using ChannelOps = NativeOps<Scalar>;
    using Channels = typename ChannelOps::Value;
    constexpr std::size_t groups = rgba_groups<Scalar>;
    std::array<Scalar, 4> sample{};
    for (std::size_t group = 0; group < groups; ++group) {
        ChannelOps::store(sample.data() + group * ChannelOps::width, sums[group]);
    }
    const Channels alpha = ChannelOps::set(std::clamp(sample[3], Scalar(0), Scalar(1)));
    for (std::size_t group = 0; group < groups; ++group) {
        sums[group] = ChannelOps::min(alpha, ChannelOps::max(sums[group], ChannelOps::set(0)));
    }
}

// Pixel lane Lane of a warp group: its contraction from the group's bases,
// the premultiplied clamp, the footprint mask; pixel has four Scalars.
template<class Ops, class Scalar, bool Masked, bool Premultiplied, std::size_t Lane>
STX_CONV_INLINE void warp_pixel(const Scalar* origin, std::size_t row_stride, bool inside,
                                const std::array<typename Ops::Value, 6>& basis_x,
                                const std::array<typename Ops::Value, 6>& basis_y, Scalar* pixel) noexcept {
    using ChannelOps = NativeOps<Scalar>;
    using Channels = typename ChannelOps::Value;
    constexpr std::size_t groups = rgba_groups<Scalar>;
    std::array<Channels, groups> sums = lane_patch_rgba<Ops, Lane, Scalar>(origin, row_stride, basis_x, basis_y);
    if constexpr (Premultiplied) { clamp_premultiplied<Scalar>(sums); }
    for (std::size_t group = 0; group < groups; ++group) {
        Channels value = sums[group];
        if constexpr (Masked) { value = ChannelOps::keep_or_zero(inside, value); }
        ChannelOps::store(pixel + group * ChannelOps::width, value);
    }
}

// Every pixel lane of a warp group in lane order; first_pixel is lane 0's.
template<class Ops, class Scalar, bool Masked, bool Premultiplied, std::size_t... Lanes>
STX_CONV_INLINE void warp_pixels(const std::array<const Scalar*, Ops::width>& origin, std::size_t row_stride,
                                 const std::array<bool, Ops::width>& inside,
                                 const std::array<typename Ops::Value, 6>& basis_x,
                                 const std::array<typename Ops::Value, 6>& basis_y, Scalar* first_pixel,
                                 std::index_sequence<Lanes...>) noexcept {
    (warp_pixel<Ops, Scalar, Masked, Premultiplied, Lanes>(origin[Lanes], row_stride, inside[Lanes], basis_x, basis_y,
                                                           first_pixel + 4 * Lanes),
     ...);
}

// One group of target pixels located in the atlas: each lane's mapped source
// point, its cell's first control and its fractions (u, v) in that cell.
template<class Scalar, std::size_t width>
struct GroupCells {
    std::array<Scalar, width> mapped_x{};
    std::array<Scalar, width> mapped_y{};
    std::array<Scalar, width> u{};
    std::array<Scalar, width> v{};
    std::array<const Scalar*, width> origin{};
};

// Pixels [column, column + width) of `row`: mapped, clamped into the node
// domain [0, source_width - 1] x [0, source_height - 1], then each lane's cell (the last
// cell also takes the last node). Any column, even past the row's end, gives
// cells inside the atlas.
template<class Ops, class Scalar, bool Affine>
STX_CONV_INLINE GroupCells<Scalar, Ops::width> locate_group(const WarpGeometry<Scalar>& geometry, std::size_t row,
                                                             std::size_t column) noexcept {
    using Vector = typename Ops::Value;
    constexpr std::size_t width = Ops::width;
    GroupCells<Scalar, width> cells{};
    std::array<Scalar, width> target_x{};
    std::array<Scalar, width> target_y{};
    for (std::size_t lane = 0; lane < width; ++lane) {
        target_x[lane] = static_cast<Scalar>(column + lane);
        target_y[lane] = static_cast<Scalar>(row);
    }
    mapping_group<Ops, Scalar, Affine>(geometry.inverse, target_x.data(), target_y.data(), cells.mapped_x.data(),
                                       cells.mapped_y.data(), 0);
    const Vector last_x = Ops::set(static_cast<Scalar>(geometry.source_width - 1));
    const Vector last_y = Ops::set(static_cast<Scalar>(geometry.source_height - 1));
    const Vector clamped_x = Ops::min(Ops::max(Ops::load(cells.mapped_x.data()), Ops::set(0)), last_x);
    const Vector clamped_y = Ops::min(Ops::max(Ops::load(cells.mapped_y.data()), Ops::set(0)), last_y);
    std::array<Scalar, width> node_x{};
    std::array<Scalar, width> node_y{};
    Ops::store(node_x.data(), clamped_x);
    Ops::store(node_y.data(), clamped_y);
    for (std::size_t lane = 0; lane < width; ++lane) {
        const std::size_t cell_x = std::min(static_cast<std::size_t>(node_x[lane]), geometry.source_width - 2);
        const std::size_t cell_y = std::min(static_cast<std::size_t>(node_y[lane]), geometry.source_height - 2);
        cells.u[lane] = node_x[lane] - static_cast<Scalar>(cell_x);
        cells.v[lane] = node_y[lane] - static_cast<Scalar>(cell_y);
        cells.origin[lane] = geometry.controls + cell_y * 5 * geometry.lattice_row_stride + cell_x * 5 * 4;
    }
    return cells;
}

// How far ahead of a streaming warp group, in target pixels, its atlas cells
// are prefetched: measured on the M4 (16 against 8, 32 and 64; and against
// prefetching three rows, or only the group's end lanes). The override is
// for repeating that measurement.
#ifndef STX_CONV_WARP_PREFETCH_DISTANCE
#define STX_CONV_WARP_PREFETCH_DISTANCE 16
#endif
inline constexpr std::size_t warp_prefetch_distance = STX_CONV_WARP_PREFETCH_DISTANCE;

// Prefetches the cells of pixels [column, column + width) of `row`: in each
// of a cell's six control rows, the lines holding its first and last RGBA
// controls. With 128-byte lines (the measuring host) that is every line a
// float row touches (96 bytes) and all but at most one of a double row's
// (192 bytes), which the next-line hardware prefetch then covers.
template<class Ops, class Scalar, bool Affine>
STX_CONV_INLINE void prefetch_group(const WarpGeometry<Scalar>& geometry, std::size_t row,
                                    std::size_t column) noexcept {
    const GroupCells<Scalar, Ops::width> cells = locate_group<Ops, Scalar, Affine>(geometry, row, column);
    for (std::size_t lane = 0; lane < Ops::width; ++lane) {
        for (std::size_t control_row = 0; control_row < 6; ++control_row) {
            const Scalar* first = cells.origin[lane] + control_row * geometry.lattice_row_stride;
            prefetch_read(first);
            prefetch_read(first + 5 * 4 + 3);
        }
    }
}

// The pixels [column, column + width) of `row`; output is the row's first
// pixel. Masked writes +0 outside the half-pixel source footprint; unmasked
// assumes every pixel is inside it. Prefetch (a streaming map) first
// prefetches the cells warp_prefetch_distance pixels ahead.
template<class Ops, class Scalar, bool Affine, bool Masked, bool Premultiplied, bool Prefetch>
STX_CONV_INLINE void warp_group(const WarpGeometry<Scalar>& geometry, std::size_t row, std::size_t column,
                                Scalar* output) noexcept {
    using Vector = typename Ops::Value;
    constexpr std::size_t width = Ops::width;
    if constexpr (Prefetch && warp_prefetch_distance > 0) {
        prefetch_group<Ops, Scalar, Affine>(geometry, row, column + warp_prefetch_distance);
    }
    const GroupCells<Scalar, width> cells = locate_group<Ops, Scalar, Affine>(geometry, row, column);
    const std::array<Scalar, width>& mapped_x = cells.mapped_x;
    const std::array<Scalar, width>& mapped_y = cells.mapped_y;
    std::array<bool, width> inside{};
    if constexpr (Masked) {
        const Scalar half = Scalar(0.5);
        const Scalar right_edge = static_cast<Scalar>(geometry.source_width) - half;
        const Scalar bottom_edge = static_cast<Scalar>(geometry.source_height) - half;
        for (std::size_t lane = 0; lane < width; ++lane) {
            const bool within_x = mapped_x[lane] >= -half && mapped_x[lane] < right_edge;
            const bool within_y = mapped_y[lane] >= -half && mapped_y[lane] < bottom_edge;
            inside[lane] = within_x && within_y;
        }
    }
    // Both bases for all lanes at once; each pixel's contraction takes its
    // weights from them by lane (mul_lane), with no weight store.
    std::array<Vector, 6> basis_x{};
    std::array<Vector, 6> basis_y{};
    quintic_basis<Ops>(Ops::load(cells.u.data()), basis_x);
    quintic_basis<Ops>(Ops::load(cells.v.data()), basis_y);
    warp_pixels<Ops, Scalar, Masked, Premultiplied>(cells.origin, geometry.lattice_row_stride, inside, basis_x, basis_y,
                                                    output + 4 * column, std::make_index_sequence<width>{});
}

// How the samples of one affine target row meet the lattice, found once per
// row as at most row_segment_limit segments. Where one source coordinate's
// cell fraction is exactly 0 or 1 at every pixel of a segment, each sample
// lies on a lattice line and the patch reduces to the six-control curve
// along it, as patch_rgba reduces it, with the same operation order:
// - row_curve: v in {0, 1}; the curve runs across a lattice row, weighted
//   by the u basis.
// - column_curve: u in {0, 1}; the curve runs down a lattice column,
//   weighted by the v basis.
// The line may change from pixel to pixel; only the fraction is fixed. Such
// rows come from axis-aligned maps, including those that rounding makes
// axis-aligned (a rotation by a computed 90 degrees has a tiny nonzero
// cosine). Three probe pixels decide whether a row is scanned for them;
// any other row is one general segment.
enum class RowShape { general, row_curve, column_curve };

struct RowSegment {
    RowShape shape = RowShape::general;
    ColumnInterval columns{};
};

inline constexpr std::size_t row_segment_limit = 8;

// Segments [0, count) cover a row's footprint interval in column order.
struct RowSegments {
    std::array<RowSegment, row_segment_limit> segments{};
    std::size_t count = 0;
};

// One node-domain coordinate's cell and fraction, as warp_group computes
// them (clamp into [0, extent - 1], cell at most extent - 2).
template<class Scalar>
void node_cell(Scalar mapped, std::size_t extent, std::size_t& cell, Scalar& fraction) noexcept {
    using Ops = ScalarOps<Scalar>;
    const Scalar node = Ops::min(Ops::max(mapped, Scalar(0)), static_cast<Scalar>(extent - 1));
    cell = std::min(static_cast<std::size_t>(node), extent - 2);
    fraction = node - static_cast<Scalar>(cell);
}

// Whether source coordinate `axis` (0 for x, 1 for y) of one target pixel
// lies on a lattice line, with mapping_group's rounding.
template<class Scalar>
bool on_lattice_line(const WarpGeometry<Scalar>& geometry, std::size_t row, std::size_t column,
                     std::size_t axis) noexcept {
    const Scalar target_x = static_cast<Scalar>(column);
    const Scalar target_y = static_cast<Scalar>(row);
    std::array<Scalar, 2> mapped{};
    mapping_group<ScalarOps<Scalar>, Scalar, true>(geometry.inverse, &target_x, &target_y, &mapped[0], &mapped[1], 0);
    const std::size_t extent = axis == 0 ? geometry.source_width : geometry.source_height;
    std::size_t cell = 0;
    Scalar fraction = Scalar(0);
    node_cell(mapped[axis], extent, cell, fraction);
    const bool result = fraction == Scalar(0) || fraction == Scalar(1);
    return result;
}

// Splits `inside` into alternating on-line and general runs of coordinate
// `axis`. False, with `result` unspecified, when the runs outnumber the
// limit.
template<class Scalar>
bool line_runs(const WarpGeometry<Scalar>& geometry, std::size_t row, ColumnInterval inside, std::size_t axis,
               RowSegments& result) noexcept {
    const RowShape curve = axis == 1 ? RowShape::row_curve : RowShape::column_curve;
    result.count = 0;
    std::size_t begin = inside.begin;
    bool on_line = on_lattice_line(geometry, row, begin, axis);
    for (std::size_t column = inside.begin + 1; column <= inside.end; ++column) {
        const bool next = column < inside.end && on_lattice_line(geometry, row, column, axis);
        if (column < inside.end && next == on_line) { continue; }
        if (result.count == row_segment_limit) { return false; }
        result.segments[result.count] = RowSegment{on_line ? curve : RowShape::general, ColumnInterval{begin, column}};
        ++result.count;
        begin = column;
        on_line = next;
    }
    return true;
}

// The segments of a nonempty footprint interval of an affine row.
template<class Scalar>
RowSegments row_segments(const WarpGeometry<Scalar>& geometry, std::size_t row, ColumnInterval inside) noexcept {
    RowSegments result{};
    const std::array<std::size_t, 3> probes{inside.begin, inside.begin + (inside.end - inside.begin) / 2,
                                            inside.end - 1};
    for (std::size_t axis = 2; axis-- > 0;) {
        std::size_t hits = 0;
        for (const std::size_t column : probes) {
            if (on_lattice_line(geometry, row, column, axis)) { ++hits; }
        }
        if (hits >= 2 && line_runs(geometry, row, inside, axis, result)) { return result; }
    }
    result.segments[0] = RowSegment{RowShape::general, inside};
    result.count = 1;
    return result;
}

// Pixel lane Lane of a curve group: six controls `step` apart weighted by
// lane Lane of the varying coordinate's basis; pixel has four Scalars.
template<class Ops, class Scalar, bool Premultiplied, std::size_t Lane>
STX_CONV_INLINE void curve_pixel(const Scalar* origin, std::size_t step, const std::array<typename Ops::Value, 6>& basis,
                                 Scalar* pixel) noexcept {
    using ChannelOps = NativeOps<Scalar>;
    using Channels = typename ChannelOps::Value;
    constexpr std::size_t groups = rgba_groups<Scalar>;
    std::array<Channels, groups> sums{};
    for (std::size_t group = 0; group < groups; ++group) { sums[group] = ChannelOps::set(0); }
    for (std::size_t control = 0; control < 6; ++control) {
        const Scalar* point = origin + control * step;
        for (std::size_t group = 0; group < groups; ++group) {
            const Channels value = ChannelOps::load(point + group * ChannelOps::width);
            sums[group] = ChannelOps::add(sums[group], weigh_channels<Ops, Lane, Scalar>(value, basis[control]));
        }
    }
    if constexpr (Premultiplied) { clamp_premultiplied<Scalar>(sums); }
    for (std::size_t group = 0; group < groups; ++group) {
        ChannelOps::store(pixel + group * ChannelOps::width, sums[group]);
    }
}

template<class Ops, class Scalar, bool Premultiplied, std::size_t... Lanes>
STX_CONV_INLINE void curve_pixels(const std::array<const Scalar*, Ops::width>& origin, std::size_t step,
                                  const std::array<typename Ops::Value, 6>& basis, Scalar* first_pixel,
                                  std::index_sequence<Lanes...>) noexcept {
    (curve_pixel<Ops, Scalar, Premultiplied, Lanes>(origin[Lanes], step, basis, first_pixel + 4 * Lanes), ...);
}

// The pixels [column, column + width) of a curve segment, all inside the
// footprint, the fixed coordinate's fraction 0 or 1 at every pixel. The
// varying coordinate's basis weights the six controls along each pixel's
// lattice line.
template<class Ops, class Scalar, RowShape Shape, bool Premultiplied>
STX_CONV_INLINE void curve_group(const WarpGeometry<Scalar>& geometry, std::size_t row, std::size_t column,
                                 Scalar* output) noexcept {
    using Vector = typename Ops::Value;
    constexpr std::size_t width = Ops::width;
    constexpr bool across = Shape == RowShape::row_curve;
    std::array<Scalar, width> target_x{};
    std::array<Scalar, width> target_y{};
    for (std::size_t lane = 0; lane < width; ++lane) {
        target_x[lane] = static_cast<Scalar>(column + lane);
        target_y[lane] = static_cast<Scalar>(row);
    }
    std::array<Scalar, width> mapped_x{};
    std::array<Scalar, width> mapped_y{};
    mapping_group<Ops, Scalar, true>(geometry.inverse, target_x.data(), target_y.data(), mapped_x.data(),
                                     mapped_y.data(), 0);
    const Vector last_x = Ops::set(static_cast<Scalar>(geometry.source_width - 1));
    const Vector last_y = Ops::set(static_cast<Scalar>(geometry.source_height - 1));
    std::array<Scalar, width> node_x{};
    std::array<Scalar, width> node_y{};
    Ops::store(node_x.data(), Ops::min(Ops::max(Ops::load(mapped_x.data()), Ops::set(0)), last_x));
    Ops::store(node_y.data(), Ops::min(Ops::max(Ops::load(mapped_y.data()), Ops::set(0)), last_y));
    // Controls along the curve: RGBA-adjacent across a lattice row, a lattice
    // row apart down a column. The fixed coordinate's line is its cell's
    // first or (fraction 1) last.
    const std::size_t step = across ? 4 : geometry.lattice_row_stride;
    std::array<Scalar, width> fraction{};
    std::array<const Scalar*, width> origin{};
    for (std::size_t lane = 0; lane < width; ++lane) {
        const std::size_t cell_x = std::min(static_cast<std::size_t>(node_x[lane]), geometry.source_width - 2);
        const std::size_t cell_y = std::min(static_cast<std::size_t>(node_y[lane]), geometry.source_height - 2);
        const Scalar u = node_x[lane] - static_cast<Scalar>(cell_x);
        const Scalar v = node_y[lane] - static_cast<Scalar>(cell_y);
        const std::size_t lattice_x = cell_x * 5 + (!across && u == Scalar(1) ? 5 : 0);
        const std::size_t lattice_y = cell_y * 5 + (across && v == Scalar(1) ? 5 : 0);
        fraction[lane] = across ? u : v;
        origin[lane] = geometry.controls + lattice_y * geometry.lattice_row_stride + lattice_x * 4;
    }
    std::array<Vector, 6> basis{};
    quintic_basis<Ops>(Ops::load(fraction.data()), basis);
    curve_pixels<Ops, Scalar, Premultiplied>(origin, step, basis, output + 4 * column, std::make_index_sequence<width>{});
}

template<class Scalar, RowShape Shape, bool Premultiplied>
void curve_columns(const WarpGeometry<Scalar>& geometry, std::size_t row, ColumnInterval columns,
                   Scalar* output) noexcept {
    using Ops = NativeOps<Scalar>;
    std::size_t column = columns.begin;
    for (; column + Ops::width <= columns.end; column += Ops::width) {
        curve_group<Ops, Scalar, Shape, Premultiplied>(geometry, row, column, output);
    }
    for (; column < columns.end; ++column) {
        curve_group<ScalarOps<Scalar>, Scalar, Shape, Premultiplied>(geometry, row, column, output);
    }
}

// Columns [begin, end) of `row`: whole lane groups, then single pixels.
template<class Scalar, bool Affine, bool Masked, bool Premultiplied, bool Prefetch>
void warp_columns(const WarpGeometry<Scalar>& geometry, std::size_t row, ColumnInterval columns,
                  Scalar* output) noexcept {
    using Ops = NativeOps<Scalar>;
    std::size_t column = columns.begin;
    for (; column + Ops::width <= columns.end; column += Ops::width) {
        warp_group<Ops, Scalar, Affine, Masked, Premultiplied, Prefetch>(geometry, row, column, output);
    }
    for (; column < columns.end; ++column) {
        warp_group<ScalarOps<Scalar>, Scalar, Affine, Masked, Premultiplied, false>(geometry, row, column, output);
    }
}

// The four half-planes of the half-pixel source footprint.
enum class FootprintEdge { left, top, right, bottom };

// Whether column `column` of `row` maps inside one edge, with the rounding
// warp_group uses.
template<class Scalar>
bool within_edge(const WarpGeometry<Scalar>& geometry, std::size_t row, std::size_t column,
                 FootprintEdge edge) noexcept {
    const Scalar target_x = static_cast<Scalar>(column);
    const Scalar target_y = static_cast<Scalar>(row);
    Scalar mapped_x = Scalar(0);
    Scalar mapped_y = Scalar(0);
    mapping_group<ScalarOps<Scalar>, Scalar, true>(geometry.inverse, &target_x, &target_y, &mapped_x, &mapped_y, 0);
    const Scalar half = Scalar(0.5);
    switch (edge) {
    case FootprintEdge::left: return mapped_x >= -half;
    case FootprintEdge::top: return mapped_y >= -half;
    case FootprintEdge::right: return mapped_x < static_cast<Scalar>(geometry.source_width) - half;
    case FootprintEdge::bottom: return mapped_y < static_cast<Scalar>(geometry.source_height) - half;
    }
    return false;
}

// The columns of `row` inside one edge. An affine map's rounded source
// coordinates are monotone along a row (each operation of mapping_group is a
// correctly rounded monotone function of the column), so every edge holds on
// a prefix or a suffix of the row: bisect for its end. target_width > 0.
template<class Scalar>
ColumnInterval edge_columns(const WarpGeometry<Scalar>& geometry, std::size_t row, std::size_t target_width,
                            FootprintEdge edge) noexcept {
    const std::size_t last = target_width - 1;
    const bool first_within = within_edge(geometry, row, 0, edge);
    const bool last_within = within_edge(geometry, row, last, edge);
    if (first_within == last_within) {
        const ColumnInterval uniform{0, first_within ? target_width : 0};
        return uniform;
    }
    // Invariant: `low` has first_within's answer, `high` the other.
    std::size_t low = 0;
    std::size_t high = last;
    while (high - low > 1) {
        const std::size_t middle = low + (high - low) / 2;
        const bool middle_within = within_edge(geometry, row, middle, edge);
        if (middle_within == first_within) {
            low = middle;
        } else {
            high = middle;
        }
    }
    const ColumnInterval prefix{0, high};
    const ColumnInterval suffix{high, target_width};
    return first_within ? prefix : suffix;
}

// The exact columns of `row` an affine map takes inside the footprint: the
// intersection of the four edges' prefixes and suffixes. target_width > 0.
template<class Scalar>
ColumnInterval affine_footprint(const WarpGeometry<Scalar>& geometry, std::size_t row,
                                std::size_t target_width) noexcept {
    ColumnInterval inside{0, target_width};
    constexpr std::array<FootprintEdge, 4> edges{FootprintEdge::left, FootprintEdge::top, FootprintEdge::right,
                                                 FootprintEdge::bottom};
    for (const FootprintEdge edge : edges) {
        const ColumnInterval columns = edge_columns(geometry, row, target_width, edge);
        inside.begin = std::max(inside.begin, columns.begin);
        inside.end = std::min(inside.end, columns.end);
    }
    if (inside.end < inside.begin) { inside.end = inside.begin; }
    return inside;
}

// A projective row in three kinds of column: outside `nonzero` every pixel
// is certainly outside the footprint (+0); inside `unmasked` every pixel is
// certainly inside it; the rest, two bands at most, run masked.
struct ProjectiveRow {
    ColumnInterval nonzero{};
    ColumnInterval unmasked{};
};

// Whether column `column` of `row` certainly lies inside (or, with
// Inside false, outside) one edge, in a sense that carries along the row:
// the computed coordinate clears the edge by twice the bound's error plus
// the threshold's own rounding, so the exact coordinate clears it by the
// error. The exact coordinate along a row is a ratio of affine functions
// with a denominator of one sign, so it is monotone; every column between
// two such columns then clears the edge by the error, and its computed
// coordinate, warp_group's, lies on the same side.
template<class Scalar, bool Inside>
bool certainly_on_side(const WarpGeometry<Scalar>& geometry, const MappingBound<Scalar>& bound, std::size_t row,
                       std::size_t column, FootprintEdge edge) noexcept {
    const Scalar target_x = static_cast<Scalar>(column);
    const Scalar target_y = static_cast<Scalar>(row);
    Scalar mapped_x = Scalar(0);
    Scalar mapped_y = Scalar(0);
    mapping_group<ScalarOps<Scalar>, Scalar, false>(geometry.inverse, &target_x, &target_y, &mapped_x, &mapped_y,
                                                    0);
    const Scalar unit = std::numeric_limits<Scalar>::epsilon() / Scalar(2);
    const Scalar half = Scalar(0.5);
    const bool horizontal = edge == FootprintEdge::left || edge == FootprintEdge::right;
    const Scalar mapped = horizontal ? mapped_x : mapped_y;
    const Scalar error = horizontal ? bound.error_x : bound.error_y;
    const Scalar extent = static_cast<Scalar>(horizontal ? geometry.source_width : geometry.source_height);
    const bool low_edge = edge == FootprintEdge::left || edge == FootprintEdge::top;
    const Scalar position = low_edge ? -half : extent - half;
    const Scalar margin = Scalar(2) * error + Scalar(2) * unit * std::fabs(position);
    // The inside of a low edge is above it, of a high edge below it.
    const bool above = low_edge == Inside;
    const bool result = above ? mapped > position + margin : mapped < position - margin;
    return result;
}

// The columns of `row` certain for one edge and side (certainly_on_side):
// all, none, or the prefix or suffix between one certain end column and
// another certain column found by bisection. target_width > 0.
template<class Scalar, bool Inside>
ColumnInterval certain_columns(const WarpGeometry<Scalar>& geometry, const MappingBound<Scalar>& bound,
                               std::size_t row, std::size_t target_width, FootprintEdge edge) noexcept {
    const std::size_t last = target_width - 1;
    const bool first_certain = certainly_on_side<Scalar, Inside>(geometry, bound, row, 0, edge);
    const bool last_certain = certainly_on_side<Scalar, Inside>(geometry, bound, row, last, edge);
    if (first_certain == last_certain) {
        const ColumnInterval uniform{0, first_certain ? target_width : 0};
        return uniform;
    }
    // Invariant: `low` has first_certain's answer, `high` the other.
    std::size_t low = 0;
    std::size_t high = last;
    while (high - low > 1) {
        const std::size_t middle = low + (high - low) / 2;
        const bool middle_certain = certainly_on_side<Scalar, Inside>(geometry, bound, row, middle, edge);
        if (middle_certain == first_certain) {
            low = middle;
        } else {
            high = middle;
        }
    }
    const ColumnInterval prefix{0, low + 1};
    const ColumnInterval suffix{high, target_width};
    return first_certain ? prefix : suffix;
}

// The layout of a projective row under the map's bound over the target.
// target_width > 0.
template<class Scalar>
ProjectiveRow projective_row(const WarpGeometry<Scalar>& geometry, const MappingBound<Scalar>& bound,
                             std::size_t row, std::size_t target_width) noexcept {
    ProjectiveRow result{ColumnInterval{0, target_width}, ColumnInterval{0, target_width}};
    constexpr std::array<FootprintEdge, 4> edges{FootprintEdge::left, FootprintEdge::top, FootprintEdge::right,
                                                 FootprintEdge::bottom};
    for (const FootprintEdge edge : edges) {
        const ColumnInterval outside = certain_columns<Scalar, false>(geometry, bound, row, target_width, edge);
        const bool outside_prefix = outside.begin == 0 && outside.end > 0;
        const bool outside_suffix = outside.end == target_width && outside.begin < target_width;
        if (outside_prefix) { result.nonzero.begin = std::max(result.nonzero.begin, outside.end); }
        if (outside_suffix) { result.nonzero.end = std::min(result.nonzero.end, outside.begin); }
        const ColumnInterval inside = certain_columns<Scalar, true>(geometry, bound, row, target_width, edge);
        result.unmasked.begin = std::max(result.unmasked.begin, inside.begin);
        result.unmasked.end = std::min(result.unmasked.end, inside.end);
    }
    if (result.nonzero.end < result.nonzero.begin) { result.nonzero.end = result.nonzero.begin; }
    // Certainly inside is never certainly outside, so a nonempty unmasked
    // interval already lies within nonzero; the clamp keeps an empty one
    // from reaching outside it.
    result.unmasked.begin = std::clamp(result.unmasked.begin, result.nonzero.begin, result.nonzero.end);
    result.unmasked.end = std::clamp(result.unmasked.end, result.unmasked.begin, result.nonzero.end);
    return result;
}

} // inline namespace STX_CONV_ISA
} // namespace stx_conv::kernels::detail
