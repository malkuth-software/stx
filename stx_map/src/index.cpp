// The uniform-grid line index (index.hpp). A line is listed in every cell its
// bounding box touches; a query reports a line only from the first cell (in
// row-major order) of its box's cells that lies in the query's cells, so each
// line comes out once without any marking storage.

#include "stx_map/index.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace stx_map {
namespace {

constexpr std::int64_t world_width = 360LL * units_per_degree;
constexpr std::int64_t world_height = 180LL * units_per_degree;
constexpr std::uint16_t largest_side = 4096U;

struct CellRange final {
    std::uint32_t first_column = 0U;
    std::uint32_t last_column = 0U;
    std::uint32_t first_row = 0U;
    std::uint32_t last_row = 0U;
};

std::uint32_t cell_of(std::int64_t position, std::int64_t extent, std::uint32_t cells) noexcept {
    if (position <= 0) return 0U;
    if (position >= extent) return cells - 1U;
    const std::int64_t cell = position * static_cast<std::int64_t>(cells) / extent;
    const std::uint32_t clamped = static_cast<std::uint32_t>(cell);
    return clamped;
}

CellRange cells_of(const Box& box, GridShape shape) noexcept {
    CellRange range;
    range.first_column = cell_of(box.min_x, world_width, shape.columns);
    range.last_column = cell_of(box.max_x, world_width, shape.columns);
    range.first_row = cell_of(box.min_y, world_height, shape.rows);
    range.last_row = cell_of(box.max_y, world_height, shape.rows);
    return range;
}

std::size_t cells_in(const CellRange& range) noexcept {
    const std::size_t columns = static_cast<std::size_t>(range.last_column - range.first_column) + 1U;
    const std::size_t rows = static_cast<std::size_t>(range.last_row - range.first_row) + 1U;
    const std::size_t count = columns * rows;
    return count;
}

bool shape_valid(GridShape shape) noexcept {
    const bool valid = shape.columns >= 1U && shape.columns <= largest_side && shape.rows >= 1U && shape.rows <= largest_side;
    return valid;
}

bool view_valid(const LayerView& layer) noexcept {
    if (layer.x.size() != layer.y.size()) return false;
    if (layer.starts.empty()) return layer.x.empty();
    if (layer.starts[0] != 0U || layer.starts[layer.starts.size() - 1U] != layer.x.size()) return false;
    for (std::size_t k = 0U; k + 1U < layer.starts.size(); ++k) {
        if (layer.starts[k + 1U] <= layer.starts[k]) return false;
    }
    return true;
}

Box box_of(const LayerView& layer, std::size_t line) noexcept {
    const std::uint32_t first = layer.starts[line];
    const std::uint32_t end = layer.starts[line + 1U];
    Box box;
    box.min_x = layer.x[first];
    box.max_x = box.min_x;
    box.min_y = layer.y[first];
    box.max_y = box.min_y;
    for (std::uint32_t v = first + 1U; v < end; ++v) {
        if (layer.x[v] < box.min_x) box.min_x = layer.x[v];
        if (layer.x[v] > box.max_x) box.max_x = layer.x[v];
        if (layer.y[v] < box.min_y) box.min_y = layer.y[v];
        if (layer.y[v] > box.max_y) box.max_y = layer.y[v];
    }
    return box;
}

bool boxes_meet(const Box& a, const Box& b) noexcept {
    const bool meet = a.min_x <= b.max_x && b.min_x <= a.max_x && a.min_y <= b.max_y && b.min_y <= a.max_y;
    return meet;
}

} // namespace

std::size_t index_entry_count(const LayerView& layer, GridShape shape) noexcept {
    if (!shape_valid(shape) || !view_valid(layer)) return 0U;
    std::size_t total = 0U;
    const std::size_t lines = line_count(layer);
    for (std::size_t line = 0U; line < lines; ++line) total += cells_in(cells_of(box_of(layer, line), shape));
    return total;
}

Status build_index(const LayerView& layer, GridShape shape, IndexStorage storage) noexcept {
    if (!shape_valid(shape) || !view_valid(layer)) return Status::invalid_argument;
    const std::size_t lines = line_count(layer);
    const std::size_t cells = static_cast<std::size_t>(shape.columns) * shape.rows;
    if (storage.boxes.size() < lines || storage.cell_starts.size() < cells + 1U) return Status::storage_too_small;
    if (lines > 0xFFFFFFFFU) return Status::invalid_argument;
    // First pass: boxes and per-cell counts (kept in cell_starts[cell + 1]).
    for (std::size_t cell = 0U; cell <= cells; ++cell) storage.cell_starts[cell] = 0U;
    std::size_t total = 0U;
    for (std::size_t line = 0U; line < lines; ++line) {
        const Box box = box_of(layer, line);
        storage.boxes[line] = box;
        const CellRange range = cells_of(box, shape);
        for (std::uint32_t row = range.first_row; row <= range.last_row; ++row) {
            for (std::uint32_t column = range.first_column; column <= range.last_column; ++column) {
                const std::size_t cell = static_cast<std::size_t>(row) * shape.columns + column;
                ++storage.cell_starts[cell + 1U];
            }
        }
        total += cells_in(range);
    }
    if (storage.entries.size() < total || total > 0xFFFFFFFFU) return Status::storage_too_small;
    for (std::size_t cell = 0U; cell < cells; ++cell) storage.cell_starts[cell + 1U] += storage.cell_starts[cell];
    // Second pass: entries, each cell's lines in line order. cell_starts[cell]
    // serves as the cell's write position and is restored after.
    for (std::size_t line = 0U; line < lines; ++line) {
        const CellRange range = cells_of(storage.boxes[line], shape);
        for (std::uint32_t row = range.first_row; row <= range.last_row; ++row) {
            for (std::uint32_t column = range.first_column; column <= range.last_column; ++column) {
                const std::size_t cell = static_cast<std::size_t>(row) * shape.columns + column;
                storage.entries[storage.cell_starts[cell]] = static_cast<std::uint32_t>(line);
                ++storage.cell_starts[cell];
            }
        }
    }
    for (std::size_t cell = cells; cell > 0U; --cell) storage.cell_starts[cell] = storage.cell_starts[cell - 1U];
    storage.cell_starts[0] = 0U;
    return Status::ok;
}

std::size_t query_index(GridShape shape, const IndexStorage& storage, const Box& query,
                        std::span<std::uint32_t> lines_out) noexcept {
    if (!shape_valid(shape) || query.min_x > query.max_x || query.min_y > query.max_y) return 0U;
    const std::size_t cells = static_cast<std::size_t>(shape.columns) * shape.rows;
    if (storage.cell_starts.size() < cells + 1U) return 0U;
    const CellRange range = cells_of(query, shape);
    std::size_t found = 0U;
    for (std::uint32_t row = range.first_row; row <= range.last_row; ++row) {
        for (std::uint32_t column = range.first_column; column <= range.last_column; ++column) {
            const std::size_t cell = static_cast<std::size_t>(row) * shape.columns + column;
            for (std::uint32_t at = storage.cell_starts[cell]; at < storage.cell_starts[cell + 1U]; ++at) {
                const std::uint32_t line = storage.entries[at];
                const Box& box = storage.boxes[line];
                if (!boxes_meet(box, query)) continue;
                const CellRange own = cells_of(box, shape);
                const std::uint32_t first_column = own.first_column > range.first_column ? own.first_column : range.first_column;
                const std::uint32_t first_row = own.first_row > range.first_row ? own.first_row : range.first_row;
                if (column != first_column || row != first_row) continue;
                if (found < lines_out.size()) lines_out[found] = line;
                ++found;
            }
        }
    }
    return found;
}

} // namespace stx_map
