#pragma once
// The lane-loop kernels as one table per SIMD variant, and the table the
// process's SIMD level selects (stx_auxlib/simd.hpp).
//
// src/lanes.cpp compiles once per variant -- scalar, v128 (SSE2 or NEON) and,
// on x86-64 with Clang or GCC, avx2 -- each in its own inline namespace
// (conv_simd_backend.hpp's STX_CONV_ISA) and each filling one table of the
// instantiations below. lane_kernels<Scalar>() returns the selected variant's
// table: chosen at its first call, kept for the process. Every variant gives
// the same bits: the backends are the correctly rounded IEEE operations,
// lane for lane, with contraction off.
#include <array>
#include <cstddef>

namespace stx_conv::kernels::detail {

template<class Scalar>
struct LaneKernels final {
    void (*jet)(const Scalar* const source[5], Scalar* fifths, Scalar* two_fifths, Scalar* twentieths,
                std::size_t first, std::size_t last) noexcept = nullptr;
    void (*raw_current)(const Scalar* const input[8], Scalar* const output[5], std::size_t first,
                        std::size_t last) noexcept = nullptr;
    void (*admit)(const Scalar* const raw[5], Scalar* const output[5], const Scalar* y0, const Scalar* y1,
                  const Scalar* y2, const Scalar* next, Scalar* left_delta, Scalar* left_last,
                  std::size_t count) noexcept = nullptr;
    // bubble_lanes<Scalar, true> and <Scalar, false>.
    void (*bubble_interior)(const Scalar* raw, const Scalar* admitted, Scalar* output, std::size_t stride,
                            std::size_t first, std::size_t last) noexcept = nullptr;
    void (*bubble_edge)(const Scalar* raw, const Scalar* admitted, Scalar* output, std::size_t stride,
                        std::size_t first, std::size_t last) noexcept = nullptr;
    // synthesis_lanes<Scalar, Bubble, Accumulate>, indexed [Bubble][Accumulate].
    void (*synthesis[2][2])(const Scalar* anchors, const Scalar* currents, std::size_t stride,
                            const std::array<Scalar, 5>& weights, Scalar anchor_weight, const Scalar* bubble,
                            Scalar bubble_weight, Scalar* output, std::size_t first,
                            std::size_t last) noexcept = {};
    // mapping_lanes<Scalar, true> (affine) and <Scalar, false> (projective).
    void (*mapping_affine)(const std::array<Scalar, 9>& h, const Scalar* x, const Scalar* y, Scalar* source_x,
                           Scalar* source_y, std::size_t count) noexcept = nullptr;
    void (*mapping_projective)(const std::array<Scalar, 9>& h, const Scalar* x, const Scalar* y,
                               Scalar* source_x, Scalar* source_y, std::size_t count) noexcept = nullptr;
};

// Each variant's tables, defined by that variant's compilation of
// src/lanes.cpp.
namespace scalar_lanes {
const LaneKernels<float>& table_f32() noexcept;
const LaneKernels<double>& table_f64() noexcept;
} // namespace scalar_lanes
namespace v128_lanes {
const LaneKernels<float>& table_f32() noexcept;
const LaneKernels<double>& table_f64() noexcept;
} // namespace v128_lanes
#if defined(STX_CONV_LANE_DISPATCH_AVX2)
namespace avx2_lanes {
const LaneKernels<float>& table_f32() noexcept;
const LaneKernels<double>& table_f64() noexcept;
} // namespace avx2_lanes
#endif

// The selected variant's table (src/lane_dispatch.cpp).
template<class Scalar>
const LaneKernels<Scalar>& lane_kernels() noexcept;

// The selected variant's name: "scalar", "sse2-128", "neon-128" or
// "avx2-256" (src/lane_dispatch.cpp).
const char* lane_variant_name() noexcept;

} // namespace stx_conv::kernels::detail
