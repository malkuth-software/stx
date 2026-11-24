#pragma once
// Coarser levels of detail, derived from a decoded layer at load: the
// Douglas-Peucker simplification of every line at a tolerance, in exact
// integer arithmetic (squared distances compared in 128 bits), so every
// platform derives the same points. Line ends are always kept, so a tied
// border still ends on its coast. A ring is split at its vertex farthest
// from its start and each half simplified; a ring left with fewer than
// three points, or smaller than the tolerance across both ways, is dropped.

#include "stx_map/map.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace stx_map {

// Working storage for simplify_layer: keep holds one flag per input vertex,
// stack 2 * simplify_stack_entries(input) entries.
struct SimplifyScratch final {
    std::span<std::uint8_t> keep;
    std::span<std::uint32_t> stack;
};

// What simplify_layer wrote.
struct SimplifiedCounts final {
    std::uint32_t lines = 0U;
    std::uint32_t vertices = 0U;
};

// The work stack's pairs for the input's longest line.
[[nodiscard]] std::size_t simplify_stack_entries(const LayerView& input) noexcept;

// Writes the simplification of `input` at `tolerance_units` into `output`
// (x and y at least the input's vertex count, starts at least its line
// count + 1) and its counts into `counts`. invalid_argument for a zero
// tolerance, a malformed view or a position farther than 2^28 units from
// the origin (decode_map never gives one), storage_too_small for short
// storage or scratch. On refusal nothing in `output` is to be used.
[[nodiscard]] Status simplify_layer(const LayerView& input, std::uint32_t tolerance_units, SimplifyScratch scratch,
                                    LayerStorage output, SimplifiedCounts& counts) noexcept;

} // namespace stx_map
