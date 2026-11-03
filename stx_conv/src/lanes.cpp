// One variant's table of lane-loop kernels (detail/conv_lane_kernels.hpp).
// CMakeLists.txt compiles this file once per variant, defining
//   STX_CONV_ISA           the variant's inline namespace (scalar, v128, avx2),
//   STX_CONV_LANES_TABLE   the namespace of its table (scalar_lanes, ...),
// and STX_CONV_SCALAR_LANES or STX_CONV_AVX2_LANES to pick its lane group.
//
// The avx2 variant is compiled for the avx2 target by attribute, applied
// after every #include so that the standard library's inline functions stay
// baseline code; only this variant's own namespace holds AVX instructions.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <optional>
#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#endif
#if defined(STX_CONV_AVX2_LANES)
#include <immintrin.h>
#endif

#include "detail/conv_lane_kernels.hpp"

#if !defined(STX_CONV_ISA) || !defined(STX_CONV_LANES_TABLE)
#error "src/lanes.cpp compiles only with STX_CONV_ISA and STX_CONV_LANES_TABLE defined (CMakeLists.txt)"
#endif

#if defined(STX_CONV_AVX2_LANES)
#if defined(__clang__)
#pragma clang attribute push(__attribute__((target("avx2"))), apply_to = function)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC target("avx2")
#else
#error "the avx2 lane variant needs Clang or GCC"
#endif
#endif

#include "detail/conv_admission_kernel.hpp"
#include "detail/conv_bubble_kernel.hpp"
#include "detail/conv_jet_kernel.hpp"
#include "detail/conv_mapping_kernel.hpp"
#include "detail/conv_raw_current_kernel.hpp"
#include "detail/conv_synthesis_kernel.hpp"

namespace stx_conv::kernels::detail::STX_CONV_LANES_TABLE {

namespace {

template<class Scalar>
constexpr LaneKernels<Scalar> make_table() noexcept {
    LaneKernels<Scalar> table;
    table.jet = &jet_lanes<Scalar>;
    table.raw_current = &raw_current_lanes<Scalar>;
    table.admit = &admit_lanes<Scalar>;
    table.bubble_interior = &bubble_lanes<Scalar, true>;
    table.bubble_edge = &bubble_lanes<Scalar, false>;
    table.synthesis[0][0] = &synthesis_lanes<Scalar, false, false>;
    table.synthesis[0][1] = &synthesis_lanes<Scalar, false, true>;
    table.synthesis[1][0] = &synthesis_lanes<Scalar, true, false>;
    table.synthesis[1][1] = &synthesis_lanes<Scalar, true, true>;
    table.mapping_affine = &mapping_lanes<Scalar, true>;
    table.mapping_projective = &mapping_lanes<Scalar, false>;
    return table;
}

constexpr LaneKernels<float> table_float = make_table<float>();
constexpr LaneKernels<double> table_double = make_table<double>();

} // namespace

const LaneKernels<float>& table_f32() noexcept { return table_float; }
const LaneKernels<double>& table_f64() noexcept { return table_double; }

} // namespace stx_conv::kernels::detail::STX_CONV_LANES_TABLE

#if defined(STX_CONV_AVX2_LANES)
#if defined(__clang__)
#pragma clang attribute pop
#else
#pragma GCC pop_options
#endif
#endif
