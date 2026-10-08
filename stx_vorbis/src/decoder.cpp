#include "stx_vorbis/decoder.hpp"
#include "packet.hpp"
#include <cmath>
#include <cstring>
#include <string>
namespace stx_vorbis {
namespace {
unsigned char ascii_upper(const unsigned char byte) noexcept {
    const unsigned char result = byte >= 'a' && byte <= 'z' ? static_cast<unsigned char>(byte - ('a' - 'A')) : byte;
    return result;
}
}
struct Decoder::State final {
    State(const Limits& configuration, const Recovery policy, const Synthesis implementation,
          std::pmr::memory_resource* const upstream)
        : limits(configuration), recovery(policy), synthesis(implementation), memory(configuration.memory_bytes - sizeof(State), upstream),
          demux(configuration, policy, &memory), workspace(&memory), vendor(&memory), comments(&memory) {}
    Limits limits;
    Recovery recovery;
    Synthesis synthesis;
    detail::Memory memory;
    OggDemuxer demux;
    detail::SetupOwner setup{nullptr, detail::SetupDeleter{}};
    detail::Identification identification{};
    detail::Workspace workspace;
    std::pmr::string vendor;
    std::pmr::vector<std::pmr::string> comments;
    StreamInfo info{};
    Diagnostic error{};
    Event pending_event{};
    unsigned int header{0};
    unsigned int next_chain{0};
    unsigned int consumed{0};
    bool active{false};
    bool event_pending{false};
    bool end_pending{false};
    bool failed{false};
    bool anchored{false};
    std::uint64_t leading_discard{0};
    std::uint64_t checked_end_page{UINT64_MAX};
    std::size_t setup_bytes{0};
    std::size_t workspace_bytes{0};

    MemoryReport report_memory() const noexcept {
        return MemoryReport{setup_bytes, workspace_bytes, memory.current + sizeof(State), memory.peak + sizeof(State)};
    }
    Status fail(const Status status, const OggPacket& packet, const std::size_t bit = 0) noexcept {
        error = Diagnostic{status, packet.byte_offset, packet.index, packet.serial, packet.sequence, bit, false};
        failed = true;
        return status;
    }
    void publish(const EventKind kind) noexcept {
        info.memory = report_memory();
        pending_event = Event{kind, info, error};
        event_pending = true;
    }
    std::uint32_t read_length(BitReader& reader) {
        std::uint32_t value = 0;
        detail::require(reader.read(32, value), Status::invalid_header, reader.position());
        return value;
    }
    void read_string(std::pmr::string& destination, BitReader& reader, const std::span<const std::uint8_t> data,
                     std::size_t& used) {
        const std::size_t size = read_length(reader);
        detail::require(size <= limits.metadata_bytes - used, Status::resource_limit);
        detail::require(size <= reader.remaining() / 8);
        const std::size_t position = reader.position() / 8;
        destination.assign(reinterpret_cast<const char*>(data.data() + position), size);
        const bool skipped = reader.skip(size * 8); (void)skipped;
        used += size;
    }
    void parse_comments(const std::span<const std::uint8_t> packet) {
        detail::validate_header(packet, 3);
        const std::span<const std::uint8_t> bytes = packet.subspan(7);
        BitReader reader(bytes);
        std::size_t used = 0;
        read_string(vendor, reader, bytes, used);
        const std::uint32_t count = read_length(reader);
        detail::require(count <= limits.comments, Status::resource_limit);
        comments.clear(); comments.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            comments.emplace_back(); read_string(comments.back(), reader, bytes, used);
        }
        std::uint32_t framing = 0;
        detail::require(reader.read(1, framing) && framing == 1);
    }
    unsigned int packet_block(const std::span<const std::uint8_t> bytes) const {
        BitReader reader(bytes);
        std::uint32_t audio = 0; std::uint32_t mode = 0;
        detail::require(reader.read(1, audio) && audio == 0, Status::invalid_packet);
        const detail::Setup& configuration = *setup;
        const unsigned int bits = detail::ilog(static_cast<std::uint32_t>(configuration.modes.size() - 1));
        detail::require(reader.read(bits, mode) && mode < configuration.modes.size(), Status::invalid_packet);
        return configuration.identification.blocks[configuration.modes[mode].large ? 1 : 0];
    }
    void anchor_page(const OggPacket& packet) {
        if (!packet.page_has_granule || (anchored && !packet.page_ending)) return;
        detail::require(packet.granule <= INT64_MAX, Status::invalid_packet);
        if (packet.page_ending && checked_end_page == packet.byte_offset) return;
        unsigned int previous = workspace.previous_block;
        unsigned int block = packet_block(packet.bytes);
        unsigned int last_span = previous == 0 ? 0 : (previous + block) / 4;
        std::uint64_t available = last_span;
        previous = block;
        std::size_t start = 0; std::size_t end = 0;
        for (const std::uint8_t lace : packet.following_lacing) {
            end += lace;
            if (lace == 255) continue;
            block = packet_block(packet.following_bytes.subspan(start, end - start));
            last_span = (previous + block) / 4;
            available += last_span;
            previous = block; start = end;
        }
        if (packet.page_ending) {
            detail::require(packet.granule >= info.granule_origin, Status::invalid_packet);
            const std::uint64_t total = packet.granule - info.granule_origin;
            if (info.position_known) {
                const std::uint64_t possible = info.next_sample + available;
                detail::require(total <= possible && total >= possible - last_span, Status::invalid_packet);
            }
            info.total_frames = total; info.total_known = true;
            checked_end_page = packet.byte_offset;
            anchored = true;
        } else if (available != 0) {
            // Vorbis I appendix A.2: negative starts discard PCM; positive starts
            // are broadcast offsets. Public sample coordinates remain relative.
            if (packet.granule < available && info.next_sample == 0) leading_discard = available - packet.granule;
            else if (packet.granule > available && info.next_sample == 0) info.granule_origin = packet.granule - available;
            anchored = true;
        }
    }
    Status advance() {
        if (failed) return error.code;
        if (event_pending) return Status::event;
        if (consumed < workspace.frames) return Status::pcm;
        if (end_pending) {
            end_pending = false; active = false; header = 0;
            publish(EventKind::stream_end); return Status::event;
        }
        OggPacket packet{};
        while (true) {
            const Status status = demux.next_packet(packet);
            if (status != Status::packet) {
                if (status == Status::end) {
                    if (active) return fail(Status::truncated, packet);
                    return Status::end;
                }
                if (status == Status::need_input) return status;
                error = demux.diagnostic();
                if (error.recoverable) {
                    detail::reset_overlap(workspace); consumed = 0; info.timeline_discontinuous = true; info.position_known = false;
                    publish(EventKind::diagnostic); return Status::event;
                }
                failed = true; return status;
            }
            try {
                if (!active) {
                    // Other logical codecs are ignored; this core owns one Vorbis
                    // logical stream at a time, while the demuxer handles multiplexing.
                    if (!packet.beginning || packet.bytes.size() < 7 || packet.bytes[0] != 1
                        || std::memcmp(packet.bytes.data() + 1, "vorbis", 6) != 0) continue;
                    detail::require(next_chain < limits.chained_streams, Status::resource_limit);
                    identification = detail::parse_identification(packet.bytes, limits);
                    info = StreamInfo{.chain_index = next_chain, .serial = packet.serial,
                        .sample_rate = identification.rate, .channels = identification.channels,
                        .small_block = identification.blocks[0], .large_block = identification.blocks[1]};
                    ++next_chain;
                    setup.reset(); vendor.clear(); comments.clear(); detail::reset_overlap(workspace);
                    setup_bytes = 0; workspace_bytes = 0; consumed = 0; anchored = false; leading_discard = 0; checked_end_page = UINT64_MAX;
                    active = true; header = 1;
                    detail::require(!packet.ending);
                    continue;
                }
                if (packet.serial != info.serial) {
                    // Concurrent Vorbis streams need separate decoders, not accidental chaining.
                    if (packet.beginning && packet.bytes.size() >= 7 && packet.bytes[0] == 1
                        && std::memcmp(packet.bytes.data() + 1, "vorbis", 6) == 0)
                        return fail(Status::unsupported, packet);
                    continue;
                }
                if (header == 1) {
                    parse_comments(packet.bytes); header = 2; detail::require(!packet.ending); continue;
                }
                if (header == 2) {
                    const std::size_t before = memory.current;
                    void* const storage = memory.allocate(sizeof(detail::Setup), alignof(detail::Setup));
                    detail::Setup* prepared_pointer = nullptr;
                    try { prepared_pointer = std::construct_at(static_cast<detail::Setup*>(storage), &memory); }
                    catch (...) { memory.deallocate(storage, sizeof(detail::Setup), alignof(detail::Setup)); throw; }
                    std::unique_ptr<detail::Setup, detail::SetupDeleter> prepared(prepared_pointer, detail::SetupDeleter{&memory});
                    (*prepared).identification = identification;
                    detail::parse_setup(*prepared, packet.bytes, limits, &memory);
                    setup_bytes = memory.current - before;

                    detail::prepare_workspace(workspace, *prepared, limits, synthesis);
                    workspace_bytes = (workspace.spectrum.capacity() + workspace.floor_curve.capacity()
                        + workspace.time.capacity() + workspace.previous.capacity() + workspace.real.capacity()
                        + workspace.imaginary.capacity()) * sizeof(double)
                        + workspace.pcm.capacity() * sizeof(float) + workspace.classifications.capacity() * sizeof(unsigned int);
                    info.codebooks = static_cast<std::uint32_t>((*prepared).books.size());
                    info.floors = static_cast<std::uint32_t>((*prepared).floors.size());
                    info.residues = static_cast<std::uint32_t>((*prepared).residues.size());
                    info.mappings = static_cast<std::uint32_t>((*prepared).mappings.size());
                    info.modes = static_cast<std::uint32_t>((*prepared).modes.size());
                    setup = std::move(prepared); header = 3;
                    detail::require(!packet.ending);
                    publish(EventKind::stream_begin); return Status::event;
                }
                if (packet.discontinuity) { detail::reset_overlap(workspace); consumed = 0; info.timeline_discontinuous = true; info.position_known = false; }
                anchor_page(packet);
                detail::decode_packet(workspace, *setup, packet.bytes);
                consumed = static_cast<unsigned int>(std::min<std::uint64_t>(leading_discard, workspace.frames));
                leading_discard -= consumed;
                if (info.total_known) {
                    detail::require(info.total_frames >= info.next_sample, Status::invalid_packet);
                    const std::uint64_t remaining = info.total_frames - info.next_sample;
                    if (remaining < workspace.frames - consumed) workspace.frames = consumed + static_cast<unsigned int>(remaining);
                }
                if (packet.has_granule) {
                    detail::require(packet.granule >= info.granule_origin, Status::invalid_packet);
                    const std::uint64_t local_granule = packet.granule - info.granule_origin;
                    if (!info.position_known) {
                        if (local_granule < workspace.frames) workspace.frames = static_cast<unsigned int>(local_granule);
                        info.next_sample = local_granule - workspace.frames;
                        info.position_known = true;
                    }
                    if (packet.ending) {
                        detail::require(local_granule >= info.next_sample, Status::invalid_packet);
                        const std::uint64_t remaining = local_granule - info.next_sample;
                        if (remaining < workspace.frames - consumed) workspace.frames = consumed + static_cast<unsigned int>(remaining);
                        else detail::require(remaining == workspace.frames - consumed || info.timeline_discontinuous, Status::invalid_packet);
                        info.total_frames = local_granule; info.total_known = true;
                    }
                }
                if (packet.ending) end_pending = true;
                if (workspace.frames > consumed) return Status::pcm;
                if (end_pending) { end_pending = false; active = false; header = 0; publish(EventKind::stream_end); return Status::event; }
            } catch (const detail::DecodeFailure& failure) {
                if (header == 3 && recovery == Recovery::resynchronize && failure.status != Status::resource_limit) {
                    error = Diagnostic{failure.status, packet.byte_offset, packet.index, packet.serial, packet.sequence, failure.bit, true};
                    detail::reset_overlap(workspace); consumed = 0; info.timeline_discontinuous = true; info.position_known = false;
                    if (packet.ending) end_pending = true;
                    publish(EventKind::diagnostic); return Status::event;
                }
                return fail(failure.status, packet, failure.bit);
            }
        }
    }
};
Decoder::Decoder(const Limits limits, const Recovery recovery, const Synthesis synthesis, std::pmr::memory_resource* const memory)
{
    if (limits.memory_bytes < sizeof(State)) throw std::bad_alloc();
    std::pmr::memory_resource* const allocator = memory == nullptr ? std::pmr::new_delete_resource() : memory;
    void* const storage = (*allocator).allocate(sizeof(State), alignof(State));
    try {
        State* const state = std::construct_at(static_cast<State*>(storage), limits, recovery, synthesis, memory);
        state_ = std::unique_ptr<State, StateDeleter>(state, StateDeleter{allocator});
    } catch (...) { (*allocator).deallocate(storage, sizeof(State), alignof(State)); throw; }
}
void Decoder::StateDeleter::operator()(State* const state) const noexcept {
    std::destroy_at(state); (*memory).deallocate(state, sizeof(State), alignof(State));
}
Decoder::~Decoder() = default;
FeedResult Decoder::push(const std::span<const std::uint8_t> bytes, const bool final) noexcept {
    State& state = *state_;
    if (state.failed) return FeedResult{state.error.code, 0};
    return state.demux.push(bytes, final);
}
Status Decoder::advance() noexcept {
    State& state = *state_;
    try { return state.advance(); }
    catch (const std::bad_alloc&) {
        const Status status = state.memory.limited ? Status::resource_limit : Status::allocation_failed;
        return state.fail(status, OggPacket{});
    }
    catch (const std::length_error&) { return state.fail(Status::resource_limit, OggPacket{}); }
}
PcmView Decoder::output() const noexcept {
    const State& state = *state_;
    if (state.consumed >= state.workspace.frames || state.failed || state.event_pending) return PcmView{};
    return PcmView{state.workspace.pcm.data() + state.consumed, state.workspace.stride,
        state.info.channels, state.workspace.frames - state.consumed, state.info.sample_rate,
        state.info.chain_index, state.info.next_sample};
}
Status Decoder::consume(const std::uint32_t frames) noexcept {
    State& state = *state_;
    if (state.consumed > state.workspace.frames || frames > state.workspace.frames - state.consumed || state.event_pending || state.failed) return Status::invalid_argument;
    if (state.info.next_sample > UINT64_MAX - frames || state.info.emitted_frames > UINT64_MAX - frames) return Status::resource_limit;
    state.consumed += frames; state.info.next_sample += frames; state.info.emitted_frames += frames;
    return Status::ok;
}
Event Decoder::event() const noexcept { return (*state_).pending_event; }
void Decoder::acknowledge_event() noexcept { (*state_).event_pending = false; }
StreamInfo Decoder::info() const noexcept {
    StreamInfo info = (*state_).info;
    info.memory = (*state_).report_memory();
    return info;
}
Diagnostic Decoder::diagnostic() const noexcept { return (*state_).error; }
std::string_view Decoder::vendor() const noexcept { return (*state_).vendor; }
std::size_t Decoder::comment_count() const noexcept { return (*state_).comments.size(); }
std::string_view Decoder::comment(const std::size_t index) const noexcept {
    if (index >= (*state_).comments.size()) return {};
    return (*state_).comments[index];
}
std::string_view Decoder::tag(const std::string_view key, const std::size_t occurrence) const noexcept {
    std::size_t found = 0;
    for (const std::pmr::string& value : (*state_).comments) {
        if (value.size() <= key.size() || value[key.size()] != '=') continue;
        bool equal = true;
        for (std::size_t index = 0; index < key.size(); ++index) {
            if (ascii_upper(static_cast<unsigned char>(value[index])) != ascii_upper(static_cast<unsigned char>(key[index]))) { equal = false; break; }
        }
        if (!equal) continue;
        if (found == occurrence) return std::string_view(value).substr(key.size() + 1);
        ++found;
    }
    return {};
}
std::uint64_t Decoder::byte_offset() const noexcept { return (*state_).demux.byte_offset(); }
Status Decoder::discontinuity() noexcept {
    State& state = *state_;
    if (state.recovery != Recovery::resynchronize || !state.setup) return Status::invalid_argument;
    state.demux.reset(); detail::reset_overlap(state.workspace);
    state.consumed = 0; state.event_pending = false; state.end_pending = false; state.failed = false;
    state.active = true; state.header = 3; state.anchored = true; state.leading_discard = 0;
    state.info.total_known = false; state.checked_end_page = UINT64_MAX; state.error = Diagnostic{}; state.info.timeline_discontinuous = true; state.info.position_known = false;
    return Status::ok;
}
void Decoder::reset() noexcept {
    State& state = *state_;
    state.demux.reset(); state.setup.reset(); detail::reset_overlap(state.workspace);
    state.vendor.clear(); state.comments.clear(); state.info = StreamInfo{}; state.error = Diagnostic{};
    state.header = 0; state.next_chain = 0; state.consumed = 0; state.active = false;
    state.event_pending = false; state.end_pending = false; state.failed = false;
    state.setup_bytes = 0; state.workspace_bytes = 0; state.anchored = false; state.leading_discard = 0; state.checked_end_page = UINT64_MAX;
}
std::span<const float> PcmView::channel(const std::uint32_t index) const noexcept {
    if (index >= channels || channels > 255 || stride > 4096 || frames > stride || samples == nullptr) return {};
    return std::span<const float>(samples + std::size_t{index} * stride, frames);
}
namespace {
std::int16_t to_int16(const float sample) noexcept {
    if (std::isnan(sample)) return 0;
    if (sample >= 32767.0F / 32768.0F) return 32767;
    if (sample <= -1) return -32768;
    const double rounded = std::round(static_cast<double>(sample) * 32768);
    return static_cast<std::int16_t>(rounded);
}
bool valid_view(const PcmView source) noexcept {
    const bool valid = source.channels <= 255 && source.stride <= 4096 && source.frames <= source.stride
        && (source.frames == 0 || (source.samples != nullptr && source.channels != 0));
    return valid;
}
}
Status copy_interleaved(const PcmView source, const std::span<float> destination) noexcept {
    if (!valid_view(source) || source.frames > destination.size() / std::max(source.channels, 1U)) return Status::invalid_argument;
    for (unsigned int channel = 0; channel < source.channels; ++channel) {
        const std::span<const float> input = source.channel(channel);
        for (unsigned int frame = 0; frame < source.frames; ++frame) destination[std::size_t{frame} * source.channels + channel] = input[frame];
    }
    return Status::ok;
}
Status copy_interleaved(const PcmView source, const std::span<std::int16_t> destination) noexcept {
    if (!valid_view(source) || source.frames > destination.size() / std::max(source.channels, 1U)) return Status::invalid_argument;
    for (unsigned int channel = 0; channel < source.channels; ++channel) {
        const std::span<const float> input = source.channel(channel);
        for (unsigned int frame = 0; frame < source.frames; ++frame) destination[std::size_t{frame} * source.channels + channel] = to_int16(input[frame]);
    }
    return Status::ok;
}
Status copy_planar(const PcmView source, const std::span<std::span<float>> destination) noexcept {
    if (!valid_view(source) || destination.size() < source.channels) return Status::invalid_argument;
    for (unsigned int channel = 0; channel < source.channels; ++channel)
        if (destination[channel].size() < source.frames) return Status::invalid_argument;
    for (unsigned int channel = 0; channel < source.channels; ++channel) {
        const std::span<const float> input = source.channel(channel);
        std::copy(input.begin(), input.end(), destination[channel].begin());
    }
    return Status::ok;
}
Status copy_planar(const PcmView source, const std::span<std::span<std::int16_t>> destination) noexcept {
    if (!valid_view(source) || destination.size() < source.channels) return Status::invalid_argument;
    for (unsigned int channel = 0; channel < source.channels; ++channel)
        if (destination[channel].size() < source.frames) return Status::invalid_argument;
    for (unsigned int channel = 0; channel < source.channels; ++channel) {
        const std::span<const float> input = source.channel(channel);
        for (unsigned int frame = 0; frame < source.frames; ++frame) destination[channel][frame] = to_int16(input[frame]);
    }
    return Status::ok;
}
} // namespace stx_vorbis
