#pragma once
// A spatial index for viewport culling: a uniform grid over the world
// (x in [0, 360) degrees, y in [0, 180) degrees, in position units), each
// cell listing the lines whose bounding box touches it. Built in two passes
// into caller storage; a query reports each line once.

#include "stx_map/map.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace stx_map {

// An inclusive box in position units.
struct Box final {
    std::int32_t min_x = 0;
    std::int32_t min_y = 0;
    std::int32_t max_x = 0;
    std::int32_t max_y = 0;
};

// The grid's cells; both at least 1 and at most 4096.
struct GridShape final {
    std::uint16_t columns = 64U;
    std::uint16_t rows = 32U;
};

// Index storage: one box per line, columns * rows + 1 cell starts, and the
// entries (index_entry_count of them).
struct IndexStorage final {
    std::span<Box> boxes;
    std::span<std::uint32_t> cell_starts;
    std::span<std::uint32_t> entries;
};

// The entries the index of `layer` needs (a line counts once per cell its
// box touches).
[[nodiscard]] std::size_t index_entry_count(const LayerView& layer, GridShape shape) noexcept;

// Fills `storage` for `layer`. invalid_argument for a bad shape or view,
// storage_too_small for short storage.
[[nodiscard]] Status build_index(const LayerView& layer, GridShape shape, IndexStorage storage) noexcept;

// The lines whose box meets `query`, each once, in cell order: writes up to
// lines_out.size() of them and returns how many there are in all. `storage`
// is what build_index filled for this shape.
[[nodiscard]] std::size_t query_index(GridShape shape, const IndexStorage& storage, const Box& query,
                                      std::span<std::uint32_t> lines_out) noexcept;

} // namespace stx_map
