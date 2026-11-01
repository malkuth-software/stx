// Instruction probes: one extern "C" symbol per kernel lane group and
// precision, so the generated code can be read and diffed by name
// (tools/kernels/inspect.sh, audit.py). Built only with
// STX_CONV_BUILD_KERNEL_PROBES; not a supported interface. extern "C"
// symbols cannot be templates, so a macro stamps the float and double sets.
#include "detail/conv_admission_kernel.hpp"
#include "detail/conv_bubble_kernel.hpp"
#include "detail/conv_jet_kernel.hpp"
#include "detail/conv_mapping_kernel.hpp"
#include "detail/conv_raw_current_kernel.hpp"
#include "detail/conv_synthesis_kernel.hpp"
#include "detail/conv_tensor_kernel.hpp"

namespace kernel = stx_conv::kernels::detail;

namespace {

template<class Scalar>
void probe_capacity(const Scalar* const input[5], const Scalar* mass, Scalar* output) noexcept {
    using Ops = kernel::NativeOps<Scalar>;
    std::array<typename Ops::Value, 5> capacities{};
    for (std::size_t slot = 0; slot < 5; ++slot) { capacities[slot] = Ops::load(input[slot]); }
    const typename Ops::Value threshold = kernel::capacity_threshold<Ops>(capacities, Ops::load(mass));
    Ops::store(output, threshold);
}

} // namespace

#define STX_CONV_PROBES(T, SUFFIX)                                                                                    \
    extern "C" void stx_patch_rgba_##SUFFIX(const T* origin, std::size_t stride, T u, T v, T* output) {               \
        kernel::patch_rgba(origin, stride, u, v, output);                                                             \
    }                                                                                                                 \
    extern "C" void stx_affine_##SUFFIX(const std::array<T, 9>& h, const T* x, const T* y, T* sx, T* sy) {            \
        kernel::mapping_group<kernel::NativeOps<T>, T, true>(h, x, y, sx, sy, 0);                                     \
    }                                                                                                                 \
    extern "C" void stx_projective_##SUFFIX(const std::array<T, 9>& h, const T* x, const T* y, T* sx, T* sy) {        \
        kernel::mapping_group<kernel::NativeOps<T>, T, false>(h, x, y, sx, sy, 0);                                    \
    }                                                                                                                 \
    extern "C" void stx_jet_##SUFFIX(const T* const input[5], T* fifths, T* two_fifths, T* twentieths) {              \
        kernel::jet_group<kernel::NativeOps<T>>(input, fifths, two_fifths, twentieths, 0);                            \
    }                                                                                                                 \
    extern "C" void stx_raw_##SUFFIX(const T* const input[8], T* const output[5]) {                                   \
        kernel::raw_current_group<kernel::NativeOps<T>>(input, output, 0);                                            \
    }                                                                                                                 \
    extern "C" void stx_capacity_##SUFFIX(const T* const input[5], const T* mass, T* output) {                        \
        probe_capacity<T>(input, mass, output);                                                                       \
    }                                                                                                                 \
    extern "C" void stx_admit_##SUFFIX(const T* const raw[5], T* const output[5], const T* y0, const T* y1,           \
                                       const T* y2, const T* next, T* left_delta, T* left_last) {                     \
        kernel::admit_group<kernel::NativeOps<T>>(raw, output, y0, y1, y2, next, left_delta, left_last, 0);           \
    }                                                                                                                 \
    extern "C" void stx_bubble_##SUFFIX(const T* raw, const T* admitted, T* output, std::size_t stride) {             \
        kernel::bubble_group<kernel::NativeOps<T>, T, true>(raw, admitted, output, stride, 0);                        \
    }                                                                                                                 \
    extern "C" void stx_tensor_##SUFFIX(const T* const controls[36], const T* u, const T* v, T* output) {             \
        kernel::tensor_group<kernel::NativeOps<T>>(controls, u, v, output, 0);                                        \
    }                                                                                                                 \
    extern "C" void stx_synthesis_##SUFFIX(const T* anchors, const T* currents, std::size_t stride,                   \
                                           const std::array<T, 5>& weights, T anchor_weight, const T* bubble,         \
                                           T bubble_weight, T* output) {                                              \
        kernel::synthesis_group<kernel::NativeOps<T>, T, true, false>(anchors, currents, stride, weights,             \
                                                                      anchor_weight, bubble, bubble_weight, output,   \
                                                                      0);                                             \
    }

STX_CONV_PROBES(float, f32)
STX_CONV_PROBES(double, f64)
#undef STX_CONV_PROBES
