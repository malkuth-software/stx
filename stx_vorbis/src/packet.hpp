#pragma once
#include "synthesis.hpp"
namespace stx_vorbis::detail {
struct Workspace final {
    explicit Workspace(std::pmr::memory_resource* memory)
        : spectrum(memory), floor_curve(memory), time(memory), previous(memory), pcm(memory),
          real(memory), imaginary(memory), classifications(memory) {}
    // Channel-major arrays; spectrum/floor stride=max_block/2, time stride=max_block.
    std::pmr::vector<double> spectrum;
    std::pmr::vector<double> floor_curve;
    std::pmr::vector<double> time;
    std::pmr::vector<double> previous;
    std::pmr::vector<float> pcm;
    std::pmr::vector<double> real;
    std::pmr::vector<double> imaginary;
    std::pmr::vector<unsigned int> classifications;
    std::array<bool, 255> floor_present{};
    std::array<bool, 255> residue_present{};
    std::array<unsigned int, 255> bundle{};
    std::array<int, 250> floor_y{};
    std::array<bool, 250> floor_active{};
    std::array<double, 255> lsp{};
    unsigned int previous_block{0};
    unsigned int stride{0};
    unsigned int frames{0};
    std::uint64_t operations{0};
    std::uint64_t operation_limit{0};
    Butterfly butterfly{scalar_butterfly};
};
// In-place inverse coupling of two disjoint, equally sized channel spectra.
// Borrows last only for this call; no allocation. Empty spans are permitted.
// Setup validation establishes distinct channel indices before packet decoding.
void inverse_couple(std::span<double> magnitudes, std::span<double> angles) noexcept;
void prepare_workspace(Workspace& workspace, const Setup& setup, const Limits& limits, Synthesis synthesis);
// Packet scratch is invalidated on failure; call reset_overlap before recovery.
void decode_packet(Workspace& workspace, const Setup& setup, std::span<const std::uint8_t> packet);
void reset_overlap(Workspace& workspace) noexcept;
} // namespace stx_vorbis::detail
