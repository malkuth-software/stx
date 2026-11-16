// Fuzz target for the TMAP 2 decoder: inspect, then decode every layer into
// storage sized from the inspected counts. Under libFuzzer a custom mutator
// repairs the trailing CRC-32 after each mutation, so mutations reach the
// header checks and the range-coded bodies, not only the check value. The
// standalone build (STX_MAP_FUZZ_STANDALONE) runs the embedded map and a
// deterministic set of single-byte changes with the check value repaired.

#include "stx_map/map.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

namespace {

void check_decoded(const stx_map::MapInfo& info, std::span<const stx_map::LayerStorage> storage, stx_map::LayerMask mask) {
    for (std::uint32_t i = 0U; i < info.layer_count; ++i) {
        if (((static_cast<std::uint32_t>(mask) >> i) & 1U) == 0U) continue;
        const stx_map::LayerInfo& layer = info.layers[i];
        const stx_map::LayerStorage& out = storage[i];
        // Line starts rise strictly and end at the vertex count.
        if (out.starts[0] != 0U || out.starts[layer.lines] != layer.vertices) std::abort();
        for (std::uint32_t k = 0U; k < layer.lines; ++k) {
            if (out.starts[k + 1U] <= out.starts[k]) std::abort();
        }
    }
}

int decode_one(std::span<const std::byte> bytes) {
    stx_map::DecodeLimits limits;
    limits.vertices = 1U << 16U;
    stx_map::MapInfo info;
    if (stx_map::inspect_map(bytes, limits, info) != stx_map::Status::ok) return 0;
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
    const stx_map::LayerMask mask = static_cast<stx_map::LayerMask>((1U << info.layer_count) - 1U);
    const stx_map::Status status = stx_map::decode_map(bytes, info, limits, mask, storage);
    if (status == stx_map::Status::ok) check_decoded(info, storage, mask);
    return 0;
}

void repair_check_value(std::uint8_t* data, std::size_t size) {
    if (size < 4U) return;
    const std::span<const std::uint8_t> covered(data, size - 4U);
    const std::uint32_t crc = stx_map::crc32(std::as_bytes(covered));
    for (std::size_t b = 0U; b < 4U; ++b) data[size - 4U + b] = static_cast<std::uint8_t>((crc >> (8U * b)) & 0xFFU);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);
    const int result = decode_one(std::as_bytes(input));
    return result;
}

#if !defined(STX_MAP_FUZZ_STANDALONE)
extern "C" std::size_t LLVMFuzzerMutate(std::uint8_t*, std::size_t, std::size_t);
extern "C" std::size_t LLVMFuzzerCustomMutator(std::uint8_t* data, std::size_t size, std::size_t maximum,
                                              unsigned seed) {
    (void)seed;
    const std::size_t changed = LLVMFuzzerMutate(data, size, maximum);
    repair_check_value(data, changed);
    return changed;
}
#else
int main() {
    const std::span<const std::byte> embedded = stx_map::embedded_world_map();
    std::vector<std::uint8_t> file(embedded.size());
    for (std::size_t i = 0U; i < embedded.size(); ++i) file[i] = static_cast<std::uint8_t>(embedded[i]);
    (void)LLVMFuzzerTestOneInput(file.data(), file.size());
    const std::uint8_t empty = 0U;
    (void)LLVMFuzzerTestOneInput(&empty, 0U);
    // Each change is undone before the next.
    for (std::size_t at = 0U; at + 4U < file.size(); at += 61U) {
        const std::uint8_t kept = file[at];
        file[at] = static_cast<std::uint8_t>(kept ^ 0xA5U);
        repair_check_value(file.data(), file.size());
        (void)LLVMFuzzerTestOneInput(file.data(), file.size());
        file[at] = kept;
    }
    return 0;
}
#endif
