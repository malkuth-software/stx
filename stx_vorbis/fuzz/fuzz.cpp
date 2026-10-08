#include "stx_vorbis/decoder.hpp"
#include "packet.hpp"
#include <cstdlib>
namespace {
stx_vorbis::Limits bounded_limits() noexcept {
    stx_vorbis::Limits limits{};
    limits.memory_bytes = 8 * 1024 * 1024;
    limits.packet_bytes = 256 * 1024;
    limits.codebook_entries = 16384;
    limits.lookup_values = 65536;
    limits.packet_operations = 256 * 1024;
    limits.channels = 8;
    return limits;
}
void bits(const std::span<const std::uint8_t> bytes) {
    stx_vorbis::BitReader reader(bytes);
    std::size_t position = 0;
    for (const std::uint8_t seed : bytes) {
        const unsigned int count = seed % 65;
        std::uint64_t actual = 0;
        const bool success = reader.read64(count, actual);
        const bool expected_success = count <= bytes.size() * 8 - position;
        if (success != expected_success) std::abort();
        if (!success) continue;
        std::uint64_t expected = 0;
        for (unsigned int bit = 0; bit < count; ++bit) {
            const std::size_t offset = position + bit;
            expected |= std::uint64_t{(bytes[offset / 8] >> (offset % 8)) & 1U} << bit;
        }
        position += count;
        if (actual != expected || reader.position() != position) std::abort();
    }
}
void huffman(const std::span<const std::uint8_t> bytes, std::pmr::memory_resource* const memory) {
    if (bytes.empty()) return;
    const std::size_t count = std::min<std::size_t>(bytes[0] + 1, bytes.size() - 1);
    std::array<std::uint8_t, 256> lengths{};
    for (std::size_t index = 0; index < count; ++index) lengths[index] = bytes[index + 1] % 33;
    stx_vorbis::detail::Codebook book(memory);
    book.entries = static_cast<std::uint32_t>(count); book.dimensions = 1;
    stx_vorbis::detail::build_huffman(book, std::span<const std::uint8_t>(lengths.data(), count));
    stx_vorbis::BitReader reader(bytes.subspan(count + 1));
    std::uint32_t entry = 0;
    for (std::size_t step = 0; step < bytes.size() * 8; ++step)
        if (book.decode(reader, entry) != stx_vorbis::Status::ok) break;
}
void setup(const std::span<const std::uint8_t> bytes, const stx_vorbis::Limits& limits,
           std::pmr::memory_resource* const memory, const bool audio) {
    // Context is [u16 setup length][channels-1][block exponent pair][setup][audio].
    if (bytes.size() < 4) return;
    const std::size_t length = std::size_t{bytes[0]} | (std::size_t{bytes[1]} << 8);
    if (length > bytes.size() - 4) return;
    stx_vorbis::detail::Setup configuration(memory);
    const unsigned int small = 6 + bytes[3] % 8;
    const unsigned int large = std::max(small, 6U + (bytes[3] >> 4) % 8);
    configuration.identification = stx_vorbis::detail::Identification{48000, 1U + bytes[2] % 8,
        {1U << small, 1U << large}};
    const stx_vorbis::Synthesis synthesis = (bytes[2] & 128U) == 0
        ? stx_vorbis::Synthesis::automatic : stx_vorbis::Synthesis::scalar;
    stx_vorbis::detail::parse_setup(configuration, bytes.subspan(4, length), limits, memory, synthesis);
    if (!audio) return;
    stx_vorbis::detail::Workspace workspace(memory);
    stx_vorbis::detail::prepare_workspace(workspace, configuration, limits);
    stx_vorbis::detail::decode_packet(workspace, configuration, bytes.subspan(4 + length));
}
void ogg(const std::span<const std::uint8_t> bytes, const stx_vorbis::Limits& limits) {
    stx_vorbis::OggDemuxer demux(limits, stx_vorbis::Recovery::resynchronize);
    std::size_t position = 0;
    for (std::size_t step = 0; step <= bytes.size() * 2 + 1024; ++step) {
        stx_vorbis::OggPacket packet{};
        const stx_vorbis::Status status = demux.next_packet(packet);
        if (status == stx_vorbis::Status::need_input) {
            const std::size_t count = std::min<std::size_t>(97, bytes.size() - position);
            const stx_vorbis::FeedResult result = demux.push(bytes.subspan(position, count), position + count == bytes.size());
            position += result.accepted;
        } else if (status == stx_vorbis::Status::packet) {
            if (packet.bytes.size() > limits.packet_bytes) std::abort();
        } else if (status == stx_vorbis::Status::event) {
            const stx_vorbis::OggStreamEnd ended = demux.stream_end_event(); (void)ended;
        } else if (status == stx_vorbis::Status::end || !demux.diagnostic().recoverable) break;
    }
}
void decoder(const std::span<const std::uint8_t> bytes, const stx_vorbis::Limits& limits) {
    stx_vorbis::Decoder decoder(limits, stx_vorbis::Recovery::resynchronize);
    std::size_t position = 0;
    for (std::size_t step = 0; step <= bytes.size() * 2 + 1024; ++step) {
        const stx_vorbis::Status status = decoder.advance();
        if (status == stx_vorbis::Status::need_input) {
            const std::size_t count = std::min<std::size_t>(97, bytes.size() - position);
            const stx_vorbis::FeedResult result = decoder.push(bytes.subspan(position, count), position + count == bytes.size());
            position += result.accepted;
        } else if (status == stx_vorbis::Status::pcm) {
            const stx_vorbis::Status consumed = decoder.consume(decoder.output().frames); (void)consumed;
        } else if (status == stx_vorbis::Status::event) decoder.acknowledge_event();
        else break;
    }
}
}
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* const data, const std::size_t size) {
    if (size > 1024 * 1024) return 0;
    const std::span<const std::uint8_t> bytes(data, size);
    const stx_vorbis::Limits limits = bounded_limits();
    stx_vorbis::detail::Memory memory(limits.memory_bytes, nullptr);
    try {
#if STX_FUZZ_STAGE == 1
        bits(bytes);
#elif STX_FUZZ_STAGE == 2
        ogg(bytes, limits);
#elif STX_FUZZ_STAGE == 3
        huffman(bytes, &memory);
#elif STX_FUZZ_STAGE == 4
        setup(bytes, limits, &memory, false);
#elif STX_FUZZ_STAGE == 5
        setup(bytes, limits, &memory, true);
#else
        decoder(bytes, limits);
#endif
    } catch (const stx_vorbis::detail::DecodeFailure&) {}
    catch (const std::bad_alloc&) {}
    catch (const std::length_error&) {}
    return 0;
}
