#include "stx_vorbis/ogg.hpp"
#include "memory.hpp"
#include <array>
#include <cstring>
#include <limits>
#include <vector>
namespace stx_vorbis {
namespace {
constexpr std::size_t maximum_page = 65307;
std::uint32_t little32(const std::uint8_t* const bytes) noexcept {
    std::uint32_t value = 0;
    for (unsigned int index = 0; index < 4; ++index) value |= std::uint32_t{bytes[index]} << (8 * index);
    return value;
}
std::uint64_t little64(const std::uint8_t* const bytes) noexcept {
    std::uint64_t value = 0;
    for (unsigned int index = 0; index < 8; ++index) value |= std::uint64_t{bytes[index]} << (8 * index);
    return value;
}
struct LogicalStream final {
    explicit LogicalStream(std::pmr::memory_resource* const memory) : assembly(memory) {}
    std::pmr::vector<std::uint8_t> assembly;
    std::uint32_t serial{0};
    std::uint32_t expected_sequence{0};
    std::uint64_t packet_index{0};
    bool beginning{true};
    bool discontinuity{false};
    bool skip_continuation{false};
};
}
std::uint32_t ogg_crc(const std::span<const std::uint8_t> page) noexcept {
    std::uint32_t crc = 0;
    for (std::size_t index = 0; index < page.size(); ++index) {
        const std::uint8_t byte = index >= 22 && index < 26 ? 0 : page[index];
        crc ^= std::uint32_t{byte} << 24;
        for (unsigned int bit = 0; bit < 8; ++bit) {
            const bool high = (crc & 0x80000000U) != 0;
            crc <<= 1;
            if (high) crc ^= 0x04c11db7U;
        }
    }
    return crc;
}
struct OggDemuxer::State final {
    explicit State(const Limits& configuration, const Recovery policy, std::pmr::memory_resource* const upstream)
        : limits(configuration), recovery(policy), memory(configuration.memory_bytes - sizeof(State), upstream),
          input(&memory), streams(&memory), output(&memory) {
        if (limits.input_bytes < maximum_page || limits.logical_streams == 0)
            throw std::invalid_argument("Ogg input_bytes must hold one maximum page and logical_streams must be positive");
        input.reserve(limits.input_bytes);
        streams.reserve(limits.logical_streams);
    }
    Limits limits;
    Recovery recovery;
    detail::Memory memory;
    std::pmr::vector<std::uint8_t> input;
    std::pmr::vector<LogicalStream> streams;
    std::pmr::vector<std::uint8_t> output;
    Diagnostic error{};
    std::size_t cursor{0};
    std::uint64_t base_offset{0};
    std::size_t skipped{0};
    std::size_t page_size{0};
    std::size_t page_body{0};
    std::size_t stream_index{0};
    unsigned int segment{0};
    unsigned int segment_count{0};
    int final_packet_segment{-1};
    std::uint64_t granule{0};
    std::uint32_t sequence{0};
    bool page_eos{false};
    bool final_input{false};
    bool failed{false};
    bool finished{false};

    Status report(const Status status, const bool recoverable, const std::uint32_t serial = 0) noexcept {
        error = Diagnostic{.code = status, .byte_offset = base_offset + cursor,
                           .serial = serial, .page_sequence = sequence, .recoverable = recoverable};
        if (!recoverable) failed = true;
        return status;
    }
    void compact() noexcept {
        if (cursor == 0 || page_size != 0) return;
        const std::size_t remaining = input.size() - cursor;
        if (remaining != 0) std::memmove(input.data(), input.data() + cursor, remaining);
        input.resize(remaining);
        base_offset += cursor;
        cursor = 0;
    }
    Status bad_page(const Status status, const std::size_t advance) noexcept {
        const bool recoverable = recovery == Recovery::resynchronize && skipped < limits.resync_bytes;
        const Status result = report(status, recoverable);
        if (recoverable) {
            cursor += advance;
            skipped += advance;
            for (LogicalStream& stream : streams) {
                stream.assembly.clear();
                stream.discontinuity = true;
            }
        }
        return result;
    }
    Status begin_page() {
        const std::size_t available = input.size() - cursor;
        if (available < 27) {
            if (!final_input) return Status::need_input;
            if (available != 0) return report(Status::truncated, false);
            for (const LogicalStream& stream : streams) {
                if (!stream.assembly.empty()) return report(Status::truncated, false, stream.serial);
            }
            if (!streams.empty()) return report(Status::truncated, false);
            finished = true;
            return Status::end;
        }
        const std::uint8_t* const page = input.data() + cursor;
        if (std::memcmp(page, "OggS", 4) != 0 || page[4] != 0 || (page[5] & 0xf8U) != 0)
            return bad_page(Status::corrupt_page, 1);
        segment_count = page[26];
        const std::size_t header_size = 27 + segment_count;
        if (available < header_size) {
            if (final_input) return report(Status::truncated, false);
            return Status::need_input;
        }
        std::size_t size = header_size;
        final_packet_segment = -1;
        for (unsigned int index = 0; index < segment_count; ++index) {
            size += page[27 + index];
            if (page[27 + index] < 255) final_packet_segment = static_cast<int>(index);
        }
        if (available < size) {
            if (final_input) return report(Status::truncated, false);
            return Status::need_input;
        }
        if (ogg_crc(std::span<const std::uint8_t>(page, size)) != little32(page + 22))
            return bad_page(Status::corrupt_page, size);
        const std::uint32_t serial = little32(page + 14);
        sequence = little32(page + 18);
        const bool bos = (page[5] & 2U) != 0;
        const bool continued = (page[5] & 1U) != 0;
        stream_index = streams.size();
        for (std::size_t index = 0; index < streams.size(); ++index) {
            if (streams[index].serial == serial) { stream_index = index; break; }
        }
        bool hole = false;
        if (stream_index == streams.size()) {
            if (!bos && recovery == Recovery::strict) return report(Status::sequence_gap, false, serial);
            if (streams.size() >= limits.logical_streams) return report(Status::resource_limit, false, serial);
            streams.emplace_back(&memory);
            LogicalStream& added = streams.back();
            added.serial = serial;
            added.expected_sequence = sequence;
            added.beginning = bos;
            added.discontinuity = !bos;
            hole = !bos;
        } else if (bos) {
            return report(Status::corrupt_page, false, serial);
        }
        LogicalStream& stream = streams[stream_index];
        if (stream.expected_sequence != sequence) hole = true;
        if (continued != !stream.assembly.empty()) hole = true;
        if (bos && (continued || sequence != 0)) hole = true;
        if (hole && recovery == Recovery::strict) return report(Status::sequence_gap, false, serial);
        if (hole) {
            stream.assembly.clear();
            stream.skip_continuation = continued;
            stream.discontinuity = true;
        }
        stream.expected_sequence = sequence + 1U; // Ogg sequence numbers wrap modulo 2^32.
        page_size = size;
        page_body = header_size;
        segment = 0;
        granule = little64(page + 6);
        page_eos = (page[5] & 4U) != 0;
        skipped = 0;
        if (hole) return report(Status::sequence_gap, true, serial);
        return Status::ok;
    }
    Status emit_packet(OggPacket& packet, const std::span<const std::uint8_t> bytes, const unsigned int current_segment) noexcept {
        LogicalStream& stream = streams[stream_index];
        const std::uint8_t* const page = input.data() + cursor;
        packet.bytes = bytes;
        packet.byte_offset = base_offset + cursor;
        packet.serial = stream.serial;
        packet.sequence = sequence;
        packet.index = stream.packet_index;
        ++stream.packet_index;
        packet.beginning = stream.beginning;
        stream.beginning = false;
        packet.discontinuity = stream.discontinuity;
        stream.discontinuity = false;
        const bool last = static_cast<int>(current_segment) == final_packet_segment;
        packet.has_granule = last && granule != std::numeric_limits<std::uint64_t>::max();
        packet.granule = granule;
        packet.ending = last && page_eos;
        packet.following_lacing = std::span<const std::uint8_t>(page + 27 + segment, segment_count - segment);
        packet.following_bytes = std::span<const std::uint8_t>(page + page_body, page_size - page_body);
        packet.page_has_granule = granule != std::numeric_limits<std::uint64_t>::max();
        packet.page_ending = page_eos;
        return Status::packet;
    }
    Status next(OggPacket& packet) {
        packet = OggPacket{};
        output.clear();
        if (failed) return error.code;
        if (finished) return Status::end;
        while (true) {
            if (page_size == 0) {
                const Status status = begin_page();
                if (status != Status::ok) return status;
            }
            LogicalStream& stream = streams[stream_index];
            const std::uint8_t* const page = input.data() + cursor;
            while (segment < segment_count) {
                if (stream.assembly.empty() && !stream.skip_continuation) {
                    unsigned int end_segment = segment;
                    std::size_t bytes = 0;
                    while (end_segment < segment_count) {
                        const std::uint8_t lace = page[27 + end_segment];
                        bytes += lace;
                        ++end_segment;
                        if (lace < 255) break;
                    }
                    if (page[27 + end_segment - 1] < 255) {
                        if (bytes > limits.packet_bytes) return report(Status::resource_limit, false, stream.serial);
                        const std::span<const std::uint8_t> contiguous(page + page_body, bytes);
                        page_body += bytes; segment = end_segment;
                        return emit_packet(packet, contiguous, end_segment - 1);
                    }
                }
                const unsigned int current_segment = segment;
                const std::size_t count = page[27 + segment];
                ++segment;
                const std::size_t source = page_body;
                page_body += count;
                if (stream.skip_continuation) {
                    if (count < 255) stream.skip_continuation = false;
                    continue;
                }
                if (count > limits.packet_bytes - stream.assembly.size())
                    return report(Status::resource_limit, false, stream.serial);
                stream.assembly.insert(stream.assembly.end(), page + source, page + source + count);
                if (count == 255) continue;
                output.swap(stream.assembly);
                return emit_packet(packet, output, current_segment);
            }
            if (page_eos && !stream.assembly.empty()) return report(Status::truncated, false, stream.serial);
            cursor += page_size;
            page_size = 0;
            if (page_eos) streams.erase(streams.begin() + static_cast<std::ptrdiff_t>(stream_index));
            compact();
        }
    }
};
OggDemuxer::OggDemuxer(const Limits limits, const Recovery recovery, std::pmr::memory_resource* const memory)
{
    if (limits.memory_bytes < sizeof(State)) throw std::bad_alloc();
    std::pmr::memory_resource* const allocator = memory == nullptr ? std::pmr::new_delete_resource() : memory;
    void* const storage = (*allocator).allocate(sizeof(State), alignof(State));
    try {
        State* const state = std::construct_at(static_cast<State*>(storage), limits, recovery, memory);
        state_ = std::unique_ptr<State, StateDeleter>(state, StateDeleter{allocator});
    } catch (...) { (*allocator).deallocate(storage, sizeof(State), alignof(State)); throw; }
}
void OggDemuxer::StateDeleter::operator()(State* const state) const noexcept {
    std::destroy_at(state); (*memory).deallocate(state, sizeof(State), alignof(State));
}
OggDemuxer::~OggDemuxer() = default;
FeedResult OggDemuxer::push(const std::span<const std::uint8_t> bytes, const bool final) noexcept {
    State& state = *state_;
    if (state.failed) return FeedResult{state.error.code, 0};
    if (state.final_input) return FeedResult{Status::invalid_argument, 0};
    state.compact();
    const std::size_t count = std::min(bytes.size(), state.limits.input_bytes - state.input.size());
    // Capacity was acquired at construction, so this insertion cannot allocate.
    state.input.insert(state.input.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(count));
    if (final && count == bytes.size()) state.final_input = true;
    const Status status = count < bytes.size() ? Status::need_output : Status::ok;
    return FeedResult{status, count};
}
Status OggDemuxer::next_packet(OggPacket& packet) noexcept {
    State& state = *state_;
    try { return state.next(packet); }
    catch (const std::bad_alloc&) {
        packet = OggPacket{};
        const Status status = state.memory.limited ? Status::resource_limit : Status::allocation_failed;
        return state.report(status, false);
    }
    catch (const std::length_error&) { return state.report(Status::resource_limit, false); }
}
Diagnostic OggDemuxer::diagnostic() const noexcept { return (*state_).error; }
MemoryReport OggDemuxer::memory_report() const noexcept {
    const State& state = *state_;
    return MemoryReport{.current_bytes = state.memory.current + sizeof(State), .peak_bytes = state.memory.peak + sizeof(State)};
}
std::uint64_t OggDemuxer::byte_offset() const noexcept {
    const State& state = *state_;
    const std::uint64_t offset = state.base_offset + state.cursor + (state.page_size != 0 ? state.page_body : 0);
    return offset;
}
void OggDemuxer::reset() noexcept {
    State& state = *state_;
    state.input.clear(); state.streams.clear(); state.output.clear();
    state.error = Diagnostic{};
    state.cursor = 0; state.base_offset = 0; state.skipped = 0; state.page_size = 0;
    state.final_input = false; state.failed = false; state.finished = false;
}
} // namespace stx_vorbis
