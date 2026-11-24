// CONV surfaces (include/stx_conv/surface.hpp): the line cell against the
// stream model's reference lines, cell shapes, interpolation through every
// node, cubic reproduction with the cubic closure, seams, derivatives
// against central differences, monotone data staying monotone and bounded,
// periodic closure, and refusals that change nothing.
//
// line_reference.txt holds the first 100 of conv-teapot's 400 reference
// lines (malkuth-software/conv-teapot tests/line_reference.txt, MIT),
// written by this package's stream model (tests/stream/oracle): per line a
// header "n m", the n samples, the m-point nodal base line and the m-point
// standard-bubble line, as hexadecimal binary64.

#include <stx_conv/surface.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace cs = stx_conv::surface;
using stx_conv::Status;

void require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

std::vector<double> read_row(std::istream& in) {
    std::string line;
    std::getline(in, line);
    std::istringstream row(line);
    std::vector<double> values;
    std::string token;
    while (row >> token) values.push_back(std::strtod(token.c_str(), nullptr));
    return values;
}

// The m-point nodal resampling of y through compile_line_cell, as the
// stream model's nodal plan places its points.
std::vector<double> resample(const std::vector<double>& y, std::size_t m, cs::Bubble bubble) {
    const std::size_t n = y.size();
    std::vector<double> out(m, 0.0);
    cs::LineLayout layout;
    layout.nodes = n;
    layout.closure = cs::Closure::paper;
    for (std::size_t r = 0U; r < m; ++r) {
        const std::size_t numerator = r * (n - 1U);
        std::size_t cell = numerator / (m - 1U);
        double t = static_cast<double>(numerator % (m - 1U)) / static_cast<double>(m - 1U);
        if (cell == n - 1U) {
            cell = n - 2U;
            t = 1.0;
        }
        std::array<double, 8> window{};
        for (std::size_t k = 0U; k < 8U; ++k) {
            const std::ptrdiff_t index = static_cast<std::ptrdiff_t>(cell) - 3 + static_cast<std::ptrdiff_t>(k);
            if (index >= 0 && index < static_cast<std::ptrdiff_t>(n)) window[k] = y[static_cast<std::size_t>(index)];
        }
        const cs::LineCell compiled = cs::compile_line_cell(window, cs::cell_shape(layout, cell), bubble);
        out[r] = cs::evaluate_line_cell(compiled, t);
    }
    return out;
}

void line_reference(const char* path) {
    std::ifstream in(path);
    require(static_cast<bool>(in), "the reference lines are readable");
    double worst_base = 0.0;
    double worst_bubble = 0.0;
    std::size_t lines = 0U;
    std::string header;
    while (std::getline(in, header)) {
        std::istringstream sizes(header);
        std::size_t n = 0U;
        std::size_t m = 0U;
        sizes >> n >> m;
        const std::vector<double> y = read_row(in);
        const std::vector<double> base = read_row(in);
        const std::vector<double> bubble = read_row(in);
        require(y.size() == n && base.size() == m && bubble.size() == m, "a reference line is complete");
        const std::vector<double> mine_base = resample(y, m, cs::Bubble::none);
        const std::vector<double> mine_bubble = resample(y, m, cs::Bubble::standard);
        double scale = 1.0;
        for (const double value : y) scale = std::fmax(scale, std::fabs(value));
        for (std::size_t r = 0U; r < m; ++r) {
            worst_base = std::fmax(worst_base, std::fabs(mine_base[r] - base[r]) / scale);
            worst_bubble = std::fmax(worst_bubble, std::fabs(mine_bubble[r] - bubble[r]) / scale);
        }
        ++lines;
    }
    require(lines == 100U, "all 100 reference lines were read");
    require(worst_base < 1e-12, "the base line cell agrees with the stream model");
    require(worst_bubble < 1e-12, "the standard bubble agrees with the stream model");
}

void cell_shapes() {
    cs::LineLayout open;
    open.nodes = 8U;
    require(cs::cell_count(open) == 7U, "an open line of 8 nodes has 7 cells");
    const cs::CellShape first = cs::cell_shape(open, 0U);
    require(first.left == cs::Jet::first4 && first.right == cs::Jet::second4 && !first.has_previous && first.has_next &&
                !first.six,
            "the first cell takes the cubic-exact end stencils");
    const cs::CellShape middle = cs::cell_shape(open, 3U);
    require(middle.left == cs::Jet::interior && middle.right == cs::Jet::interior && middle.six,
            "an interior cell takes the centred stencil and the six-sample term");
    const cs::CellShape last = cs::cell_shape(open, 6U);
    require(last.left == cs::Jet::penultimate4 && last.right == cs::Jet::last4 && !last.has_next,
            "the last cell mirrors the first");
    open.closure = cs::Closure::paper;
    require(cs::cell_shape(open, 0U).left == cs::Jet::paper_first, "the paper closure at the start");
    cs::LineLayout ring;
    ring.nodes = 6U;
    ring.periodic = true;
    require(cs::cell_count(ring) == 6U && cs::cell_shape(ring, 0U).left == cs::Jet::interior &&
                cs::cell_shape(ring, 5U).has_next,
            "a periodic line without seams is interior throughout");
    const std::array<std::uint8_t, 6> seam_at_two{0U, 0U, 1U, 0U, 0U, 0U};
    ring.seams = seam_at_two;
    const cs::CellShape before_seam = cs::cell_shape(ring, 1U);
    const cs::CellShape after_seam = cs::cell_shape(ring, 2U);
    require(before_seam.right != cs::Jet::interior && after_seam.left != cs::Jet::interior &&
                after_seam.previous == before_seam.right && !after_seam.six,
            "a seam cuts the stencils on both sides and the neighbours agree on the shared node");
    const std::array<std::uint8_t, 3> wrong_length{0U, 0U, 0U};
    ring.seams = wrong_length;
    require(cs::cell_count(ring) == 0U && cs::cell_shape(ring, 0U).left == cs::Jet::first2,
            "a seam table of the wrong length is refused");
    cs::LineLayout single;
    single.nodes = 1U;
    require(cs::cell_count(single) == 0U, "one node has no cells");
}

cs::SurfaceShape shape_of(std::size_t nodes_u, std::size_t nodes_v, std::size_t channels, bool periodic_u) {
    cs::SurfaceShape shape;
    shape.nodes_u = nodes_u;
    shape.nodes_v = nodes_v;
    shape.channels = channels;
    shape.periodic_u = periodic_u;
    return shape;
}

// A fixed sequence in [0, 1), so a failure reproduces.
struct Sequence final {
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    double next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const std::uint64_t high = state >> 11U;
        const double value = static_cast<double>(high) / 9007199254740992.0;
        return value;
    }
};

void interpolates_nodes() {
    for (std::size_t pass = 0U; pass < 2U; ++pass) {
        const bool periodic = pass == 1U;
        const std::size_t nu = 9U;
        const std::size_t nv = 7U;
        const std::size_t channels = 3U;
        std::vector<double> nodes(nu * nv * channels, 0.0);
        Sequence sequence;
        for (double& node : nodes) node = sequence.next() * 4.0 - 2.0;
        cs::Surface surface(shape_of(nu, nv, channels, periodic));
        require(surface.compile(nodes, {}, {}) == Status::ok && surface.ready(), "a random lattice compiles");
        std::array<double, 4> values{};
        double worst = 0.0;
        for (std::size_t v = 0U; v < nv; ++v) {
            for (std::size_t u = 0U; u < nu; ++u) {
                require(surface.evaluate(static_cast<double>(u), static_cast<double>(v), values) == Status::ok,
                        "evaluation at a node");
                for (std::size_t channel = 0U; channel < channels; ++channel) {
                    const double expected = nodes[(v * nu + u) * channels + channel];
                    worst = std::fmax(worst, std::fabs(values[channel] - expected));
                }
            }
        }
        require(worst < 1e-12, "the surface passes through every node");
        cs::SurfaceSample sample;
        require(surface.evaluate_with_derivatives(2.0, 3.0, sample) == Status::ok &&
                    std::fabs(sample.value[1] - nodes[(3U * nu + 2U) * channels + 1U]) < 1e-12,
                "the derivative path gives the same value at a node");
    }
}

double cubic(double x) {
    const double value = 0.002 * x * x * x + 0.05 * x * x + x;
    return value;
}

void reproduces_cubics() {
    const std::size_t nu = 12U;
    const std::size_t nv = 2U;
    std::vector<double> nodes(nu * nv, 0.0);
    for (std::size_t v = 0U; v < nv; ++v) {
        for (std::size_t u = 0U; u < nu; ++u) nodes[v * nu + u] = cubic(static_cast<double>(u));
    }
    cs::Surface surface(shape_of(nu, nv, 1U, false));
    require(surface.compile(nodes, {}, {}) == Status::ok, "the cubic lattice compiles");
    std::array<double, 1> value{};
    double worst = 0.0;
    for (std::size_t step = 0U; step <= 110U; ++step) {
        const double u = static_cast<double>(step) * 0.1;
        require(surface.evaluate(u, 0.5, value) == Status::ok, "evaluation along the cubic");
        worst = std::fmax(worst, std::fabs(value[0] - cubic(u)));
    }
    require(worst < 1e-10, "the cubic closure reproduces a cubic, ends included");
}

// Two linear pieces with a kink at node 5.
double kinked(double x) {
    const double value = x <= 5.0 ? x : 5.0 + 3.0 * (x - 5.0);
    return value;
}

void seams_keep_kinks() {
    const std::size_t nu = 11U;
    const std::size_t nv = 2U;
    std::vector<double> nodes(nu * nv, 0.0);
    for (std::size_t v = 0U; v < nv; ++v) {
        for (std::size_t u = 0U; u < nu; ++u) nodes[v * nu + u] = kinked(static_cast<double>(u));
    }
    std::vector<std::uint8_t> seams(nu, 0U);
    seams[5] = 1U;
    cs::Surface seamed(shape_of(nu, nv, 1U, false));
    cs::Surface smooth(shape_of(nu, nv, 1U, false));
    require(seamed.compile(nodes, seams, {}) == Status::ok && smooth.compile(nodes, {}, {}) == Status::ok,
            "both lattices compile");
    std::array<double, 1> value{};
    double worst_seamed = 0.0;
    double worst_smooth = 0.0;
    for (std::size_t step = 0U; step <= 100U; ++step) {
        const double u = static_cast<double>(step) * 0.1;
        require(seamed.evaluate(u, 0.0, value) == Status::ok, "seamed evaluation");
        worst_seamed = std::fmax(worst_seamed, std::fabs(value[0] - kinked(u)));
        require(smooth.evaluate(u, 0.0, value) == Status::ok, "seamless evaluation");
        worst_smooth = std::fmax(worst_smooth, std::fabs(value[0] - kinked(u)));
    }
    require(worst_seamed < 1e-11, "with a seam each piece is reproduced and the kink stays sharp");
    require(worst_smooth > 1e-3, "without the seam the kink is rounded (the seam matters)");
}

double field(double u, double v, std::size_t channel) {
    const double scale = static_cast<double>(channel + 1U);
    const double value = std::sin(0.3 * u * scale) * std::cos(0.25 * v) + 0.1 * u * v;
    return value;
}

void derivatives_match_differences() {
    const std::size_t nu = 14U;
    const std::size_t nv = 11U;
    const std::size_t channels = 2U;
    std::vector<double> nodes(nu * nv * channels, 0.0);
    for (std::size_t v = 0U; v < nv; ++v) {
        for (std::size_t u = 0U; u < nu; ++u) {
            for (std::size_t channel = 0U; channel < channels; ++channel) {
                nodes[(v * nu + u) * channels + channel] = field(static_cast<double>(u), static_cast<double>(v), channel);
            }
        }
    }
    cs::Surface surface(shape_of(nu, nv, channels, false));
    require(surface.compile(nodes, {}, {}) == Status::ok, "the smooth lattice compiles");
    Sequence sequence;
    const double h = 1e-5;
    double worst = 0.0;
    std::array<double, 4> plus{};
    std::array<double, 4> minus{};
    for (std::size_t trial = 0U; trial < 200U; ++trial) {
        // Inside a cell, away from its edges, where the surface is smooth.
        const double u = std::floor(sequence.next() * 12.0) + 0.1 + 0.8 * sequence.next();
        const double v = std::floor(sequence.next() * 9.0) + 0.1 + 0.8 * sequence.next();
        cs::SurfaceSample sample;
        require(surface.evaluate_with_derivatives(u, v, sample) == Status::ok, "the derivative evaluation");
        require(surface.evaluate(u + h, v, plus) == Status::ok && surface.evaluate(u - h, v, minus) == Status::ok,
                "the u differences");
        for (std::size_t channel = 0U; channel < channels; ++channel) {
            const double difference = (plus[channel] - minus[channel]) / (2.0 * h);
            worst = std::fmax(worst, std::fabs(difference - sample.along_u[channel]));
        }
        require(surface.evaluate(u, v + h, plus) == Status::ok && surface.evaluate(u, v - h, minus) == Status::ok,
                "the v differences");
        for (std::size_t channel = 0U; channel < channels; ++channel) {
            const double difference = (plus[channel] - minus[channel]) / (2.0 * h);
            worst = std::fmax(worst, std::fabs(difference - sample.along_v[channel]));
        }
    }
    require(worst < 1e-6, "both derivatives agree with central differences");
}

void monotone_stays_monotone() {
    const std::size_t nu = 16U;
    const std::size_t nv = 2U;
    Sequence sequence;
    for (std::size_t trial = 0U; trial < 20U; ++trial) {
        std::vector<double> line(nu, 0.0);
        double level = 0.0;
        for (std::size_t u = 0U; u < nu; ++u) {
            // Steps, plateaus and jumps.
            const double draw = sequence.next();
            const double step = draw < 0.4 ? 0.0 : draw < 0.8 ? sequence.next() : 5.0 * sequence.next();
            level = level + step;
            line[u] = level;
        }
        std::vector<double> nodes(nu * nv, 0.0);
        for (std::size_t v = 0U; v < nv; ++v) {
            for (std::size_t u = 0U; u < nu; ++u) nodes[v * nu + u] = line[u];
        }
        cs::Surface surface(shape_of(nu, nv, 1U, false));
        require(surface.compile(nodes, {}, {}) == Status::ok, "the monotone lattice compiles");
        std::array<double, 1> value{};
        double previous = -std::numeric_limits<double>::infinity();
        bool monotone = true;
        bool bounded = true;
        for (std::size_t step = 0U; step <= 1500U; ++step) {
            const double u = static_cast<double>(step) * 0.01;
            require(surface.evaluate(u, 0.25, value) == Status::ok, "monotone evaluation");
            if (value[0] < previous - 1e-12) monotone = false;
            if (value[0] < line.front() - 1e-12 || value[0] > line.back() + 1e-12) bounded = false;
            previous = value[0];
        }
        require(monotone, "monotone data gives a monotone surface (no wiggle on plateaus or steps)");
        require(bounded, "the surface never overshoots the data");
    }
}

void periodic_closure() {
    const std::size_t nu = 12U;
    const std::size_t nv = 3U;
    const std::size_t channels = 2U;
    std::vector<double> nodes(nu * nv * channels, 0.0);
    const double turn = 6.283185307179586;
    for (std::size_t v = 0U; v < nv; ++v) {
        for (std::size_t u = 0U; u < nu; ++u) {
            const double angle = turn * static_cast<double>(u) / static_cast<double>(nu);
            const double radius = 1.0 + 0.25 * static_cast<double>(v);
            nodes[(v * nu + u) * channels] = radius * std::cos(angle);
            nodes[(v * nu + u) * channels + 1U] = radius * std::sin(angle);
        }
    }
    cs::Surface surface(shape_of(nu, nv, channels, true));
    require(surface.compile(nodes, {}, {}) == Status::ok && surface.cells_u() == nu, "a ring closes around u");
    std::array<double, 4> end{};
    std::array<double, 4> start{};
    std::array<double, 4> wrapped{};
    require(surface.evaluate(static_cast<double>(nu) - 1e-9, 1.5, end) == Status::ok &&
                surface.evaluate(0.0, 1.5, start) == Status::ok,
            "evaluation on both sides of the closure");
    require(std::fabs(end[0] - start[0]) < 1e-7 && std::fabs(end[1] - start[1]) < 1e-7,
            "the surface is continuous across the periodic closure");
    require(surface.evaluate(-0.5, 1.5, wrapped) == Status::ok &&
                surface.evaluate(static_cast<double>(nu) - 0.5, 1.5, end) == Status::ok && wrapped[0] == end[0] &&
                wrapped[1] == end[1],
            "u wraps");
    double worst_radius = 0.0;
    for (std::size_t step = 0U; step < 120U; ++step) {
        const double u = static_cast<double>(step) * 0.1;
        require(surface.evaluate(u, 0.0, end) == Status::ok, "evaluation around the ring");
        worst_radius = std::fmax(worst_radius, std::fabs(std::hypot(end[0], end[1]) - 1.0));
    }
    require(worst_radius < 1e-2, "a ring of 12 nodes stays close to the circle");

    // A-stx-codecs-01-F3: far out, the wrap's quotient rounds and its remainder can fall below
    // zero (here -2 for three cells). The coordinate is clamped into the first cell instead of
    // converting a negative cell index.
    const std::size_t small_u = 3U;
    std::vector<double> small_nodes(small_u * nv, 0.0);
    for (std::size_t v = 0U; v < nv; ++v) {
        for (std::size_t u = 0U; u < small_u; ++u) small_nodes[v * small_u + u] = static_cast<double>(u);
    }
    cs::Surface small(shape_of(small_u, nv, 1U, true));
    require(small.compile(small_nodes, {}, {}) == Status::ok, "a three-cell ring compiles");
    std::array<double, 1> far{-7.0};
    std::array<double, 1> origin{-7.0};
    cs::SurfaceSample far_sample;
    require(small.evaluate(13510798882111490.0, 1.0, far) == Status::ok &&
                small.evaluate(0.0, 1.0, origin) == Status::ok && far[0] == origin[0],
            "a remainder that rounds below zero wraps to the start of the ring");
    require(small.evaluate_with_derivatives(13510798882111490.0, 1.0, far_sample) == Status::ok &&
                far_sample.value[0] == origin[0],
            "the derivative path wraps the same coordinate the same way");
}

void refusals() {
    bool threw = false;
    try {
        const cs::Surface bad(shape_of(1U, 4U, 1U, false));
        static_cast<void>(bad);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "one node along u is refused");
    threw = false;
    try {
        const cs::Surface bad(shape_of(4U, 4U, 5U, false));
        static_cast<void>(bad);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "five channels are refused");

    const std::size_t nu = 5U;
    const std::size_t nv = 4U;
    std::vector<double> nodes(nu * nv, 1.0);
    cs::Surface surface(shape_of(nu, nv, 1U, false));
    std::array<double, 1> value{-7.0};
    require(surface.evaluate(1.0, 1.0, value) == Status::unprepared_profile && value[0] == -7.0,
            "an unprepared surface refuses evaluation and writes nothing");
    const std::span<const double> short_nodes(nodes.data(), nodes.size() - 1U);
    require(surface.compile(short_nodes, {}, {}) == Status::invalid_shape && !surface.ready(),
            "a short lattice is refused and the surface stays unprepared");
    require(surface.compile(nodes, {}, {}) == Status::ok, "the lattice compiles");
    nodes[7] = std::numeric_limits<double>::quiet_NaN();
    require(surface.compile(nodes, {}, {}) == Status::nonfinite_input && surface.ready(),
            "a non-finite node is refused and the compiled surface is kept");
    require(surface.evaluate(1.0, 1.0, value) == Status::ok && value[0] == 1.0, "the kept surface is the earlier one");
    nodes[7] = 1e101;
    require(surface.compile(nodes, {}, {}) == Status::input_out_of_range, "a magnitude past the limit is refused");
    const std::array<std::uint8_t, 2> short_seams{0U, 0U};
    nodes[7] = 1.0;
    require(surface.compile(nodes, short_seams, {}) == Status::invalid_shape, "a seam table of the wrong length");
    value[0] = -7.0;
    require(surface.evaluate(std::numeric_limits<double>::quiet_NaN(), 1.0, value) == Status::input_out_of_range &&
                value[0] == -7.0,
            "a non-finite coordinate is refused");
    const std::span<double> empty_output(value.data(), 0U);
    require(surface.evaluate(1.0, 1.0, empty_output) == Status::insufficient_storage, "a short output is refused");
    cs::Surface moved(std::move(surface));
    require(moved.ready() && !surface.ready() && surface.cells_u() == 0U, "a move transfers the surface");
    require(surface.compile(nodes, {}, {}) == Status::invalid_shape, "a moved-from surface refuses compile");
    require(moved.evaluate(1.5, 1.5, value) == Status::ok && std::fabs(value[0] - 1.0) < 1e-12,
            "a constant lattice gives a constant surface");
}

} // namespace

int main(int argc, char** argv) {
    require(argc > 1, "the reference file is passed as the first argument");
    line_reference(argv[1]);
    cell_shapes();
    interpolates_nodes();
    reproduces_cubics();
    seams_keep_kinks();
    derivatives_match_differences();
    monotone_stays_monotone();
    periodic_closure();
    refusals();
    std::puts("stx_conv surface: reference lines, cell shapes, interpolation, cubic reproduction, seams, "
              "derivatives, monotonicity, periodic closure and refusals passed");
    return 0;
}
