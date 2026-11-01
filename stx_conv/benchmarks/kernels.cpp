// Per-position cost of each public kernel batch over 65,536 positions, both
// precisions. Each kernel is called once to warm, then five samples of eight
// repeats; the median sample is printed as CSV (backend, bits, kernel,
// positions, ns_per_position). Timing evidence for the measuring host only;
// no correctness claim (tests/ cover that). Built only with
// STX_CONV_BUILD_KERNEL_BENCHMARKS.
#include <stx_conv/kernels.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

enum class Task { jets, raw, capacity, admission, bubble, synthesis, tensor, mapping };

constexpr std::size_t positions = 65536;
constexpr std::size_t samples = 5;
constexpr std::size_t repeats = 8;

// All streams for every task, allocated once before any measurement. Planar
// stores (raw, admitted) hold five planes of `positions` elements.
template<class Scalar>
struct Work {
    std::array<std::vector<Scalar>, 5> source;
    std::array<std::vector<Scalar>, 36> controls;
    std::vector<Scalar> fifths;
    std::vector<Scalar> two_fifths;
    std::vector<Scalar> twentieths;
    std::vector<Scalar> raw;
    std::vector<Scalar> admitted;
    std::vector<Scalar> mass;
    std::vector<Scalar> threshold;
    std::vector<Scalar> amplitude;
    std::vector<Scalar> value;
    std::vector<Scalar> left_delta;
    std::vector<Scalar> left_last;
    std::vector<Scalar> u;
    std::vector<Scalar> v;
    std::vector<Scalar> source_x;
    std::vector<Scalar> source_y;
    std::array<const Scalar*, 5> sources{};
    std::array<const Scalar*, 5> raw_views{};
    std::array<Scalar*, 5> raw_outputs{};
    std::array<Scalar*, 5> admitted_outputs{};
    std::array<const Scalar*, 8> terms{};
    std::array<const Scalar*, 36> tensor_views{};
    std::array<Scalar, 5> weights{Scalar(31) / 32, Scalar(26) / 32, Scalar(16) / 32, Scalar(6) / 32, Scalar(1) / 32};
    std::array<Scalar, 9> map{1, Scalar(0.2), 0, 0, 1, 0, Scalar(0.001), 0, 1};
};

template<class Scalar>
void fill_work(Work<Scalar>& work) {
    work.fifths.assign(positions, Scalar(0));
    work.two_fifths.assign(positions, Scalar(0));
    work.twentieths.assign(positions, Scalar(0));
    work.raw.assign(positions * 5, Scalar(0.002));
    work.admitted.assign(positions * 5, Scalar(0.002));
    work.mass.assign(positions, Scalar(0.005));
    work.threshold.assign(positions, Scalar(0));
    work.amplitude.assign(positions, Scalar(0));
    work.value.assign(positions, Scalar(0));
    work.left_delta.assign(positions, Scalar(0.01));
    work.left_last.assign(positions, Scalar(0.002));
    work.u.assign(positions, Scalar(0));
    work.v.assign(positions, Scalar(0));
    work.source_x.assign(positions, Scalar(0));
    work.source_y.assign(positions, Scalar(0));
    for (std::size_t slot = 0; slot < 5; ++slot) {
        work.source[slot].resize(positions);
        for (std::size_t index = 0; index < positions; ++index) {
            const double wave = 0.05 * std::sin(static_cast<double>(index) * 0.01);
            work.source[slot][index] = Scalar(0.5 + 0.01 * static_cast<double>(slot) + wave);
        }
        work.sources[slot] = work.source[slot].data();
        work.raw_views[slot] = work.raw.data() + slot * positions;
        work.raw_outputs[slot] = work.raw.data() + slot * positions;
        work.admitted_outputs[slot] = work.admitted.data() + slot * positions;
    }
    work.terms = {work.sources[2], work.sources[3], work.fifths.data(), work.two_fifths.data(),
                  work.twentieths.data(), work.fifths.data(), work.two_fifths.data(), work.twentieths.data()};
    for (std::size_t control = 0; control < 36; ++control) {
        work.controls[control].resize(positions);
        for (std::size_t index = 0; index < positions; ++index) {
            const double phase = static_cast<double>(control + index) * 0.03;
            work.controls[control][index] = Scalar(0.5 + 0.2 * std::sin(phase));
        }
        work.tensor_views[control] = work.controls[control].data();
    }
    for (std::size_t index = 0; index < positions; ++index) {
        work.u[index] = Scalar(index % 101) / Scalar(100);
        work.v[index] = Scalar(index % 79) / Scalar(78);
    }
}

template<Task task, class Scalar>
void call(Work<Scalar>& work) {
    if constexpr (task == Task::jets) {
        stx_conv::kernels::jet_terms<Scalar>(work.sources.data(), work.fifths.data(), work.two_fifths.data(),
                                             work.twentieths.data(), positions);
    } else if constexpr (task == Task::raw) {
        stx_conv::kernels::raw_currents<Scalar>(work.terms.data(), work.raw_outputs.data(), positions);
    } else if constexpr (task == Task::capacity) {
        stx_conv::kernels::capacity<Scalar>(work.sources.data(), work.mass.data(), work.threshold.data(), positions);
    } else if constexpr (task == Task::admission) {
        stx_conv::kernels::admit_cell<Scalar>(work.raw_views.data(), work.admitted_outputs.data(), work.sources[2],
                                              work.sources[3], work.sources[4], work.raw_views[0],
                                              work.left_delta.data(), work.left_last.data(), positions);
    } else if constexpr (task == Task::bubble) {
        stx_conv::kernels::bubble<Scalar>(work.raw.data(), work.admitted.data(), work.amplitude.data(), positions,
                                          positions, true);
    } else if constexpr (task == Task::synthesis) {
        stx_conv::kernels::synthesize<Scalar>(work.sources[2], work.admitted.data(), positions, work.weights,
                                              Scalar(1), work.amplitude.data(), Scalar(0.001), work.value.data(),
                                              positions, false);
    } else if constexpr (task == Task::tensor) {
        stx_conv::kernels::tensor_quintic<Scalar>(work.tensor_views.data(), work.u.data(), work.v.data(),
                                                  work.value.data(), positions);
    } else {
        const stx_conv::Status status =
            stx_conv::kernels::map_points<Scalar>(work.map, work.u, work.v, work.source_x, work.source_y);
        if (status != stx_conv::Status::ok) { std::abort(); }
    }
}

template<Task task, class Scalar>
void measure(Work<Scalar>& work, const char* name) {
    call<task>(work);
    std::array<double, samples> times{};
    for (std::size_t sample = 0; sample < samples; ++sample) {
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        for (std::size_t repeat = 0; repeat < repeats; ++repeat) { call<task>(work); }
        const std::chrono::steady_clock::time_point stop = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(stop - start).count();
        times[sample] = seconds * 1e9 / static_cast<double>(repeats * positions);
    }
    std::sort(times.begin(), times.end());
    std::printf("%s,%zu,%s,%zu,%.6f\n", stx_conv::kernels::backend(), sizeof(Scalar) * 8, name, positions,
                times[samples / 2]);
}

template<class Scalar>
void run() {
    Work<Scalar> work;
    fill_work(work);
    measure<Task::jets>(work, "jets");
    measure<Task::raw>(work, "raw");
    measure<Task::capacity>(work, "capacity");
    measure<Task::admission>(work, "admission");
    measure<Task::bubble>(work, "bubble");
    measure<Task::synthesis>(work, "synthesis");
    measure<Task::tensor>(work, "tensor");
    measure<Task::mapping>(work, "mapping_checked");
    // Keeps the results observable so no batch is removed as dead code.
    const double checksum =
        static_cast<double>(work.value[133]) + static_cast<double>(work.source_x[177]) + static_cast<double>(work.threshold[11]);
    if (!std::isfinite(checksum)) { std::abort(); }
    std::fprintf(stderr, "f%zu checksum %.17g\n", sizeof(Scalar) * 8, checksum);
}

} // namespace

int main() {
    std::puts("backend,bits,kernel,positions,ns_per_position");
    run<float>();
    run<double>();
    return 0;
}
