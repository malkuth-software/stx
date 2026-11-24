#pragma once
// stx_map: a small baked world map (Natural Earth 1:50m) in the TMAP 2 format
// and its decoder. Positions are integers: units_per_degree per degree,
// x from 180 degrees west, y from 90 degrees south (y grows northward).
// Nothing here draws; a consumer projects and draws the polylines itself.
//
// Decoding is two calls. inspect_map checks the whole file (its CRC-32,
// header and every count) and fills a MapInfo, from which the caller sizes
// one LayerStorage per layer it wants; decode_map then fills that storage
// with integer arithmetic only and allocates nothing. The format is
// described in docs/FORMAT.md; tools/bake/tmap_decode.js is the reference
// decoder this one is checked against.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace stx_map {

// What an operation reports. A refused operation leaves the caller's
// storage partly written; nothing it filled is to be used.
enum class Status : std::uint8_t {
    ok,
    not_tmap,          // not a TMAP 2 file (magic or version)
    bad_check_value,   // the CRC-32 over the file does not match
    bad_header,        // a header field out of its range
    bad_counts,        // counts the file or the limits cannot hold
    bad_body,          // a range-coded body that does not decode to its counts
    bad_tie,           // a line-end tie that names no valid place
    invalid_argument,  // a layer selection, storage or parameter that cannot be used
    storage_too_small  // caller storage shorter than the counts need
};

inline constexpr std::uint32_t units_per_degree = 32768U;
inline constexpr std::size_t max_layers = 16U;
inline constexpr std::size_t max_layer_name = 32U;

enum class LayerKind : std::uint8_t {
    rings = 0, // closed outlines (land, lakes); the closing segment is implied
    lines = 1  // open polylines (borders)
};

// One layer's record from the file header.
struct LayerInfo final {
    std::array<char, max_layer_name> name{};
    std::uint8_t name_length = 0U;
    LayerKind kind = LayerKind::rings;
    // An optional layer is decoded only when it is asked for by name.
    bool optional = false;
    // Bit i: this layer's ties name places in layer i (always an earlier one).
    std::uint16_t depends = 0U;
    std::uint16_t start_grid = 0U;     // position units between start cells
    std::uint32_t lines = 0U;
    std::uint32_t vertices = 0U;
    std::uint32_t ties = 0U;
    std::uint32_t body_offset = 0U;    // byte offset of the range-coded body
    std::uint32_t body_bytes = 0U;
};

// The whole header, after inspect_map has checked it.
struct MapInfo final {
    std::uint32_t units_per_degree = 0U;
    // Every decoded line lies within this many position units of its
    // source line (the symmetric Hausdorff distance the bake measured).
    std::uint32_t tolerance_units = 0U;
    std::uint8_t mantissa_bits = 0U;
    std::uint16_t length_unit = 0U;
    std::array<std::uint8_t, 16> turn_bins_log2{};
    std::uint8_t layer_count = 0U;
    std::array<LayerInfo, max_layers> layers{};
};

// What the decoder may be asked to hold. The default keeps a hostile file's
// claims to tens of megabytes.
struct DecodeLimits final {
    std::uint32_t vertices = 1U << 22U; // over all layers
};

// A set of layers, bit i for layer i.
using LayerMask = std::uint16_t;

// Caller storage for one decoded layer: x and y hold `vertices` entries,
// starts holds `lines + 1` (line k is vertices starts[k] .. starts[k+1]-1).
// A layer not decoded may leave every span empty.
struct LayerStorage final {
    std::span<std::int32_t> x;
    std::span<std::int32_t> y;
    std::span<std::uint32_t> starts;
};

// A decoded (or derived) layer, read-only.
struct LayerView final {
    LayerKind kind = LayerKind::rings;
    std::span<const std::int32_t> x;
    std::span<const std::int32_t> y;
    std::span<const std::uint32_t> starts;
};

[[nodiscard]] inline std::size_t line_count(const LayerView& layer) noexcept {
    const std::size_t count = layer.starts.empty() ? 0U : layer.starts.size() - 1U;
    return count;
}

// Checks the whole file and fills `info`. Leaves `info` unspecified on
// refusal.
[[nodiscard]] Status inspect_map(std::span<const std::byte> file, const DecodeLimits& limits, MapInfo& info) noexcept;

// The layers that are not optional.
[[nodiscard]] LayerMask default_layers(const MapInfo& info) noexcept;
// The layer with this name, or 0 when there is none.
[[nodiscard]] LayerMask layer_named(const MapInfo& info, std::string_view name) noexcept;
// The mask with every layer its layers' ties depend on.
[[nodiscard]] LayerMask with_dependencies(const MapInfo& info, LayerMask mask) noexcept;
[[nodiscard]] std::string_view layer_name(const LayerInfo& layer) noexcept;

// Decodes the layers in `mask` (which must hold its own dependencies) into
// storage[i] for layer i; storage.size() is info.layer_count. The file is
// inspected again and must match `info`. Allocates nothing.
[[nodiscard]] Status decode_map(std::span<const std::byte> file, const MapInfo& info, const DecodeLimits& limits,
                                LayerMask mask, std::span<const LayerStorage> storage) noexcept;

// The CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320) of `bytes`; a
// TMAP 2 file ends with this over everything before it, little-endian.
[[nodiscard]] std::uint32_t crc32(std::span<const std::byte> bytes) noexcept;

// The view of a decoded layer's storage.
[[nodiscard]] LayerView view_of(const LayerInfo& info, const LayerStorage& storage) noexcept;

// The TMAP 2 file built into the library: Natural Earth 1:50m land, lakes
// and the optional borders_admin0_ne_default layer (PROVENANCE.md).
[[nodiscard]] std::span<const std::byte> embedded_world_map() noexcept;

} // namespace stx_map
