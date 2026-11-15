// CONV surfaces (include/stx_conv/surface.hpp): line layouts and cell shapes,
// the binary64 line cell, and the tensor surface with its derivatives. The
// cell kernel is src/detail/surface_kernel.hpp.

#include <stx_conv/surface.hpp>

#include "detail/surface_kernel.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace stx_conv::surface {

namespace {

// A walk to the segment's ends stops after this many nodes: pick_jet only
// tells distances below two apart and spans below five, so a capped
// distance gives the same jet.
constexpr std::ptrdiff_t walk_limit = 5;

std::size_t wrapped_index(std::ptrdiff_t node, std::size_t nodes) noexcept {
    const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(nodes);
    const std::ptrdiff_t remainder = node % count;
    const std::ptrdiff_t wrapped = remainder < 0 ? remainder + count : remainder;
    return static_cast<std::size_t>(wrapped);
}

bool layout_valid(const LineLayout& layout) noexcept {
    const std::size_t minimum = layout.periodic ? 3U : 2U;
    if (layout.nodes < minimum) return false;
    const bool seams_fit = layout.seams.empty() || layout.seams.size() == layout.nodes;
    return seams_fit;
}

bool is_seam(const LineLayout& layout, std::ptrdiff_t node) noexcept {
    const std::ptrdiff_t last = static_cast<std::ptrdiff_t>(layout.nodes) - 1;
    if (!layout.periodic && (node <= 0 || node >= last)) return true;
    if (layout.seams.empty()) return false;
    const bool seam = layout.seams[wrapped_index(node, layout.nodes)] != 0U;
    return seam;
}

bool has_any_seam(const LineLayout& layout) noexcept {
    if (!layout.periodic) return true;
    for (const std::uint8_t entry : layout.seams) {
        if (entry != 0U) return true;
    }
    return false;
}

// The jet of a node `before` nodes after the start and `after` nodes before
// the end of its segment.
Jet pick_jet(std::ptrdiff_t before, std::ptrdiff_t after, Closure closure) noexcept {
    if (before >= 2 && after >= 2) return Jet::interior;
    const std::ptrdiff_t span = before + after;
    if (closure == Closure::paper && span >= 4) {
        if (before == 0) return Jet::paper_first;
        if (after == 0) return Jet::paper_last;
        return Jet::paper_central;
    }
    if (span >= 3) {
        if (before == 0) return Jet::first4;
        if (after == 0) return Jet::last4;
        if (before == 1) return Jet::second4;
        return Jet::penultimate4;
    }
    if (span == 2) {
        if (before == 0) return Jet::first3;
        if (after == 0) return Jet::last3;
        return Jet::central3;
    }
    const Jet secant = before == 0 ? Jet::first2 : Jet::last2;
    return secant;
}

// The jet of `node` as the cell [cell, cell + 1] sees it.
Jet jet_in_cell(const LineLayout& layout, bool any_seam, std::ptrdiff_t cell, std::ptrdiff_t node) noexcept {
    if (!any_seam) return Jet::interior;
    std::ptrdiff_t start = cell;
    while (!is_seam(layout, start) && cell - start < walk_limit) --start;
    std::ptrdiff_t end = cell + 1;
    while (!is_seam(layout, end) && end - (cell + 1) < walk_limit) ++end;
    const Jet jet = pick_jet(node - start, end - node, layout.closure);
    return jet;
}

CellShape shape_of(const LineLayout& layout, bool any_seam, std::size_t cell) noexcept {
    CellShape shape;
    const std::ptrdiff_t i = static_cast<std::ptrdiff_t>(cell);
    shape.has_previous = layout.periodic || cell > 0U;
    shape.has_next = layout.periodic || cell + 2U < layout.nodes;
    shape.left = jet_in_cell(layout, any_seam, i, i);
    shape.right = jet_in_cell(layout, any_seam, i, i + 1);
    shape.previous = shape.has_previous ? jet_in_cell(layout, any_seam, i - 1, i) : shape.left;
    shape.next = shape.has_next ? jet_in_cell(layout, any_seam, i + 1, i + 1) : shape.right;
    // Six samples y[i-2 .. i+3] inside one segment.
    shape.six = !is_seam(layout, i) && !is_seam(layout, i - 1) && !is_seam(layout, i + 1) && !is_seam(layout, i + 2);
    const bool near_open_end = !layout.periodic && (cell < 2U || cell + 3U >= layout.nodes);
    if (near_open_end) shape.six = false;
    return shape;
}

CellShape two_node_shape() noexcept {
    CellShape shape;
    shape.left = Jet::first2;
    shape.right = Jet::last2;
    shape.previous = Jet::first2;
    shape.next = Jet::last2;
    shape.has_previous = false;
    shape.has_next = false;
    shape.six = false;
    return shape;
}

// Only the four-sample end stencils reach three nodes before or four after
// a cell's first node.
bool reaches_low(Jet jet) noexcept { return jet == Jet::last4 || jet == Jet::paper_last; }
bool reaches_high(Jet jet) noexcept { return jet == Jet::first4 || jet == Jet::paper_first; }

std::size_t checked_product(std::size_t a, std::size_t b) {
    if (a != 0U && b > std::numeric_limits<std::size_t>::max() / a) {
        throw std::length_error("stx_conv::surface: size overflow");
    }
    const std::size_t product = a * b;
    return product;
}

bool shape_valid(const SurfaceShape& shape) noexcept {
    const std::size_t minimum_u = shape.periodic_u ? 3U : 2U;
    const bool channels_fit = shape.channels >= 1U && shape.channels <= Surface::maximum_channels;
    const bool valid = shape.nodes_u >= minimum_u && shape.nodes_v >= 2U && channels_fit;
    return valid;
}

std::size_t cells_along(std::size_t nodes, bool periodic) noexcept {
    const std::size_t cells = periodic ? nodes : nodes - 1U;
    return cells;
}

// The cell and local parameter in [0, 1] of a coordinate.
void locate(double x, std::size_t cells, bool periodic, std::size_t& cell, double& t) noexcept {
    const double extent = static_cast<double>(cells);
    double position = x;
    if (periodic) {
        // Past 2^52 cells the quotient rounds, and the product may round above the
        // position, so the remainder can fall on either side of [0, extent).
        position = position - std::floor(position / extent) * extent;
        if (!(position >= 0.0)) position = 0.0;
        if (position >= extent) position = 0.0;
    } else {
        position = position < 0.0 ? 0.0 : position > extent ? extent : position;
    }
    double whole = std::floor(position);
    if (whole >= extent) whole = extent - 1.0;
    cell = static_cast<std::size_t>(whole);
    t = position - whole;
}

detail::Cell<double> kernel_cell(const LineCell& cell) noexcept {
    detail::Cell<double> copy;
    copy.anchor = cell.anchor;
    copy.currents = cell.currents;
    copy.bubble = cell.bubble;
    return copy;
}

LineCell public_cell(const detail::Cell<double>& cell) noexcept {
    LineCell copy;
    copy.anchor = cell.anchor;
    copy.currents = cell.currents;
    copy.bubble = cell.bubble;
    return copy;
}

} // namespace

// -- lines ------------------------------------------------------------------------------

std::size_t cell_count(const LineLayout& layout) noexcept {
    if (!layout_valid(layout)) return 0U;
    const std::size_t cells = cells_along(layout.nodes, layout.periodic);
    return cells;
}

CellShape cell_shape(const LineLayout& layout, std::size_t cell) noexcept {
    if (!layout_valid(layout) || cell >= cells_along(layout.nodes, layout.periodic)) return two_node_shape();
    const CellShape shape = shape_of(layout, has_any_seam(layout), cell);
    return shape;
}

LineCell compile_line_cell(std::span<const double, 8> window, const CellShape& shape, Bubble bubble) noexcept {
    const detail::Cell<double> compiled = detail::compile_cell<double>(window, shape, bubble);
    const LineCell cell = public_cell(compiled);
    return cell;
}

double evaluate_line_cell(const LineCell& cell, double t) noexcept {
    const detail::Basis<double> basis = detail::make_basis(t);
    const double value = detail::evaluate_cell(kernel_cell(cell), basis);
    return value;
}

// -- the surface ------------------------------------------------------------------------

Surface::Surface(const SurfaceShape& requested) : shape_(requested) {
    if (!shape_valid(requested)) throw std::invalid_argument("stx_conv::surface: invalid surface shape");
    const std::size_t cells_u_count = cells_along(requested.nodes_u, requested.periodic_u);
    const std::size_t cells_v_count = requested.nodes_v - 1U;
    const std::size_t row_cells = checked_product(checked_product(requested.nodes_v, cells_u_count), requested.channels);
    // The node count itself must be representable for compile's check.
    static_cast<void>(checked_product(checked_product(requested.nodes_u, requested.nodes_v), requested.channels));
    rows_.resize(row_cells);
    shapes_u_.resize(cells_u_count);
    shapes_v_.resize(cells_v_count);
    reads_low_.resize(cells_v_count);
    reads_high_.resize(cells_v_count);
}

Surface::Surface(Surface&& other) noexcept
    : shape_(other.shape_), ready_(other.ready_), rows_(std::move(other.rows_)), shapes_u_(std::move(other.shapes_u_)),
      shapes_v_(std::move(other.shapes_v_)), reads_low_(std::move(other.reads_low_)),
      reads_high_(std::move(other.reads_high_)) {
    other.release();
}

Surface& Surface::operator=(Surface&& other) noexcept {
    if (this == &other) return *this;
    shape_ = other.shape_;
    ready_ = other.ready_;
    rows_ = std::move(other.rows_);
    shapes_u_ = std::move(other.shapes_u_);
    shapes_v_ = std::move(other.shapes_v_);
    reads_low_ = std::move(other.reads_low_);
    reads_high_ = std::move(other.reads_high_);
    other.release();
    return *this;
}

void Surface::release() noexcept {
    shape_ = SurfaceShape{};
    shape_.nodes_u = 0U;
    shape_.nodes_v = 0U;
    shape_.channels = 0U;
    ready_ = false;
    rows_.clear();
    rows_.shrink_to_fit();
    shapes_u_.clear();
    shapes_u_.shrink_to_fit();
    shapes_v_.clear();
    shapes_v_.shrink_to_fit();
    reads_low_.clear();
    reads_low_.shrink_to_fit();
    reads_high_.clear();
    reads_high_.shrink_to_fit();
}

std::size_t Surface::cells_u() const noexcept {
    if (shape_.nodes_u == 0U) return 0U;
    const std::size_t cells = cells_along(shape_.nodes_u, shape_.periodic_u);
    return cells;
}

std::size_t Surface::cells_v() const noexcept {
    const std::size_t cells = shape_.nodes_v == 0U ? 0U : shape_.nodes_v - 1U;
    return cells;
}

std::size_t Surface::storage_bytes() const noexcept {
    const std::size_t bytes = rows_.capacity() * sizeof(LineCell) +
                              (shapes_u_.capacity() + shapes_v_.capacity()) * sizeof(CellShape) +
                              reads_low_.capacity() + reads_high_.capacity();
    return bytes;
}

Surface::Location Surface::locate_u(double u) const noexcept {
    Location location;
    locate(u, cells_u(), shape_.periodic_u, location.cell, location.t);
    return location;
}

Surface::Location Surface::locate_v(double v) const noexcept {
    Location location;
    locate(v, cells_v(), false, location.cell, location.t);
    return location;
}

Status Surface::compile(std::span<const double> nodes, std::span<const std::uint8_t> seams_u,
                        std::span<const std::uint8_t> seams_v) noexcept {
    // Validation first: nothing is written until the whole input is known good.
    if (shape_.channels == 0U) return Status::invalid_shape;
    const std::size_t channels = shape_.channels;
    const std::size_t expected = shape_.nodes_u * shape_.nodes_v * channels;
    if (nodes.size() != expected) return Status::invalid_shape;
    if (!seams_u.empty() && seams_u.size() != shape_.nodes_u) return Status::invalid_shape;
    if (!seams_v.empty() && seams_v.size() != shape_.nodes_v) return Status::invalid_shape;
    for (const double node : nodes) {
        if (!std::isfinite(node)) return Status::nonfinite_input;
    }
    for (const double node : nodes) {
        if (std::fabs(node) > maximum_magnitude) return Status::input_out_of_range;
    }

    LineLayout layout_u;
    layout_u.nodes = shape_.nodes_u;
    layout_u.periodic = shape_.periodic_u;
    layout_u.closure = shape_.closure;
    layout_u.seams = seams_u;
    LineLayout layout_v;
    layout_v.nodes = shape_.nodes_v;
    layout_v.periodic = false;
    layout_v.closure = shape_.closure;
    layout_v.seams = seams_v;
    const bool any_seam_u = has_any_seam(layout_u);
    const bool any_seam_v = has_any_seam(layout_v);
    const std::size_t count_u = cells_u();
    const std::size_t count_v = cells_v();
    for (std::size_t cell = 0U; cell < count_u; ++cell) shapes_u_[cell] = shape_of(layout_u, any_seam_u, cell);
    for (std::size_t cell = 0U; cell < count_v; ++cell) {
        const CellShape form = shape_of(layout_v, any_seam_v, cell);
        shapes_v_[cell] = form;
        const bool low = reaches_low(form.left) || reaches_low(form.previous) || reaches_low(form.right);
        const bool high = reaches_high(form.right) || reaches_high(form.next) || reaches_high(form.left);
        reads_low_[cell] = static_cast<std::uint8_t>(low ? 1U : 0U);
        reads_high_[cell] = static_cast<std::uint8_t>(high ? 1U : 0U);
    }

    // The u-cells of every row, channel by channel, from an eight-node window
    // (wrapped around a periodic u; outside an open u the entries are zero
    // and never read).
    const std::ptrdiff_t node_count_u = static_cast<std::ptrdiff_t>(shape_.nodes_u);
    std::array<double, 8> window{};
    for (std::size_t row = 0U; row < shape_.nodes_v; ++row) {
        const std::size_t row_start = row * shape_.nodes_u;
        for (std::size_t cell = 0U; cell < count_u; ++cell) {
            for (std::size_t channel = 0U; channel < channels; ++channel) {
                for (std::size_t k = 0U; k < 8U; ++k) {
                    const std::ptrdiff_t index = static_cast<std::ptrdiff_t>(cell) - 3 + static_cast<std::ptrdiff_t>(k);
                    window[k] = 0.0;
                    if (shape_.periodic_u) {
                        const std::size_t wrapped = wrapped_index(index, shape_.nodes_u);
                        window[k] = nodes[(row_start + wrapped) * channels + channel];
                    } else if (index >= 0 && index < node_count_u) {
                        window[k] = nodes[(row_start + static_cast<std::size_t>(index)) * channels + channel];
                    }
                }
                const std::span<const double, 8> view(window);
                const detail::Cell<double> compiled = detail::compile_cell<double>(view, shapes_u_[cell], shape_.bubble);
                rows_[(row * count_u + cell) * channels + channel] = public_cell(compiled);
            }
        }
    }
    ready_ = true;
    return Status::ok;
}

Status Surface::evaluate(double u, double v, std::span<double> values) const noexcept {
    if (!ready_) return Status::unprepared_profile;
    if (!std::isfinite(u) || !std::isfinite(v)) return Status::input_out_of_range;
    const std::size_t channels = shape_.channels;
    if (values.size() < channels) return Status::insufficient_storage;
    const Location at_u = locate_u(u);
    const Location at_v = locate_v(v);
    const detail::Basis<double> basis_u = detail::make_basis(at_u.t);
    const detail::Basis<double> basis_v = detail::make_basis(at_v.t);
    const std::size_t count_u = cells_u();
    const std::ptrdiff_t node_count_v = static_cast<std::ptrdiff_t>(shape_.nodes_v);
    // The u-line values at u of the eight rows around the v-cell, per channel.
    std::array<std::array<double, 8>, maximum_channels> windows{};
    for (std::size_t k = 0U; k < 8U; ++k) {
        const std::ptrdiff_t row = static_cast<std::ptrdiff_t>(at_v.cell) - 3 + static_cast<std::ptrdiff_t>(k);
        const bool skipped = row < 0 || row >= node_count_v || (k == 0U && reads_low_[at_v.cell] == 0U) ||
                             (k == 7U && reads_high_[at_v.cell] == 0U);
        if (skipped) continue;
        const std::size_t first = (static_cast<std::size_t>(row) * count_u + at_u.cell) * channels;
        for (std::size_t channel = 0U; channel < channels; ++channel) {
            windows[channel][k] = detail::evaluate_cell(kernel_cell(rows_[first + channel]), basis_u);
        }
    }
    for (std::size_t channel = 0U; channel < channels; ++channel) {
        const std::span<const double, 8> view(windows[channel]);
        const detail::Cell<double> across = detail::compile_cell<double>(view, shapes_v_[at_v.cell], shape_.bubble);
        values[channel] = detail::evaluate_cell(across, basis_v);
    }
    return Status::ok;
}

Status Surface::evaluate_with_derivatives(double u, double v, SurfaceSample& sample) const noexcept {
    if (!ready_) return Status::unprepared_profile;
    if (!std::isfinite(u) || !std::isfinite(v)) return Status::input_out_of_range;
    const std::size_t channels = shape_.channels;
    const Location at_u = locate_u(u);
    const Location at_v = locate_v(v);
    // The rows' values at u carry their derivative along u; the v-cell is
    // admitted from those, so its currents carry it too. The derivative
    // along v comes from the basis alone.
    const detail::Basis<detail::Dual1> basis_u = detail::make_basis(detail::Dual1(at_u.t, 1.0));
    const detail::Basis<detail::Dual1> basis_v = detail::make_basis(detail::Dual1(at_v.t, 1.0));
    const std::size_t count_u = cells_u();
    const std::ptrdiff_t node_count_v = static_cast<std::ptrdiff_t>(shape_.nodes_v);
    std::array<std::array<detail::Dual1, 8>, maximum_channels> windows{};
    for (std::size_t k = 0U; k < 8U; ++k) {
        const std::ptrdiff_t row = static_cast<std::ptrdiff_t>(at_v.cell) - 3 + static_cast<std::ptrdiff_t>(k);
        const bool skipped = row < 0 || row >= node_count_v || (k == 0U && reads_low_[at_v.cell] == 0U) ||
                             (k == 7U && reads_high_[at_v.cell] == 0U);
        if (skipped) continue;
        const std::size_t first = (static_cast<std::size_t>(row) * count_u + at_u.cell) * channels;
        for (std::size_t channel = 0U; channel < channels; ++channel) {
            windows[channel][k] = detail::evaluate_cell(kernel_cell(rows_[first + channel]), basis_u);
        }
    }
    for (std::size_t channel = 0U; channel < channels; ++channel) {
        const std::span<const detail::Dual1, 8> view(windows[channel]);
        const detail::Cell<detail::Dual1> across =
            detail::compile_cell<detail::Dual1>(view, shapes_v_[at_v.cell], shape_.bubble);
        double value = across.anchor.value;
        double along = across.anchor.derivative;
        double crossing = 0.0;
        for (std::size_t j = 0U; j < 5U; ++j) {
            value = value + across.currents[j].value * basis_v.tau[j].value;
            along = along + across.currents[j].derivative * basis_v.tau[j].value;
            crossing = crossing + across.currents[j].value * basis_v.tau[j].derivative;
        }
        sample.value[channel] = value + across.bubble.value * basis_v.psi.value;
        sample.along_u[channel] = along + across.bubble.derivative * basis_v.psi.value;
        sample.along_v[channel] = crossing + across.bubble.value * basis_v.psi.derivative;
    }
    return Status::ok;
}

} // namespace stx_conv::surface
