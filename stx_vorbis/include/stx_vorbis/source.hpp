#pragma once
#include "decoder.hpp"
#include <array>
#include <cstdio>
namespace stx_vorbis {
// Source is borrowed by PullDecoder and must outlive it. read returns ok with
// progress, end at EOF, or an error. seek uses absolute byte offsets.
class Source {
public:
    virtual ~Source() = default;
    [[nodiscard]] virtual Status read(std::span<std::uint8_t> destination, std::size_t& count) noexcept = 0;
    [[nodiscard]] virtual Status seek(std::uint64_t byte_offset) noexcept = 0;
};
class MemorySource final : public Source {
public:
    explicit MemorySource(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] Status read(std::span<std::uint8_t> destination, std::size_t& count) noexcept override;
    [[nodiscard]] Status seek(std::uint64_t byte_offset) noexcept override;
private:
    std::span<const std::uint8_t> bytes_{};
    std::size_t position_{0};
};
class FileSource final : public Source {
public:
    FileSource() noexcept = default;
    ~FileSource() override;
    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;
    [[nodiscard]] Status open(const char* path) noexcept;
    // Borrowed FILE starts at its current position, which becomes stream offset 0.
    [[nodiscard]] Status attach(std::FILE* file, bool take_ownership = false) noexcept;
    void close() noexcept;
    [[nodiscard]] Status read(std::span<std::uint8_t> destination, std::size_t& count) noexcept override;
    [[nodiscard]] Status seek(std::uint64_t byte_offset) noexcept override;
private:
    std::FILE* file_{nullptr};
    std::uint64_t origin_{0};
    bool owned_{false};
};
struct SamplePosition final { std::uint32_t chain_index{0}; std::uint64_t sample{0}; };
class PullDecoder final {
public:
    explicit PullDecoder(Source& source, Limits limits = {}, Recovery recovery = Recovery::strict,
                         Synthesis synthesis = Synthesis::automatic, std::pmr::memory_resource* memory = nullptr);
    [[nodiscard]] Status advance() noexcept;
    [[nodiscard]] Decoder& decoder() noexcept;
    // Exact bounded-memory seek by decoding from BOS. O(target) work, no hidden
    // growing index. Events traversed during seek are acknowledged internally;
    // current metadata/info describe the target chain. PCM begins at target.
    [[nodiscard]] Status seek(SamplePosition target) noexcept;
    [[nodiscard]] Status rewind() noexcept;
private:
    Source& source_;
    Decoder decoder_;
    std::array<std::uint8_t, 32768> input_{};
    std::size_t size_{0};
    std::size_t used_{0};
    bool final_{false};
    bool final_sent_{false};
};
} // namespace stx_vorbis
