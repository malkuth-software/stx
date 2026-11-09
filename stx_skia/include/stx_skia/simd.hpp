#pragma once
// The SIMD tier stx_skia's raster kernels run at, chosen once per process.
//
// The tiers follow stx_auxlib's (stx_auxlib/simd.hpp, after BFFT): a scalar
// reference, one 128-bit tier (SSE2 on x86-64, NEON on arm64, the
// architecture's baseline), and AVX2 with FMA at 256 bits. There is no AVX
// tier without AVX2, no SSE4 or SSSE3 tier and no AVX-512 tier.
//
// stx_skia compiles its raster pipeline, row blitters and swizzlers at the
// build's baseline and, on x86-64 builds of the baseline profile, a second
// time for avx2_fma; the first raster use selects one table and keeps it for
// the process. The scalar reference is a separate build (the STX_SKIA_ISA
// scalar profile, docs/ISA_PROFILES.md), not a run-time tier: a request for
// scalar runs vector128, and selected_level() says so.
//
// Requests, in order: request_level() before the first selection, then the
// environment variable STX_SIMD_LEVEL (scalar, vector128 or avx2_fma), read
// once at the first selection. A request above what the machine and the
// build can run is lowered to it; an unrecognised value is ignored.

#include <cstdint>

namespace stx_skia::simd {

// Ordered: a higher level can run every lower level's code.
enum class Level : std::uint8_t {
    scalar = 0,
    vector128 = 1,
    avx2_fma = 2,
};

// The highest level this machine can run and this build carries.
[[nodiscard]] Level hardware_level() noexcept;

// The level stx_skia's kernels use for the rest of the process. Fixed at
// the first call; SkGraphics::Init() makes that call.
[[nodiscard]] Level selected_level() noexcept;

// Asks for `level` before the first selection; a later request replaces an
// earlier one. Returns false, and changes nothing, once the level is fixed,
// or for a value outside Level. For tests and benchmarks.
bool request_level(Level level) noexcept;

// "scalar", "sse2-128", "neon-128" or "avx2-fma-256" (BFFT's names).
[[nodiscard]] const char* level_name(Level level) noexcept;

} // namespace stx_skia::simd
