// The TMAP 2 file built into the library (map.hpp, embedded_world_map).

#include "stx_map/map.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace stx_map {
namespace {

#include "world_map_data.inc"

} // namespace

std::span<const std::byte> embedded_world_map() noexcept {
    // The array has static storage duration, so the span never dangles.
    // Reading unsigned char storage through std::byte is permitted aliasing.
    const std::byte* const first = reinterpret_cast<const std::byte*>(world_map_bytes.data());
    const std::span<const std::byte> file(first, world_map_bytes.size());
    return file;
}

} // namespace stx_map
