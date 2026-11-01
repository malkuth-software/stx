// Per-pixel cost of warping a 256x256-pixel tensor-quintic RGBA atlas into
// a 256x256 target, by warp_rgba and by its reference (map_points then
// sample_rgba, one row at a time, buffers prepared), both precisions,
// premultiplied projection. One untimed round warms every case on both
// paths, then seven interleaved rounds are timed; the CSV line gives the median, minimum and maximum ns per target
// pixel. Timing evidence for the measuring host only (tests/warp.cpp covers
// equality). Built only with STX_CONV_BUILD_KERNEL_BENCHMARKS.
#include <stx_conv/kernels.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <vector>

namespace {

constexpr std::size_t source_size = 256;
constexpr std::size_t lattice_size = 5 * (source_size - 1) + 1;
constexpr std::size_t lattice_row_stride = 4 * lattice_size;
constexpr std::size_t target_size = 256;
constexpr std::size_t target_row_stride = 4 * target_size;
constexpr std::size_t samples = 7;

enum class Path { reference, warp };

struct MapCase {
    const char* name = "";
    std::array<double, 9> inverse{};
};

// Rotation by `degrees` and scale `scale` (source pixels per target pixel)
// about the target centre onto the source centre.
MapCase rotation(const char* name, double degrees, double scale) {
    const double angle = degrees * std::numbers::pi / 180.0;
    const double a = scale * std::cos(angle);
    const double b = scale * std::sin(angle);
    const double target_centre = 0.5 * static_cast<double>(target_size - 1);
    const double source_centre = 0.5 * static_cast<double>(source_size - 1);
    const MapCase result{name,
                         {a, b, source_centre - (a + b) * target_centre, -b, a, source_centre + (b - a) * target_centre,
                          0.0, 0.0, 1.0}};
    return result;
}

std::vector<MapCase> make_cases() {
    std::vector<MapCase> cases;
    cases.push_back(rotation("rotate_17", 17.0, 1.0));
    cases.push_back(rotation("rotate_90", 90.0, 1.0));
    // The same general contraction as rotate_17 over a sixteenth of the
    // atlas per target pixel: separates arithmetic from atlas traffic.
    cases.push_back(rotation("rotate_17_zoom_0.25", 17.0, 0.25));
    cases.push_back(rotation("rotate_0_zoom_0.5", 0.0, 0.5));
    cases.push_back(rotation("rotate_33_zoom_0.4", 33.0, 0.4));
    cases.push_back(rotation("rotate_33_shrink_1.6", 33.0, 1.6));
    // The same rotation with a negligible projective row: the masked,
    // dividing path over identical samples (isolates the affine interval).
    MapCase near_affine = rotation("rotate_17_projective_path", 17.0, 1.0);
    near_affine.inverse[6] = 1e-30;
    cases.push_back(near_affine);
    cases.push_back({"perspective_tilt", {1.0, 0.1, 0.0, 0.0, 1.1, 0.0, 0.0008, 0.0011, 1.0}});
    return cases;
}

template<class Scalar>
struct Work {
    std::vector<Scalar> controls;
    std::vector<Scalar> output;
    std::vector<Scalar> x;
    std::vector<Scalar> y;
    std::vector<Scalar> source_x;
    std::vector<Scalar> source_y;
};

template<class Scalar>
void fill_work(Work<Scalar>& work) {
    work.controls.resize(lattice_size * lattice_row_stride);
    for (std::size_t row = 0; row < lattice_size; ++row) {
        for (std::size_t element = 0; element < lattice_row_stride; ++element) {
            const double phase = 0.013 * static_cast<double>(row) + 0.007 * static_cast<double>(element);
            work.controls[row * lattice_row_stride + element] = static_cast<Scalar>(0.5 + 0.45 * std::sin(phase));
        }
    }
    work.output.assign(target_size * target_row_stride, Scalar(0));
    work.x.resize(target_size);
    work.y.assign(target_size, Scalar(0));
    work.source_x.assign(target_size, Scalar(0));
    work.source_y.assign(target_size, Scalar(0));
    for (std::size_t column = 0; column < target_size; ++column) { work.x[column] = static_cast<Scalar>(column); }
}

template<Path path, class Scalar>
void run_once(Work<Scalar>& work, const std::array<Scalar, 9>& inverse) {
    const stx_conv::kernels::TensorAtlasView<Scalar> atlas{work.controls, source_size, source_size,
                                                           lattice_row_stride};
    if constexpr (path == Path::warp) {
        const stx_conv::Status status = stx_conv::kernels::warp_rgba<Scalar>(atlas, inverse, target_size, target_size,
                                                                             work.output, target_row_stride);
        if (status != stx_conv::Status::ok) { std::abort(); }
    } else {
        for (std::size_t row = 0; row < target_size; ++row) {
            std::fill(work.y.begin(), work.y.end(), static_cast<Scalar>(row));
            const stx_conv::Status mapped =
                stx_conv::kernels::map_points<Scalar>(inverse, work.x, work.y, work.source_x, work.source_y);
            const std::span<Scalar> row_output(work.output.data() + row * target_row_stride, 4 * target_size);
            const stx_conv::Status sampled =
                stx_conv::kernels::sample_rgba<Scalar>(atlas, work.source_x, work.source_y, row_output);
            if (mapped != stx_conv::Status::ok || sampled != stx_conv::Status::ok) { std::abort(); }
        }
    }
}

template<Path path, class Scalar>
double time_once(Work<Scalar>& work, const std::array<Scalar, 9>& inverse) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    run_once<path>(work, inverse);
    const std::chrono::steady_clock::time_point stop = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(stop - start).count();
    const double result = seconds * 1e9 / static_cast<double>(target_size * target_size);
    return result;
}

void print_line(std::size_t bits, const char* map, const char* path, std::array<double, samples> times) {
    std::sort(times.begin(), times.end());
    std::printf("%s,%zu,%s,%s,%zu,%.3f,%.3f,%.3f\n", stx_conv::kernels::backend(), bits, map, path,
                target_size * target_size, times[samples / 2], times.front(), times.back());
}

// Samples are interleaved: each sample round times every case on both
// paths, so slow drift of the clock or the cache state spreads evenly.
template<class Scalar>
void run(const std::vector<MapCase>& cases) {
    Work<Scalar> work;
    fill_work(work);
    std::vector<std::array<Scalar, 9>> inverses(cases.size());
    for (std::size_t index = 0; index < cases.size(); ++index) {
        for (std::size_t element = 0; element < 9; ++element) {
            inverses[index][element] = static_cast<Scalar>(cases[index].inverse[element]);
        }
    }
    std::vector<std::array<double, samples>> reference_times(cases.size());
    std::vector<std::array<double, samples>> warp_times(cases.size());
    double checksum = 0.0;
    // Round 0 warms every case on both paths and is not recorded.
    for (std::size_t round = 0; round <= samples; ++round) {
        for (std::size_t index = 0; index < cases.size(); ++index) {
            const double reference = time_once<Path::reference>(work, inverses[index]);
            const double warp = time_once<Path::warp>(work, inverses[index]);
            checksum += static_cast<double>(work.output[target_row_stride * (target_size / 2) + 2 * target_size]);
            if (round == 0) { continue; }
            reference_times[index][round - 1] = reference;
            warp_times[index][round - 1] = warp;
        }
    }
    for (std::size_t index = 0; index < cases.size(); ++index) {
        print_line(8 * sizeof(Scalar), cases[index].name, "reference", reference_times[index]);
        print_line(8 * sizeof(Scalar), cases[index].name, "warp", warp_times[index]);
    }
    // Keeps the results observable so no path is removed as dead code.
    if (!std::isfinite(checksum)) { std::abort(); }
    std::fprintf(stderr, "f%zu checksum %.17g\n", sizeof(Scalar) * 8, checksum);
}

} // namespace

int main() {
    const std::vector<MapCase> cases = make_cases();
    std::puts("backend,bits,map,path,pixels,median_ns_per_pixel,min,max");
    run<float>(cases);
    run<double>(cases);
    return 0;
}
