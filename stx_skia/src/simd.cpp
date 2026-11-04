#include "stx_skia/simd.hpp"

#include "include/private/SkFeatures.h"
#include "src/core/SkCpu.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

namespace stx_skia::simd {

namespace {

// A level request not yet made: one past the last Level, below selected_flag.
constexpr std::uint8_t no_request = 3;

// The request recorded by request_level, then (at selection) the selected
// level itself with selected_flag set; written with compare-exchange so a
// request racing the selection either lands before it or is refused.
constexpr std::uint8_t selected_flag = 0x80;
std::atomic<std::uint8_t> state{no_request};

bool valid_level(Level level) noexcept {
    const bool valid = level == Level::scalar || level == Level::vector128 ||
        level == Level::avx2_fma;
    return valid;
}

// The level named by STX_SIMD_LEVEL, or no_request when it is unset or
// names no level.
std::uint8_t environment_request() noexcept {
    const char* const value = std::getenv("STX_SIMD_LEVEL");
    if (value == nullptr) return no_request;
    if (std::strcmp(value, "scalar") == 0) return static_cast<std::uint8_t>(Level::scalar);
    if (std::strcmp(value, "vector128") == 0) return static_cast<std::uint8_t>(Level::vector128);
    if (std::strcmp(value, "avx2_fma") == 0) return static_cast<std::uint8_t>(Level::avx2_fma);
    return no_request;
}

// The lowest level this build's kernels run at: its compiled baseline. A SIMD build has no
// run-time scalar tier (the scalar reference is the STX_SKIA_ISA=scalar build).
Level floor_level() noexcept {
#if SK_CPU_X64_LEVEL >= SK_CPU_X64_LEVEL_AVX2
    return Level::avx2_fma;
#elif SK_CPU_X64_LEVEL >= SK_CPU_X64_LEVEL_SSE2 || defined(SK_ARM_HAS_NEON)
    return Level::vector128;
#else
    return Level::scalar;
#endif
}

// Moves a requested level into what runs here: at least the build's floor, at most what the
// machine and the build can run.
Level runnable_level(Level requested) noexcept {
    const Level ceiling = hardware_level();
    const Level floor = floor_level();
    Level level = requested;
    if (level > ceiling) level = ceiling;
    if (level < floor) level = floor;
    return level;
}

} // namespace

Level hardware_level() noexcept {
#if defined(STX_SKIA_ML3_UNITS)
    // A baseline x86-64 build carries the avx2_fma variants; SkCpu checks the CPU's
    // AVX2, FMA, BMI1, BMI2 and F16C flags and the operating system's saving
    // of the AVX registers. The features are cached here too, since a level
    // fixed before they were would stay vector128.
    SkCpu::CacheRuntimeFeatures();
    const bool wide = SkCpu::Supports(SkX64::ML3);
    const Level level = wide ? Level::avx2_fma : Level::vector128;
    return level;
#elif SK_CPU_X64_LEVEL >= SK_CPU_X64_LEVEL_AVX2
    // Built for AVX2 throughout (the x86-64-v3 profile).
    return Level::avx2_fma;
#elif SK_CPU_X64_LEVEL >= SK_CPU_X64_LEVEL_SSE2 || defined(SK_ARM_HAS_NEON)
    return Level::vector128;
#else
    return Level::scalar;
#endif
}

Level selected_level() noexcept {
    std::uint8_t current = state.load(std::memory_order_acquire);
    while ((current & selected_flag) == 0) {
        std::uint8_t request = current;
        if (request == no_request) request = environment_request();
        const Level requested = request == no_request
            ? Level::avx2_fma : static_cast<Level>(request);
        const Level level = runnable_level(requested);
        const std::uint8_t fixed = static_cast<std::uint8_t>(
            selected_flag | static_cast<std::uint8_t>(level));
        if (state.compare_exchange_weak(current, fixed, std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
            current = fixed;
        }
    }
    const Level result = static_cast<Level>(current & static_cast<std::uint8_t>(~selected_flag));
    return result;
}

bool request_level(Level level) noexcept {
    if (!valid_level(level)) return false;
    std::uint8_t current = state.load(std::memory_order_acquire);
    while ((current & selected_flag) == 0) {
        if (state.compare_exchange_weak(current, static_cast<std::uint8_t>(level),
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
            return true;
        }
    }
    return false;
}

const char* level_name(Level level) noexcept {
    switch (level) {
    case Level::scalar:
        return "scalar";
    case Level::vector128:
#if defined(SK_ARM_HAS_NEON)
        return "neon-128";
#else
        return "sse2-128";
#endif
    case Level::avx2_fma:
        return "avx2-fma-256";
    }
    return "unknown";
}

} // namespace stx_skia::simd
