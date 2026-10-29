#pragma once

#include <cstddef>
#include <cstdlib>
#include <functional>
#include <span>

// STX_VORBIS_RESTRICT marks a private kernel's pointer parameter as the only way, for the
// call, that the elements it reaches are read or written: no other pointer
// used in the call reaches them. It expands to `__restrict__` on Clang and
// GCC and `__restrict` on MSVC.
//
// Caution: restrict is a promise to the compiler, and a false promise is
// undefined behaviour that tests and sanitizers generally do not catch. Each
// use therefore
// - states its overlap rule beside the kernel;
// - checks that rule with require_disjoint, which ends the process in builds
//   without NDEBUG and in AddressSanitizer builds;
// - stays on parameters of functions with internal linkage, called with
//   pointers taken from checked spans. Clang marks restrict parameters
//   noalias; restrict on a local pointer is not relied on. On MSVC's ABI
//   `__restrict` on a parameter changes the mangled name, so a public
//   signature carries it only after review.
//
// The macro follows bfft's (third_party/bfft/src/detail/bruun_simd_backend.hpp)
// and stx_conv's STX_CONV_RESTRICT: MSVC proper takes `__restrict`; Clang
// (clang-cl included) and GCC take `__restrict__`, because macOS's
// <sys/cdefs.h> defines `__restrict` as nothing in C++, which silently drops
// the promise.
#if defined(_MSC_VER) && !defined(__clang__)
#define STX_VORBIS_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define STX_VORBIS_RESTRICT __restrict__
#else
#define STX_VORBIS_RESTRICT
#endif

namespace stx_vorbis::detail {

// Clang reports AddressSanitizer through __has_feature; GCC and MSVC define
// __SANITIZE_ADDRESS__.
#if defined(__SANITIZE_ADDRESS__)
#define STX_VORBIS_RESTRICT_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define STX_VORBIS_RESTRICT_SANITIZED 1
#endif
#endif

#if !defined(NDEBUG) || defined(STX_VORBIS_RESTRICT_SANITIZED)
inline constexpr bool overlap_checks = true;
#else
inline constexpr bool overlap_checks = false;
#endif

// Whether two spans share no byte. An empty span shares none. std::less
// orders pointers into different objects.
template <class First, class Second>
[[nodiscard]] bool spans_disjoint(std::span<First> first, std::span<Second> second) noexcept {
    if (first.empty() || second.empty()) return true;
    const std::byte* const first_start = static_cast<const std::byte*>(static_cast<const void*>(first.data()));
    const std::byte* const second_start = static_cast<const std::byte*>(static_cast<const void*>(second.data()));
    const std::less<const std::byte*> before{};
    const bool first_ends_before = !before(second_start, first_start + first.size_bytes());
    const bool second_ends_before = !before(first_start, second_start + second.size_bytes());
    return first_ends_before || second_ends_before;
}

// A kernel's overlap assertion: ends the process when `first` and `second`
// share a byte and checks are on.
template <class First, class Second>
void require_disjoint(std::span<First> first, std::span<Second> second) noexcept {
    if constexpr (overlap_checks) {
        if (!spans_disjoint(first, second)) std::abort();
    }
}

} // namespace stx_vorbis::detail
