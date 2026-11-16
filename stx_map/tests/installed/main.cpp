// Installed stx_map consumer: inspect and decode the embedded map's default
// layers, then its borders on request, derive a coarser level, index it and
// project a point, using only the installed headers and the exported
// stx_map::stx_map target.
#include <stx_map/index.hpp>
#include <stx_map/levels.hpp>
#include <stx_map/map.hpp>
#include <stx_map/projection.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "stx_map: %s\n", message);
    std::abort();
}
} // namespace

int main() {
    const std::span<const std::byte> file = stx_map::embedded_world_map();
    const stx_map::DecodeLimits limits;
    stx_map::MapInfo info;
    require(stx_map::inspect_map(file, limits, info) == stx_map::Status::ok, "inspect");
    std::vector<std::vector<std::int32_t>> x(info.layer_count);
    std::vector<std::vector<std::int32_t>> y(info.layer_count);
    std::vector<std::vector<std::uint32_t>> starts(info.layer_count);
    std::vector<stx_map::LayerStorage> storage(info.layer_count);
    for (std::size_t i = 0U; i < info.layer_count; ++i) {
        x[i].resize(info.layers[i].vertices);
        y[i].resize(info.layers[i].vertices);
        starts[i].resize(static_cast<std::size_t>(info.layers[i].lines) + 1U);
        storage[i].x = x[i];
        storage[i].y = y[i];
        storage[i].starts = starts[i];
    }
    const stx_map::LayerMask defaults = stx_map::default_layers(info);
    require(stx_map::decode_map(file, info, limits, defaults, storage) == stx_map::Status::ok, "decode defaults");
    const stx_map::LayerMask borders =
        stx_map::with_dependencies(info, stx_map::layer_named(info, "borders_admin0_ne_default"));
    require(borders != 0U, "the borders layer exists");
    require(stx_map::decode_map(file, info, limits, borders, storage) == stx_map::Status::ok, "decode borders");

    std::size_t land_layer = 0U;
    while (land_layer < info.layer_count && stx_map::layer_name(info.layers[land_layer]) != "land") ++land_layer;
    require(land_layer < info.layer_count, "the land layer exists");
    const stx_map::LayerView land = stx_map::view_of(info.layers[land_layer], storage[land_layer]);
    std::vector<std::uint8_t> keep(land.x.size());
    std::vector<std::uint32_t> stack(stx_map::simplify_stack_entries(land) * 2U);
    std::vector<std::int32_t> level_x(land.x.size());
    std::vector<std::int32_t> level_y(land.x.size());
    std::vector<std::uint32_t> level_starts(stx_map::line_count(land) + 1U);
    stx_map::SimplifyScratch scratch;
    scratch.keep = keep;
    scratch.stack = stack;
    stx_map::LayerStorage level;
    level.x = level_x;
    level.y = level_y;
    level.starts = level_starts;
    stx_map::SimplifiedCounts counts;
    require(stx_map::simplify_layer(land, info.tolerance_units * 8U, scratch, level, counts) == stx_map::Status::ok,
            "simplify");
    stx_map::LayerView coarse;
    coarse.kind = land.kind;
    coarse.x = std::span<const std::int32_t>(level_x.data(), counts.vertices);
    coarse.y = std::span<const std::int32_t>(level_y.data(), counts.vertices);
    coarse.starts = std::span<const std::uint32_t>(level_starts.data(), static_cast<std::size_t>(counts.lines) + 1U);

    const stx_map::GridShape shape;
    std::vector<stx_map::Box> boxes(counts.lines);
    std::vector<std::uint32_t> cell_starts(static_cast<std::size_t>(shape.columns) * shape.rows + 1U);
    std::vector<std::uint32_t> entries(stx_map::index_entry_count(coarse, shape));
    stx_map::IndexStorage index;
    index.boxes = boxes;
    index.cell_starts = cell_starts;
    index.entries = entries;
    require(stx_map::build_index(coarse, shape, index) == stx_map::Status::ok, "index");
    const std::int32_t d = static_cast<std::int32_t>(stx_map::units_per_degree);
    const stx_map::Box europe{170 * d, 120 * d, 220 * d, 160 * d};
    const std::size_t found = stx_map::query_index(shape, index, europe, std::span<std::uint32_t>());
    require(found > 0U, "land in Europe");
    const stx_map::WorldPoint point = stx_map::project(stx_map::Projection::web_mercator, 180 * d, 90 * d);
    require(point.x == 0.5, "projection");
    std::printf("stx_map installed consumer: %u land lines, %u at the coarse level, %zu in Europe\n",
                info.layers[land_layer].lines, counts.lines, found);
    return 0;
}
