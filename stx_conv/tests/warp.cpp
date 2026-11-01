// stx_conv::kernels::warp_rgba against its kept reference, map_points
// followed by sample_rgba at every target pixel: equal at every channel of
// every pixel (== , so a zero's sign is free) over rotations (including the
// exact 0, 90 and 180 degrees and the computed 270 and -90, whose samples
// fall on grid lines and nodes), scales (including the streaming maps of at
// least half a source cell per target pixel), skews, translations and perspective maps, partly or wholly off the source,
// with lane-group tails in the target width, both projections and both
// precisions. Also the refusals, each of which leaves the output unchanged,
// row padding, and the empty target.
#include <stx_conv/kernels.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {

void require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}

constexpr std::size_t source_width = 9;
constexpr std::size_t source_height = 7;
constexpr std::size_t lattice_width = 5 * (source_width - 1) + 1;
constexpr std::size_t lattice_height = 5 * (source_height - 1) + 1;
// Two padding pixels after every lattice row.
constexpr std::size_t lattice_row_stride = 4 * lattice_width + 8;
constexpr std::size_t target_width = 37;
constexpr std::size_t target_height = 29;
// One padding pixel after every target row.
constexpr std::size_t target_row_stride = 4 * target_width + 4;
constexpr double padding_value = -7.0;

// Deterministic controls in [-0.1, 1.1], so both projections clamp.
template<class Scalar>
std::vector<Scalar> make_atlas() {
    std::vector<Scalar> controls(lattice_height * lattice_row_stride, Scalar(padding_value));
    std::uint64_t state = 0x5DEECE66Dull;
    for (std::size_t row = 0; row < lattice_height; ++row) {
        for (std::size_t element = 0; element < 4 * lattice_width; ++element) {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            const double unit = static_cast<double>(state >> 11) * (1.0 / 9007199254740992.0);
            controls[row * lattice_row_stride + element] = static_cast<Scalar>(-0.1 + 1.2 * unit);
        }
    }
    return controls;
}

// Rotation by `degrees` about the target centre onto the source centre,
// scaled by `scale` (source pixels per target pixel), then shifted.
template<class Scalar>
std::array<Scalar, 9> rotation(double degrees, double scale, double shift_x, double shift_y) {
    const double angle = degrees * std::numbers::pi / 180.0;
    const double cosine = degrees == 90.0 ? 0.0 : (degrees == 180.0 ? -1.0 : std::cos(angle));
    const double sine = degrees == 90.0 ? 1.0 : (degrees == 180.0 ? 0.0 : std::sin(angle));
    const double target_x = 0.5 * static_cast<double>(target_width - 1);
    const double target_y = 0.5 * static_cast<double>(target_height - 1);
    const double centre_x = 0.5 * static_cast<double>(source_width - 1) + shift_x;
    const double centre_y = 0.5 * static_cast<double>(source_height - 1) + shift_y;
    const double a = scale * cosine;
    const double b = scale * sine;
    const std::array<Scalar, 9> h{
        static_cast<Scalar>(a), static_cast<Scalar>(b), static_cast<Scalar>(centre_x - a * target_x - b * target_y),
        static_cast<Scalar>(-b), static_cast<Scalar>(a), static_cast<Scalar>(centre_y + b * target_x - a * target_y),
        Scalar(0), Scalar(0), Scalar(1),
    };
    return h;
}

template<class Scalar>
std::vector<std::array<Scalar, 9>> make_maps() {
    std::vector<std::array<Scalar, 9>> maps;
    // 270 and -90 go through std::cos and std::sin: a tiny nonzero cosine.
    const std::array<double, 11> angles{0.0, 90.0, 180.0, 270.0, -90.0, 17.0, -33.0, 45.0, 123.0, 271.0, 3.5};
    for (const double angle : angles) { maps.push_back(rotation<Scalar>(angle, 0.25, 0.0, 0.0)); }
    maps.push_back(rotation<Scalar>(0.0, 0.2, 0.0, 0.0));
    maps.push_back(rotation<Scalar>(29.0, 0.6, 1.3, -0.7));
    maps.push_back(rotation<Scalar>(-71.0, 0.11, -2.9, 1.1));
    maps.push_back(rotation<Scalar>(8.0, 0.4, 30.0, 0.0));
    // At least half a source cell per target pixel: the streaming path,
    // which prefetches cells ahead (also past a row's end).
    maps.push_back(rotation<Scalar>(17.0, 1.0, 0.0, 0.0));
    maps.push_back(rotation<Scalar>(-33.0, 1.6, 0.4, -0.3));
    maps.push_back(rotation<Scalar>(90.0, 1.0, 0.0, 0.0));
    maps.push_back(rotation<Scalar>(0.0, 0.70710678118654752, 0.0, 0.0));
    // Skew and anisotropic stretch.
    maps.push_back({Scalar(0.21), Scalar(0.07), Scalar(-0.4), Scalar(-0.05), Scalar(0.31), Scalar(0.2), 0, 0, 1});
    // Perspective tilts: mild, strong, and partly off the source.
    maps.push_back({Scalar(0.25), 0, Scalar(-0.3), 0, Scalar(0.25), Scalar(-0.2), Scalar(0.004), Scalar(-0.003), 1});
    maps.push_back({Scalar(0.3), Scalar(0.05), 0, Scalar(-0.02), Scalar(0.3), 0, Scalar(0.02), Scalar(0.015), 1});
    maps.push_back({Scalar(0.45), Scalar(-0.1), Scalar(-2), Scalar(0.1), Scalar(0.4), Scalar(-1.5), Scalar(-0.01),
                    Scalar(0.012), Scalar(1.1)});
    // At about one source pixel per target pixel, partly off the source:
    // rows with certain zero, unmasked and masked columns.
    maps.push_back({Scalar(0.9), Scalar(0.1), Scalar(-3), Scalar(-0.05), Scalar(0.8), Scalar(-2), Scalar(0.01),
                    Scalar(0.012), Scalar(1)});
    // A denominator that changes sign between pixel centres (d = x - 10.5):
    // no bound holds, so validation checks every pixel and rows run masked.
    maps.push_back({Scalar(0.5), 0, 0, 0, Scalar(0.5), 0, Scalar(1), 0, Scalar(-10.5)});
    return maps;
}

// The kept reference: map_points and sample_rgba, one target row at a time.
template<class Scalar>
void reference_warp(stx_conv::kernels::TensorAtlasView<Scalar> atlas, const std::array<Scalar, 9>& h,
                    stx_conv::kernels::ColorProjection projection, std::vector<Scalar>& output) {
    std::vector<Scalar> x(target_width);
    std::vector<Scalar> y(target_width);
    std::vector<Scalar> source_x(target_width);
    std::vector<Scalar> source_y(target_width);
    std::vector<Scalar> row_rgba(4 * target_width);
    for (std::size_t row = 0; row < target_height; ++row) {
        for (std::size_t column = 0; column < target_width; ++column) {
            x[column] = static_cast<Scalar>(column);
            y[column] = static_cast<Scalar>(row);
        }
        const stx_conv::Status mapped = stx_conv::kernels::map_points<Scalar>(h, x, y, source_x, source_y);
        require(mapped == stx_conv::Status::ok, "reference mapping failed");
        const stx_conv::Status sampled = stx_conv::kernels::sample_rgba<Scalar>(atlas, source_x, source_y, row_rgba,
                                                                                projection);
        require(sampled == stx_conv::Status::ok, "reference sampling failed");
        for (std::size_t element = 0; element < 4 * target_width; ++element) {
            output[row * target_row_stride + element] = row_rgba[element];
        }
    }
}

template<class Scalar>
void check_equivalence() {
    const std::vector<Scalar> controls = make_atlas<Scalar>();
    const stx_conv::kernels::TensorAtlasView<Scalar> atlas{controls, source_width, source_height, lattice_row_stride};
    const std::array<stx_conv::kernels::ColorProjection, 2> projections{
        stx_conv::kernels::ColorProjection::none, stx_conv::kernels::ColorProjection::premultiplied};
    const std::vector<std::array<Scalar, 9>> maps = make_maps<Scalar>();
    std::vector<Scalar> expected(target_height * target_row_stride, Scalar(padding_value));
    std::vector<Scalar> actual(target_height * target_row_stride, Scalar(padding_value));
    std::size_t exterior = 0;
    std::size_t compared = 0;
    for (const std::array<Scalar, 9>& h : maps) {
        for (const stx_conv::kernels::ColorProjection projection : projections) {
            reference_warp(atlas, h, projection, expected);
            const stx_conv::Status status = stx_conv::kernels::warp_rgba<Scalar>(
                atlas, h, target_width, target_height, actual, target_row_stride, projection);
            require(status == stx_conv::Status::ok, "warp failed");
            for (std::size_t index = 0; index < actual.size(); ++index) {
                require(!std::isnan(actual[index]), "warp produced NaN");
                require(actual[index] == expected[index], "warp differs from map_points + sample_rgba");
            }
            for (std::size_t row = 0; row < target_height; ++row) {
                const Scalar padding = actual[row * target_row_stride + 4 * target_width];
                require(padding == Scalar(padding_value), "warp wrote row padding");
                for (std::size_t column = 0; column < target_width; ++column) {
                    const Scalar alpha = expected[row * target_row_stride + 4 * column + 3];
                    if (alpha == Scalar(0)) { ++exterior; }
                    ++compared;
                }
            }
        }
    }
    // Every map family must leave some pixels outside and most inside.
    require(exterior > 0 && exterior < compared, "the maps do not exercise both footprint sides");
    std::printf("warp f%zu: %zu pixels equal (%zu transparent)\n", 8 * sizeof(Scalar), compared, exterior);
}

template<class Scalar>
void check_refusals() {
    std::vector<Scalar> controls = make_atlas<Scalar>();
    const stx_conv::kernels::TensorAtlasView<Scalar> atlas{controls, source_width, source_height, lattice_row_stride};
    std::vector<Scalar> output(target_height * target_row_stride, Scalar(padding_value));
    const std::array<Scalar, 9> identity{Scalar(0.25), 0, 0, 0, Scalar(0.25), 0, 0, 0, 1};
    // The denominator 1 - x/10 vanishes at column 10.
    const std::array<Scalar, 9> horizon{1, 0, 0, 0, 1, 0, Scalar(-0.1), 0, 1};
    const std::array<Scalar, 9> nonfinite{std::numeric_limits<Scalar>::infinity(), 0, 0, 0, 1, 0, 0, 0, 1};
    const Scalar huge = std::numeric_limits<Scalar>::max() / Scalar(4);
    const std::array<Scalar, 9> overflow{huge, 0, 0, 0, 1, 0, 0, 0, 1};
    const std::array<Scalar, 9> projective_overflow{huge, 0, 0, 0, 1, 0, Scalar(1e-30), 0, 1};
    const stx_conv::kernels::ColorProjection premultiplied = stx_conv::kernels::ColorProjection::premultiplied;
    const stx_conv::kernels::ColorProjection invalid = static_cast<stx_conv::kernels::ColorProjection>(7);
    const stx_conv::kernels::TensorAtlasView<Scalar> narrow{controls, 1, source_height, lattice_row_stride};
    // One element short of the last row's pixels (its padding is not needed).
    const std::span<Scalar> short_output(output.data(), (target_height - 1) * target_row_stride + 4 * target_width - 1);
    const std::span<Scalar> over_controls(controls);
    struct Refusal {
        stx_conv::Status status = stx_conv::Status::ok;
        const char* message = "";
    };
    const std::array<Refusal, 9> refusals{{
        {stx_conv::kernels::warp_rgba<Scalar>(atlas, identity, target_width, target_height, output, target_row_stride,
                                              invalid),
         "invalid projection"},
        {stx_conv::kernels::warp_rgba<Scalar>(narrow, identity, target_width, target_height, output,
                                              target_row_stride, premultiplied),
         "narrow atlas"},
        {stx_conv::kernels::warp_rgba<Scalar>(atlas, identity, std::size_t(1) << 21, 1, output, target_row_stride,
                                              premultiplied),
         "target extent"},
        {stx_conv::kernels::warp_rgba<Scalar>(atlas, identity, target_width, target_height, short_output,
                                              target_row_stride, premultiplied),
         "short output"},
        {stx_conv::kernels::warp_rgba<Scalar>(atlas, identity, target_width, target_height, output, 4 * target_width - 1,
                                              premultiplied),
         "short row stride"},
        {stx_conv::kernels::warp_rgba<Scalar>(atlas, horizon, target_width, target_height, output, target_row_stride,
                                              premultiplied),
         "horizon"},
        {stx_conv::kernels::warp_rgba<Scalar>(atlas, nonfinite, target_width, target_height, output,
                                              target_row_stride, premultiplied),
         "nonfinite map"},
        {stx_conv::kernels::warp_rgba<Scalar>(atlas, overflow, target_width, target_height, output, target_row_stride,
                                              premultiplied),
         "affine overflow"},
        {stx_conv::kernels::warp_rgba<Scalar>(atlas, projective_overflow, target_width, target_height, output,
                                              target_row_stride, premultiplied),
         "projective overflow"},
    }};
    const std::array<stx_conv::Status, 9> expected{
        stx_conv::Status::invalid_shape,        stx_conv::Status::invalid_shape,     stx_conv::Status::invalid_shape,
        stx_conv::Status::insufficient_storage, stx_conv::Status::insufficient_storage, stx_conv::Status::invalid_shape,
        stx_conv::Status::nonfinite_input,      stx_conv::Status::numerical_failure, stx_conv::Status::numerical_failure,
    };
    for (std::size_t index = 0; index < refusals.size(); ++index) {
        require(refusals[index].status == expected[index], refusals[index].message);
    }
    const stx_conv::Status aliased = stx_conv::kernels::warp_rgba<Scalar>(atlas, identity, 2, 2, over_controls,
                                                                          lattice_row_stride, premultiplied);
    require(aliased == stx_conv::Status::overlap, "output over the controls accepted");
    for (const Scalar value : output) { require(value == Scalar(padding_value), "a refused warp wrote output"); }
    const stx_conv::Status empty = stx_conv::kernels::warp_rgba<Scalar>(atlas, identity, 0, target_height, {}, 0,
                                                                        premultiplied);
    require(empty == stx_conv::Status::ok, "empty target refused");
}

} // namespace

int main() {
    try {
        check_equivalence<float>();
        check_equivalence<double>();
        check_refusals<float>();
        check_refusals<double>();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::printf("warp passed: %s\n", stx_conv::kernels::backend());
    return 0;
}
