#pragma once
#include "stx_vorbis/bit_reader.hpp"
#include "memory.hpp"
#include <array>
#include <vector>
#include <string>
#include <memory>
namespace stx_vorbis::detail {
struct Identification final {
    std::uint32_t rate{0};
    std::uint32_t channels{0};
    std::array<std::uint32_t, 2> blocks{};
};
struct HuffmanNode final { std::array<std::int32_t, 2> children{-1, -1}; std::int32_t entry{-1}; };
struct Prefix final { std::int32_t entry{-1}; std::uint8_t bits{0}; };
struct Codebook final {
    explicit Codebook(std::pmr::memory_resource* memory) : nodes(memory), values(memory) {}
    std::uint32_t dimensions{0};
    std::uint32_t entries{0};
    std::uint32_t used{0};
    std::pmr::vector<HuffmanNode> nodes;
    std::array<Prefix, 1024> prefix{};
    // Entry-major, dimensions contiguous. Empty for scalar-only books.
    std::pmr::vector<double> values;
    [[nodiscard]] Status decode(BitReader& reader, std::uint32_t& entry) const noexcept;
};
struct FloorClass final {
    std::uint32_t dimensions{0};
    std::uint32_t subclasses{0};
    std::uint32_t master{0};
    std::array<int, 8> books{};
};
struct Floor final {
    explicit Floor(std::pmr::memory_resource* memory) : bark0(memory), bark1(memory) {}
    unsigned int type{0};
    unsigned int partitions{0};
    std::array<unsigned int, 31> partition_class{};
    std::array<FloorClass, 16> classes{};
    unsigned int multiplier{0};
    unsigned int points{0};
    std::array<int, 250> x{};
    std::array<unsigned int, 250> low{};
    std::array<unsigned int, 250> high{};
    std::array<unsigned int, 250> sorted{};
    unsigned int order{0};
    unsigned int rate{0};
    unsigned int bark_size{0};
    unsigned int amplitude_bits{0};
    unsigned int amplitude_db{0};
    unsigned int book_count{0};
    std::array<unsigned int, 16> books{};
    // Precomputed 2*cos(pi*bark_bin/bark_size) per spectrum bin.
    std::pmr::vector<double> bark0;
    std::pmr::vector<double> bark1;
};
struct Residue final {
    unsigned int type{0};
    unsigned int begin{0};
    unsigned int end{0};
    unsigned int partition_size{0};
    unsigned int classifications{0};
    unsigned int classbook{0};
    std::array<std::array<int, 8>, 64> books{};
};
struct Coupling final { unsigned int magnitude{0}; unsigned int angle{0}; };
struct Mapping final {
    explicit Mapping(std::pmr::memory_resource* memory) : couplings(memory) {}
    unsigned int submaps{0};
    std::array<unsigned int, 255> mux{};
    std::array<unsigned int, 16> floors{};
    std::array<unsigned int, 16> residues{};
    std::pmr::vector<Coupling> couplings;
};
struct Mode final { bool large{false}; unsigned int mapping{0}; };
struct Transform final {
    explicit Transform(std::pmr::memory_resource* memory)
        : permutation(memory), cosine(memory), sine(memory), pre_cosine(memory), pre_sine(memory),
          post_cosine(memory), post_sine(memory), window(memory) {}
    unsigned int block{0};
    unsigned int fft_size{0};
    std::pmr::vector<unsigned int> permutation;
    std::pmr::vector<double> cosine;
    std::pmr::vector<double> sine;
    std::pmr::vector<double> pre_cosine;
    std::pmr::vector<double> pre_sine;
    std::pmr::vector<double> post_cosine;
    std::pmr::vector<double> post_sine;
    std::pmr::vector<double> window;
};
struct Setup final {
    explicit Setup(std::pmr::memory_resource* memory)
        : books(memory), floors(memory), residues(memory), mappings(memory), modes(memory),
          transforms{Transform(memory), Transform(memory)} {}
    Identification identification{};
    std::pmr::vector<Codebook> books;
    std::pmr::vector<Floor> floors;
    std::pmr::vector<Residue> residues;
    std::pmr::vector<Mapping> mappings;
    std::pmr::vector<Mode> modes;
    std::array<Transform, 2> transforms;
    std::array<double, 256> inverse_db{};
};
struct SetupDeleter final {
    std::pmr::memory_resource* memory{nullptr};
    void operator()(const Setup* setup) const noexcept;
};
using SetupOwner = std::unique_ptr<const Setup, SetupDeleter>;
[[nodiscard]] Identification parse_identification(std::span<const std::uint8_t> packet, const Limits& limits);
void validate_header(std::span<const std::uint8_t> packet, unsigned int type);
void build_huffman(Codebook& book, std::span<const std::uint8_t> lengths);
void parse_setup(Setup& setup, std::span<const std::uint8_t> packet, const Limits& limits,
                 std::pmr::memory_resource* memory);
void prepare_transform(Transform& transform, unsigned int block);
} // namespace stx_vorbis::detail
