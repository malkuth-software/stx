// The lane-loop kernel table the process's SIMD level selects
// (detail/conv_lane_kernels.hpp, stx_auxlib/simd.hpp).
#include "detail/conv_lane_kernels.hpp"
#include "detail/conv_simd_backend.hpp"

#if defined(STX_CONV_SIMD_LAYER)
#include <stx_auxlib/simd.hpp>
#endif

namespace stx_conv::kernels::detail {

namespace {

// The variant for the process's level. A forced scalar level runs the scalar
// tables; avx2_fma runs avx2 where this build has it.
enum class LaneVariant { scalar, v128, avx2 };

LaneVariant select_variant() noexcept {
#if defined(STX_CONV_SIMD_LAYER)
    const stx_auxlib::simd::Level level = stx_auxlib::simd::selected_level();
#if defined(STX_CONV_LANE_DISPATCH_AVX2)
    if (level == stx_auxlib::simd::Level::avx2_fma) {
        return LaneVariant::avx2;
    }
#endif
    if (level == stx_auxlib::simd::Level::scalar) {
        return LaneVariant::scalar;
    }
#endif
    // Without the layer (a standalone build) the 128-bit variant runs alone.
    return LaneVariant::v128;
}

// Chosen at the first call, thread-safely, and kept for the process: later
// calls read the cached value and never consult the CPU again.
LaneVariant selected_variant() noexcept {
    static const LaneVariant variant = select_variant();
    return variant;
}

const LaneKernels<float>& select_f32() noexcept {
    const LaneVariant variant = selected_variant();
#if defined(STX_CONV_LANE_DISPATCH_AVX2)
    if (variant == LaneVariant::avx2) {
        return avx2_lanes::table_f32();
    }
#endif
    if (variant == LaneVariant::scalar) {
        return scalar_lanes::table_f32();
    }
    return v128_lanes::table_f32();
}

const LaneKernels<double>& select_f64() noexcept {
    const LaneVariant variant = selected_variant();
#if defined(STX_CONV_LANE_DISPATCH_AVX2)
    if (variant == LaneVariant::avx2) {
        return avx2_lanes::table_f64();
    }
#endif
    if (variant == LaneVariant::scalar) {
        return scalar_lanes::table_f64();
    }
    return v128_lanes::table_f64();
}

} // namespace

template<>
const LaneKernels<float>& lane_kernels<float>() noexcept {
    static const LaneKernels<float>& table = select_f32();
    return table;
}

template<>
const LaneKernels<double>& lane_kernels<double>() noexcept {
    static const LaneKernels<double>& table = select_f64();
    return table;
}

const char* lane_variant_name() noexcept {
    const LaneVariant variant = selected_variant();
    switch (variant) {
    case LaneVariant::scalar:
        return "scalar";
    case LaneVariant::v128:
        // The 128-bit variant is whatever the native backend is in this build.
        return native_backend_name();
    case LaneVariant::avx2:
        return "avx2-256";
    }
    return native_backend_name();
}

} // namespace stx_conv::kernels::detail
