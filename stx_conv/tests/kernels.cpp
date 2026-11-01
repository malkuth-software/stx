// stx_conv::kernels and the stream's bubble toggle:
// - the leaf batches on closed-form inputs, with guard samples on both sides
//   and a lane count (19) that leaves a scalar tail for every backend;
// - tensor_quintic against an independent long-double de Casteljau oracle,
//   and the RGBA patch's grid-line and node reductions against it;
// - integrate_patch_rgba's physical projection before quadrature;
// - map_points and sample_rgba on resize, rotation, skew, translation and
//   perspective maps of an affine ramp, transparent exteriors, horizons,
//   overflow and aliasing, with refused calls leaving the output unchanged;
// - the stream's bubble toggle: on by default, effective, repeatable.
#include <stx_conv/kernels.hpp>
#include <stx_conv/stream.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}

template<class Scalar>
bool near(Scalar actual, Scalar expected, Scalar tolerance) {
    const bool result = std::abs(actual - expected) < tolerance;
    return result;
}

template<class Scalar>
void leaf_kernels() {
    // Streams of n lanes inside storage of n + 2: one guard sample each side.
    constexpr std::size_t n = 19;
    constexpr std::size_t stride = n + 2;
    using Lanes = std::array<Scalar, stride>;
    std::array<Lanes, 5> source{};
    std::array<Lanes, 5> admitted{};
    std::array<Lanes, 5> capacity_storage{};
    std::array<const Scalar*, 5> nodes{};
    std::array<const Scalar*, 5> raw{};
    std::array<const Scalar*, 5> capacities{};
    std::array<Scalar*, 5> output{};
    Lanes mass{};
    Lanes threshold{};
    Lanes fifths{};
    Lanes two_fifths{};
    Lanes twentieths{};
    Lanes y0{};
    Lanes y1{};
    Lanes y2{};
    Lanes next{};
    Lanes left_delta{};
    Lanes left_last{};
    Lanes amplitude{};
    Lanes value{};
    const Scalar epsilon = Scalar(64) * std::numeric_limits<Scalar>::epsilon();
    for (std::size_t slot = 0; slot < 5; ++slot) {
        for (std::size_t lane = 0; lane < stride; ++lane) {
            source[slot][lane] = Scalar(slot) + Scalar(lane) * Scalar(0.1);
            capacity_storage[slot][lane] = Scalar(1);
            admitted[slot][lane] = Scalar(0.2);
        }
        nodes[slot] = source[slot].data() + 1;
        capacities[slot] = capacity_storage[slot].data() + 1;
        raw[slot] = admitted[slot].data() + 1;
        output[slot] = source[slot].data() + 1;
    }
    for (std::size_t lane = 0; lane < stride; ++lane) {
        mass[lane] = Scalar(5) * Scalar(lane) / Scalar(stride);
        y1[lane] = Scalar(1);
        y2[lane] = Scalar(2);
        next[lane] = Scalar(0.2);
        left_delta[lane] = Scalar(1);
        left_last[lane] = Scalar(0.2);
    }
    // Equal capacities: the threshold is mass / 5. Linear nodes 0.1 apart in
    // the lane and 1 apart across streams: first = 1, second = 0.
    stx_conv::kernels::capacity<Scalar>(capacities.data(), mass.data() + 1, threshold.data() + 1, n);
    stx_conv::kernels::jet_terms<Scalar>(nodes.data(), fifths.data() + 1, two_fifths.data() + 1, twentieths.data() + 1, n);
    for (std::size_t lane = 1; lane <= n; ++lane) {
        require(near(threshold[lane], mass[lane] / Scalar(5), epsilon), "capacity batch failed");
        const bool jets = near(fifths[lane], Scalar(0.2), epsilon) && near(two_fifths[lane], Scalar(0.4), epsilon)
            && near(twentieths[lane], Scalar(0), epsilon);
        require(jets, "jet batch failed");
    }
    // Uniform currents of a unit step are admitted unchanged.
    stx_conv::kernels::admit_cell<Scalar>(raw.data(), output.data(), y0.data() + 1, y1.data() + 1, y2.data() + 1,
                                          next.data() + 1, left_delta.data() + 1, left_last.data() + 1, n);
    for (std::size_t slot = 0; slot < 5; ++slot) {
        for (std::size_t lane = 1; lane <= n; ++lane) {
            require(near(source[slot][lane], Scalar(0.2), epsilon), "admission batch failed");
        }
    }
    // Equal raw and admitted uniform currents carry no bubble.
    std::array<Scalar, 5 * stride> packed{};
    packed.fill(Scalar(0.2));
    stx_conv::kernels::bubble<Scalar>(packed.data(), packed.data(), amplitude.data() + 1, stride, n, true);
    for (std::size_t lane = 1; lane <= n; ++lane) { require(near(amplitude[lane], Scalar(0), epsilon), "bubble batch failed"); }
    // Weights summing to 80/32 over currents of 0.2 give 0.5 per call; the
    // opening call writes it and the accumulating call adds it.
    const std::array<Scalar, 5> weights{Scalar(31) / 32, Scalar(26) / 32, Scalar(16) / 32, Scalar(6) / 32, Scalar(1) / 32};
    stx_conv::kernels::synthesize<Scalar>(y0.data() + 1, packed.data() + 1, stride, weights, Scalar(1), nullptr,
                                          Scalar(0), value.data() + 1, n, false);
    stx_conv::kernels::synthesize<Scalar>(y0.data() + 1, packed.data() + 1, stride, weights, Scalar(1), nullptr,
                                          Scalar(0), value.data() + 1, n, true);
    for (std::size_t lane = 1; lane <= n; ++lane) { require(near(value[lane], Scalar(1), epsilon), "synthesis batch failed"); }
    const bool guards = threshold.front() == Scalar(0) && threshold.back() == Scalar(0) && value.front() == Scalar(0)
        && value.back() == Scalar(0);
    require(guards, "a batch wrote a guard sample");
}

// Independent long-double de Casteljau evaluation of position `p`.
template<class Scalar, std::size_t Positions>
long double de_casteljau(const std::array<std::array<Scalar, Positions>, 36>& controls, std::size_t p, Scalar u,
                         Scalar v) {
    const long double lu = static_cast<long double>(u);
    const long double lv = static_cast<long double>(v);
    std::array<long double, 6> rows{};
    for (std::size_t y = 0; y < 6; ++y) {
        std::array<long double, 6> row{};
        for (std::size_t x = 0; x < 6; ++x) { row[x] = static_cast<long double>(controls[y * 6 + x][p]); }
        for (std::size_t level = 5; level > 0; --level) {
            for (std::size_t x = 0; x < level; ++x) { row[x] = (1 - lu) * row[x] + lu * row[x + 1]; }
        }
        rows[y] = row[0];
    }
    for (std::size_t level = 5; level > 0; --level) {
        for (std::size_t y = 0; y < level; ++y) { rows[y] = (1 - lv) * rows[y] + lv * rows[y + 1]; }
    }
    return rows[0];
}

template<class Scalar>
void tensor_patches() {
    constexpr std::size_t n = 19;
    const Scalar tolerance = Scalar(128) * std::numeric_limits<Scalar>::epsilon();
    std::array<std::array<Scalar, n>, 36> controls{};
    std::array<const Scalar*, 36> views{};
    std::array<Scalar, n> u{};
    std::array<Scalar, n> v{};
    std::array<Scalar, n> evaluated{};
    for (std::size_t p = 0; p < n; ++p) {
        u[p] = Scalar(p) / Scalar(n - 1);
        v[p] = Scalar(n - 1 - p) / Scalar(n - 1);
    }
    for (std::size_t control = 0; control < 36; ++control) {
        views[control] = controls[control].data();
        for (std::size_t p = 0; p < n; ++p) {
            const double phase = static_cast<double>(control * 7 + p * 3);
            controls[control][p] = Scalar(std::sin(phase) * 0.4 + 0.5);
        }
    }
    stx_conv::kernels::tensor_quintic<Scalar>(views.data(), u.data(), v.data(), evaluated.data(), n);
    for (std::size_t p = 0; p < n; ++p) {
        const long double expected = de_casteljau(controls, p, u[p], v[p]);
        const long double error = std::abs(static_cast<long double>(evaluated[p]) - expected);
        require(error < static_cast<long double>(tolerance), "tensor disagrees with the de Casteljau oracle");
    }
    // An RGBA patch: 36 controls of four channels, rows of 24 Scalars.
    std::array<Scalar, 144> patch{};
    std::array<const Scalar*, 36> patch_views{};
    for (std::size_t index = 0; index < patch.size(); ++index) {
        patch[index] = Scalar(0.5 + 0.4 * std::sin(static_cast<double>(index) * 0.31));
    }
    for (std::size_t control = 0; control < 36; ++control) { patch_views[control] = patch.data() + control * 4; }
    // Nodes, grid lines and an interior point against the generic evaluator.
    const std::array<std::array<Scalar, 2>, 5> fractions{{
        {Scalar(0), Scalar(0)}, {Scalar(1), Scalar(0.3)}, {Scalar(0.2), Scalar(1)}, {Scalar(0.4), Scalar(0.6)},
        {Scalar(0), Scalar(1)},
    }};
    for (const std::array<Scalar, 2>& fraction : fractions) {
        const std::array<Scalar, 4> pu{fraction[0], fraction[0], fraction[0], fraction[0]};
        const std::array<Scalar, 4> pv{fraction[1], fraction[1], fraction[1], fraction[1]};
        std::array<Scalar, 4> generic{};
        std::array<Scalar, 4> specialized{};
        stx_conv::kernels::tensor_quintic<Scalar>(patch_views.data(), pu.data(), pv.data(), generic.data(), 4);
        stx_conv::kernels::tensor_patch_rgba<Scalar>(patch.data(), 24, fraction[0], fraction[1], specialized.data());
        for (std::size_t channel = 0; channel < 4; ++channel) {
            require(near(generic[channel], specialized[channel], tolerance), "patch specialization differs");
        }
    }
    // Red falls 0.7 -> 0.3 and alpha rises 0.2 -> 0.8 across u: red exceeds
    // alpha at u = 0, so it is projected to alpha (0.2) before weighting.
    for (std::size_t y = 0; y < 6; ++y) {
        for (std::size_t x = 0; x < 6; ++x) {
            const Scalar fraction = Scalar(x) / Scalar(5);
            const std::size_t offset = (y * 6 + x) * 4;
            patch[offset] = Scalar(0.7) - Scalar(0.4) * fraction;
            patch[offset + 1] = Scalar(0.05);
            patch[offset + 2] = Scalar(0.1);
            patch[offset + 3] = Scalar(0.2) + Scalar(0.6) * fraction;
        }
    }
    const std::array<Scalar, 2> quadrature_u{Scalar(0), Scalar(1)};
    const std::array<Scalar, 2> quadrature_v{Scalar(0.5), Scalar(0.5)};
    const std::array<Scalar, 2> quadrature_weights{Scalar(0.5), Scalar(0.5)};
    std::array<Scalar, 4> sums{};
    stx_conv::kernels::integrate_patch_rgba<Scalar>(patch.data(), 24, quadrature_u.data(), quadrature_v.data(),
                                                    quadrature_weights.data(), 2, sums.data());
    const bool projected_first = near(sums[0], Scalar(0.25), tolerance) && near(sums[3], Scalar(0.5), tolerance);
    require(projected_first, "physical projection was moved after quadrature");
    stx_conv::kernels::integrate_patch_rgba<Scalar>(nullptr, 0, nullptr, nullptr, nullptr, 0, nullptr);
}

template<class Scalar>
void mapping_and_atlas() {
    constexpr std::size_t n = 19;
    const Scalar tolerance = Scalar(128) * std::numeric_limits<Scalar>::epsilon();
    std::array<Scalar, n> x{};
    std::array<Scalar, n> y{};
    std::array<Scalar, n> source_x{};
    std::array<Scalar, n> source_y{};
    for (std::size_t p = 0; p < n; ++p) {
        x[p] = Scalar(p) / Scalar(n - 1);
        y[p] = Scalar(n - 1 - p) / Scalar(n - 1);
    }
    // Resize/stretch, rotation, skew, translation and perspective inverses.
    const std::array<std::array<Scalar, 9>, 5> maps{{
        {Scalar(0.5), 0, 0, 0, Scalar(0.75), 0, 0, 0, 1},
        {0, -1, 1, 1, 0, 0, 0, 0, 1},
        {1, Scalar(0.2), 0, Scalar(-0.1), 1, 0, 0, 0, 1},
        {1, 0, Scalar(0.1), 0, 1, Scalar(0.2), 0, 0, 1},
        {1, Scalar(0.1), 0, 0, 1, 0, Scalar(0.2), Scalar(0.1), 1},
    }};
    // A 3x3-pixel atlas (11x11 lattice) whose red channel is the affine ramp
    // 0.1 + 0.05 x + 0.1 y in source pixels, which the quintic reproduces.
    std::vector<Scalar> atlas(11 * 11 * 4);
    for (std::size_t row = 0; row < 11; ++row) {
        for (std::size_t column = 0; column < 11; ++column) {
            const std::size_t offset = (row * 11 + column) * 4;
            const Scalar ramp_x = Scalar(0.05) * Scalar(column) / Scalar(5);
            const Scalar ramp_y = Scalar(0.1) * Scalar(row) / Scalar(5);
            atlas[offset] = Scalar(0.1) + ramp_x + ramp_y;
            atlas[offset + 1] = Scalar(0.2);
            atlas[offset + 2] = Scalar(0.3);
            atlas[offset + 3] = Scalar(0.8);
        }
    }
    const stx_conv::kernels::TensorAtlasView<Scalar> view{atlas, 3, 3, 44};
    std::array<Scalar, n * 4> rgba{};
    for (const std::array<Scalar, 9>& h : maps) {
        require(stx_conv::kernels::map_points<Scalar>(h, x, y, source_x, source_y) == stx_conv::Status::ok, "map failed");
        require(stx_conv::kernels::sample_rgba<Scalar>(view, source_x, source_y, rgba) == stx_conv::Status::ok, "atlas sampling failed");
        for (std::size_t p = 0; p < n; ++p) {
            const long double lx = static_cast<long double>(x[p]);
            const long double ly = static_cast<long double>(y[p]);
            const long double denominator = static_cast<long double>(h[6]) * lx + static_cast<long double>(h[7]) * ly + h[8];
            const long double px = (static_cast<long double>(h[0]) * lx + static_cast<long double>(h[1]) * ly + h[2]) / denominator;
            const long double py = (static_cast<long double>(h[3]) * lx + static_cast<long double>(h[4]) * ly + h[5]) / denominator;
            const long double map_error = std::abs(static_cast<long double>(source_x[p]) - px);
            require(map_error < static_cast<long double>(tolerance), "mapped x inaccurate");
            // Below 0 the sample clamps to the edge pixel.
            const long double expected = 0.1L + 0.05L * std::max(0.0L, px) + 0.1L * std::max(0.0L, py);
            const long double ramp_error = std::abs(static_cast<long double>(rgba[p * 4]) - expected);
            require(ramp_error < static_cast<long double>(tolerance), "transformed ramp inaccurate");
        }
    }
    source_x.fill(Scalar(-1));
    source_y.fill(Scalar(1));
    require(stx_conv::kernels::sample_rgba<Scalar>(view, source_x, source_y, rgba) == stx_conv::Status::ok, "exterior sampling failed");
    for (const Scalar value : rgba) { require(value == Scalar(0), "exterior not transparent"); }
    const std::array<Scalar, 9> horizon{1, 0, 0, 0, 1, 0, 0, 0, 0};
    source_x.fill(Scalar(17));
    require(stx_conv::kernels::map_points<Scalar>(horizon, x, y, source_x, source_y) == stx_conv::Status::invalid_shape,
            "a horizon was accepted");
    for (const Scalar value : source_x) { require(value == Scalar(17), "a refused mapping changed the output"); }
    require(stx_conv::kernels::map_points<Scalar>(maps[0], x, y, x, source_y) == stx_conv::Status::overlap,
            "aliased mapping accepted");
    const std::array<Scalar, 9> huge{1, 0, 0, 0, 1, 0, std::numeric_limits<Scalar>::max(), 0, 1};
    const std::array<Scalar, 1> large_x{Scalar(2)};
    const std::array<Scalar, 1> zero_y{Scalar(0)};
    std::array<Scalar, 1> protected_x{Scalar(19)};
    std::array<Scalar, 1> protected_y{Scalar(23)};
    const stx_conv::Status overflow = stx_conv::kernels::map_points<Scalar>(huge, large_x, zero_y, protected_x, protected_y);
    require(overflow == stx_conv::Status::numerical_failure, "an overflowed denominator was accepted");
    require(protected_x[0] == Scalar(19) && protected_y[0] == Scalar(23), "an overflow changed the output");
}

template<class Scalar>
void bubble_toggle() {
    constexpr std::size_t sw = 17;
    constexpr std::size_t sh = 13;
    constexpr std::size_t tw = 25;
    constexpr std::size_t th = 19;
    std::vector<Scalar> input(sw * sh);
    std::vector<Scalar> enabled(tw * th);
    std::vector<Scalar> disabled(tw * th);
    std::vector<Scalar> again(tw * th);
    for (std::size_t index = 0; index < input.size(); ++index) {
        const double column = static_cast<double>(index % sw);
        const double row = static_cast<double>(index / sw);
        input[index] = Scalar(0.5 + 0.3 * std::sin(column * 0.7) * std::cos(row * 0.9));
    }
    stx_conv::stream::Workspace<Scalar> workspace(sw, sh, tw, th, 1, stx_conv::Sampling::nodal);
    require(workspace.is_bubble_enabled(), "bubble correction not enabled by default");
    require(stx_conv::stream::resize<Scalar>(workspace, {input, sw}, {enabled, tw}) == stx_conv::Status::ok, "default resize failed");
    workspace.set_bubble_enabled(false);
    require(stx_conv::stream::resize<Scalar>(workspace, {input, sw}, {disabled, tw}) == stx_conv::Status::ok, "disabled resize failed");
    require(enabled != disabled, "the toggle has no effect on non-midpoint targets");
    workspace.set_bubble_enabled(true);
    require(stx_conv::stream::resize<Scalar>(workspace, {input, sw}, {again, tw}) == stx_conv::Status::ok, "re-enabled resize failed");
    require(enabled == again, "the toggle is not repeatable");
}

} // namespace

int main() {
    try {
        leaf_kernels<float>();
        leaf_kernels<double>();
        tensor_patches<float>();
        tensor_patches<double>();
        mapping_and_atlas<float>();
        mapping_and_atlas<double>();
        bubble_toggle<float>();
        bubble_toggle<double>();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::printf("kernels and transforms passed: %s\n", stx_conv::kernels::backend());
    return 0;
}
