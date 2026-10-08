#include "stx_vorbis/stb_compatible.h"
#include "stx_vorbis/source.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <vector>
static_assert(sizeof(short) == 2);
namespace {
using stx_vorbis::Status;
class SectionSource final : public stx_vorbis::Source {
public:
    explicit SectionSource(stx_vorbis::FileSource& file) noexcept : file_(file) {}
    std::uint64_t length{UINT64_MAX};
    std::uint64_t position{0};
    Status read(const std::span<std::uint8_t> destination, std::size_t& count) noexcept override {
        const std::size_t available = static_cast<std::size_t>(std::min<std::uint64_t>(destination.size(), length - position));
        if (available == 0) { count = 0; return Status::end; }
        const Status status = file_.read(destination.first(available), count);
        position += count;
        return status;
    }
    Status seek(const std::uint64_t offset) noexcept override {
        if (offset > length) return Status::invalid_argument;
        const Status status = file_.seek(offset);
        if (status == Status::ok) position = offset;
        return status;
    }
private:
    stx_vorbis::FileSource& file_;
};
int legacy_error(const Status status) noexcept {
    switch (status) {
    case Status::ok: case Status::pcm: case Status::event: case Status::end: return VORBIS__no_error;
    case Status::need_input: return VORBIS_need_more_data;
    case Status::allocation_failed: case Status::resource_limit: return VORBIS_outofmem;
    case Status::unsupported: return VORBIS_feature_not_supported;
    case Status::io_error: return VORBIS_file_open_failure;
    case Status::truncated: return VORBIS_unexpected_eof;
    case Status::invalid_header: return VORBIS_invalid_setup;
    default: return VORBIS_invalid_stream;
    }
}
unsigned int narrow_size(const std::size_t value) noexcept {
    const unsigned int result = static_cast<unsigned int>(std::min<std::size_t>(value, UINT32_MAX));
    return result;
}
}
struct stb_vorbis final {
    stb_vorbis(void* const buffer, const std::size_t size, const bool external)
        : externally_owned(external), arena(buffer, size, std::pmr::null_memory_resource()),
          pool(external ? &arena : std::pmr::new_delete_resource()),
          resource(external ? &pool : std::pmr::new_delete_resource()), section(file), comments(resource) {
        if (external) limits.memory_bytes = std::min(limits.memory_bytes, size);
    }
    bool externally_owned{false};
    std::pmr::monotonic_buffer_resource arena;
    std::pmr::unsynchronized_pool_resource pool;
    std::pmr::memory_resource* resource;
    stx_vorbis::Limits limits{};
    stx_vorbis::FileSource file{};
    SectionSource section;
    std::optional<stx_vorbis::MemorySource> memory{};
    std::optional<stx_vorbis::PullDecoder> pull{};
    std::optional<stx_vorbis::Decoder> push{};
    std::pmr::vector<char*> comments;
    std::array<float*, 255> planes{};
    int error{VORBIS__no_error};
    bool stopped{false};
    bool known_position{true};
    bool length_known{false};
    unsigned int length{0};
    stx_vorbis::Decoder& decoder() noexcept {
        if (push.has_value()) return *push;
        return (*pull).decoder();
    }
    Status next(const bool opening = false) noexcept {
        if (stopped) return Status::end;
        while (true) {
            const Status status = push.has_value() ? (*push).advance() : (*pull).advance();
            if (status != Status::event) {
                if (status != Status::pcm && status != Status::end && status != Status::need_input) error = legacy_error(status);
                return status;
            }
            stx_vorbis::Decoder& current = decoder();
            const stx_vorbis::Event event = current.event();
            current.acknowledge_event();
            if (event.kind == stx_vorbis::EventKind::stream_begin && opening) return Status::ok;
            if (event.kind == stx_vorbis::EventKind::stream_end) {
                stopped = true;
                if (event.stream.total_known && event.stream.total_frames <= UINT32_MAX) {
                    length = static_cast<unsigned int>(event.stream.total_frames); length_known = true;
                }
                return Status::end; // stb API is one logical stream; C++ exposes chains.
            }
            if (event.kind == stx_vorbis::EventKind::diagnostic) error = legacy_error(event.diagnostic.code);
        }
    }
};
namespace {
stb_vorbis* create(const stb_vorbis_alloc* const allocation) {
    if (allocation == nullptr) return new stb_vorbis(nullptr, 0, false);
    if ((*allocation).alloc_buffer == nullptr || (*allocation).alloc_buffer_length_in_bytes <= 0) throw std::bad_alloc();
    void* storage = (*allocation).alloc_buffer;
    std::size_t space = static_cast<std::size_t>((*allocation).alloc_buffer_length_in_bytes);
    if (std::align(alignof(stb_vorbis), sizeof(stb_vorbis), storage, space) == nullptr) throw std::bad_alloc();
    std::byte* const remaining = static_cast<std::byte*>(storage) + sizeof(stb_vorbis);
    const std::size_t bytes = space - sizeof(stb_vorbis);
    stb_vorbis* const result = std::construct_at(static_cast<stb_vorbis*>(storage), remaining, bytes, true);
    return result;
}
void set_error(int* const destination, const int error) noexcept { if (destination != nullptr) *destination = error; }
stb_vorbis* finish_open(stb_vorbis* const handle, int* const error) noexcept {
    const Status status = (*handle).next(true);
    if (status == Status::ok) { set_error(error, 0); return handle; }
    set_error(error, status == Status::end ? VORBIS_invalid_stream : legacy_error(status));
    stb_vorbis_close(handle); return nullptr;
}
short convert_short(const double value) noexcept {
    if (std::isnan(value)) return 0;
    if (value >= 32767.0 / 32768.0) return 32767;
    if (value <= -1) return -32768;
    const double rounded = std::round(value * 32768);
    return static_cast<short>(rounded);
}
double mixed_sample(const stx_vorbis::PcmView pcm, const unsigned int channel, const unsigned int channels,
                    const unsigned int frame, const bool coerce) noexcept {
    if (!coerce || channels == pcm.channels || channels > 2 || pcm.channels > 6) {
        if (channel >= pcm.channels) return 0;
        return pcm.channel(channel)[frame];
    }
    // Vorbis speaker order; center and LFE enter both sides, matching stb.
    constexpr std::array<std::array<unsigned int, 6>, 7> masks{{{0}, {3}, {1, 2}, {1, 3, 2}, {1, 2, 1, 2}, {1, 3, 2, 1, 2}, {1, 3, 2, 1, 2, 3}}};
    const unsigned int selected = channels == 1 ? 3 : 1U << channel;
    double sum = 0;
    for (unsigned int source = 0; source < pcm.channels; ++source)
        if ((masks[pcm.channels][source] & selected) != 0) sum += pcm.channel(source)[frame];
    return sum;
}
// Destination format/layout is selected once, outside each conversion kernel.
void float_interleaved(const stx_vorbis::PcmView pcm, float* const output, const unsigned int channels, const unsigned int frames) noexcept {
    for (unsigned int channel = 0; channel < channels; ++channel) {
        for (unsigned int frame = 0; frame < frames; ++frame) {
            const float sample = channel < pcm.channels ? pcm.channel(channel)[frame] : 0;
            output[std::size_t{frame} * channels + channel] = sample;
        }
    }
}
void short_interleaved(const stx_vorbis::PcmView pcm, short* const output, const unsigned int channels, const unsigned int frames) noexcept {
    for (unsigned int channel = 0; channel < channels; ++channel)
        for (unsigned int frame = 0; frame < frames; ++frame)
            output[std::size_t{frame} * channels + channel] = convert_short(mixed_sample(pcm, channel, channels, frame, true));
}
void float_planar(const stx_vorbis::PcmView pcm, float** const output, const unsigned int channels,
                  const unsigned int frames, const unsigned int offset) noexcept {
    for (unsigned int channel = 0; channel < channels; ++channel)
        for (unsigned int frame = 0; frame < frames; ++frame)
            output[channel][offset + frame] = channel < pcm.channels ? pcm.channel(channel)[frame] : 0;
}
void short_planar(const stx_vorbis::PcmView pcm, short** const output, const unsigned int channels,
                  const unsigned int frames, const unsigned int offset) noexcept {
    for (unsigned int channel = 0; channel < channels; ++channel)
        for (unsigned int frame = 0; frame < frames; ++frame)
            output[channel][offset + frame] = convert_short(mixed_sample(pcm, channel, channels, frame, true));
}
bool valid_output(stb_vorbis* const handle, const int channels, const void* const buffer, const int frames) noexcept {
    if (handle == nullptr) return false;
    if ((*handle).push.has_value()) { (*handle).error = VORBIS_invalid_api_mixing; return false; }
    if (channels < 1 || channels > 255 || frames < 0 || (frames != 0 && buffer == nullptr)) {
        (*handle).error = VORBIS_invalid_stream; return false;
    }
    return true;
}
int read_float(stb_vorbis& handle, const unsigned int channels, float* const interleaved, float** const planar,
               const unsigned int frames, const bool one_frame) noexcept {
    unsigned int written = 0;
    while (written < frames && handle.next() == Status::pcm) {
        stx_vorbis::Decoder& decoder = handle.decoder();
        const stx_vorbis::PcmView pcm = decoder.output();
        const unsigned int count = std::min(frames - written, pcm.frames);
        if (interleaved != nullptr) float_interleaved(pcm, interleaved + std::size_t{written} * channels, channels, count);
        else float_planar(pcm, planar, channels, count, written);
        const Status consumed = decoder.consume(one_frame ? pcm.frames : count); (void)consumed;
        written += count;
        if (one_frame) break;
    }
    return static_cast<int>(written);
}
int read_short(stb_vorbis& handle, const unsigned int channels, short* const interleaved, short** const planar,
               const unsigned int frames, const bool one_frame) noexcept {
    unsigned int written = 0;
    while (written < frames && handle.next() == Status::pcm) {
        stx_vorbis::Decoder& decoder = handle.decoder();
        const stx_vorbis::PcmView pcm = decoder.output();
        const unsigned int count = std::min(frames - written, pcm.frames);
        if (interleaved != nullptr) short_interleaved(pcm, interleaved + std::size_t{written} * channels, channels, count);
        else short_planar(pcm, planar, channels, count, written);
        const Status consumed = decoder.consume(one_frame ? pcm.frames : count); (void)consumed;
        written += count;
        if (one_frame) break;
    }
    return static_cast<int>(written);
}
int frame(stb_vorbis& handle, int* const channels, float*** const output) noexcept {
    if (handle.next() != Status::pcm) return 0;
    stx_vorbis::Decoder& decoder = handle.decoder();
    const stx_vorbis::PcmView pcm = decoder.output();
    for (unsigned int channel = 0; channel < pcm.channels; ++channel)
        handle.planes[channel] = const_cast<float*>(pcm.channel(channel).data());
    if (channels != nullptr) *channels = static_cast<int>(pcm.channels);
    if (output != nullptr) *output = handle.planes.data();
    const Status consumed = decoder.consume(pcm.frames); (void)consumed;
    return static_cast<int>(pcm.frames);
}
int decode_all(stb_vorbis* const handle, int* const channels, int* const sample_rate, short** const output) noexcept {
    if (handle == nullptr || output == nullptr) { stb_vorbis_close(handle); return -1; }
    *output = nullptr;
    const stb_vorbis_info info = stb_vorbis_get_info(handle);
    const std::size_t channel_count = static_cast<std::size_t>(info.channels);
    constexpr std::size_t maximum_bytes = 128 * 1024 * 1024;
    short* samples = nullptr;
    std::size_t frames = 0; std::size_t capacity = 0;
    while ((*handle).next() == Status::pcm) {
        const stx_vorbis::PcmView pcm = (*handle).decoder().output();
        if (pcm.frames > maximum_bytes / sizeof(short) / channel_count - frames) {
            std::free(samples); stb_vorbis_close(handle); return -1;
        }
        const std::size_t needed = frames + pcm.frames;
        if (needed > capacity) {
            capacity = std::min(std::max(needed, std::max<std::size_t>(4096, capacity * 2)), maximum_bytes / sizeof(short) / channel_count);
            void* const allocation = std::realloc(samples, capacity * channel_count * sizeof(short));
            if (allocation == nullptr) { std::free(samples); stb_vorbis_close(handle); return -1; }
            samples = static_cast<short*>(allocation);
        }
        short_interleaved(pcm, samples + frames * channel_count, static_cast<unsigned int>(channel_count), pcm.frames);
        const Status consumed = (*handle).decoder().consume(pcm.frames); (void)consumed;
        frames = needed;
    }
    const int error = (*handle).error;
    stb_vorbis_close(handle);
    if (error != 0) { std::free(samples); return -1; }
    *output = samples;
    if (channels != nullptr) *channels = info.channels;
    if (sample_rate != nullptr) *sample_rate = static_cast<int>(info.sample_rate);
    return static_cast<int>(frames);
}
}
extern "C" {
void stb_vorbis_close(stb_vorbis* const handle) {
    if (handle == nullptr) return;
    if ((*handle).externally_owned) std::destroy_at(handle);
    else delete handle;
}
stb_vorbis* stb_vorbis_open_memory(const unsigned char* const bytes, const int size, int* const error, const stb_vorbis_alloc* const allocation) {
    if (bytes == nullptr || size < 0) { set_error(error, VORBIS_invalid_stream); return nullptr; }
    stb_vorbis* handle = nullptr;
    try {
        handle = create(allocation);
        (*handle).memory.emplace(std::span<const std::uint8_t>(bytes, static_cast<std::size_t>(size)));
        (*handle).pull.emplace(*(*handle).memory, (*handle).limits, stx_vorbis::Recovery::strict, stx_vorbis::Synthesis::automatic, (*handle).resource);
        return finish_open(handle, error);
    } catch (...) { stb_vorbis_close(handle); set_error(error, VORBIS_outofmem); return nullptr; }
}
stb_vorbis* stb_vorbis_open_filename(const char* const path, int* const error, const stb_vorbis_alloc* const allocation) {
    stb_vorbis* handle = nullptr;
    try {
        handle = create(allocation);
        if ((*handle).file.open(path) != Status::ok) { stb_vorbis_close(handle); set_error(error, VORBIS_file_open_failure); return nullptr; }
        (*handle).pull.emplace((*handle).section, (*handle).limits, stx_vorbis::Recovery::strict, stx_vorbis::Synthesis::automatic, (*handle).resource);
        return finish_open(handle, error);
    } catch (...) { stb_vorbis_close(handle); set_error(error, VORBIS_outofmem); return nullptr; }
}
stb_vorbis* stb_vorbis_open_file_section(FILE* const file, const int close_on_close, int* const error,
                                       const stb_vorbis_alloc* const allocation, const unsigned int length) {
    stb_vorbis* handle = nullptr;
    try {
        handle = create(allocation);
        if ((*handle).file.attach(file, close_on_close != 0) != Status::ok) { stb_vorbis_close(handle); set_error(error, VORBIS_file_open_failure); return nullptr; }
        (*handle).section.length = length;
        (*handle).pull.emplace((*handle).section, (*handle).limits, stx_vorbis::Recovery::strict, stx_vorbis::Synthesis::automatic, (*handle).resource);
        return finish_open(handle, error);
    } catch (...) { stb_vorbis_close(handle); set_error(error, VORBIS_outofmem); return nullptr; }
}
stb_vorbis* stb_vorbis_open_file(FILE* const file, const int close_on_close, int* const error, const stb_vorbis_alloc* const allocation) {
    stb_vorbis* const handle = stb_vorbis_open_file_section(file, close_on_close, error, allocation, UINT32_MAX);
    if (handle != nullptr) (*handle).section.length = UINT64_MAX;
    return handle;
}
stb_vorbis_info stb_vorbis_get_info(stb_vorbis* const handle) {
    if (handle == nullptr) return {};
    const stx_vorbis::StreamInfo info = (*handle).decoder().info();
    return stb_vorbis_info{info.sample_rate, static_cast<int>(info.channels), narrow_size(info.memory.setup_bytes),
        narrow_size(info.memory.peak_bytes), narrow_size(info.memory.workspace_bytes), static_cast<int>(info.large_block / 2)};
}
stb_vorbis_comment stb_vorbis_get_comment(stb_vorbis* const handle) {
    if (handle == nullptr) return {};
    try {
        stx_vorbis::Decoder& decoder = (*handle).decoder();
        (*handle).comments.resize(decoder.comment_count());
        for (std::size_t index = 0; index < (*handle).comments.size(); ++index)
            (*handle).comments[index] = const_cast<char*>(decoder.comment(index).data());
        return stb_vorbis_comment{const_cast<char*>(decoder.vendor().data()), static_cast<int>((*handle).comments.size()), (*handle).comments.data()};
    } catch (...) { (*handle).error = VORBIS_outofmem; return {}; }
}
int stb_vorbis_get_error(stb_vorbis* const handle) {
    if (handle == nullptr) return VORBIS_invalid_stream;
    const int error = (*handle).error; (*handle).error = 0; return error;
}
int stb_vorbis_get_sample_offset(stb_vorbis* const handle) {
    if (handle == nullptr || !(*handle).decoder().info().position_known) return -1;
    const std::uint64_t offset = (*handle).decoder().info().next_sample;
    if (offset > INT32_MAX) return -1;
    return static_cast<int>(offset);
}
unsigned int stb_vorbis_get_file_offset(stb_vorbis* const handle) {
    if (handle == nullptr || (*handle).push.has_value()) return 0;
    const std::uint64_t offset = (*handle).decoder().byte_offset();
    if (offset > UINT32_MAX) return UINT32_MAX;
    return static_cast<unsigned int>(offset);
}
int stb_vorbis_get_frame_float(stb_vorbis* const handle, int* const channels, float*** const output) {
    if (handle == nullptr) return 0;
    if ((*handle).push.has_value()) { (*handle).error = VORBIS_invalid_api_mixing; return 0; }
    return frame(*handle, channels, output);
}
int stb_vorbis_get_samples_float_interleaved(stb_vorbis* const handle, const int channels, float* const buffer, const int floats) {
    if (!valid_output(handle, channels, buffer, floats)) return 0;
    return read_float(*handle, static_cast<unsigned int>(channels), buffer, nullptr, static_cast<unsigned int>(floats / channels), false);
}
int stb_vorbis_get_samples_short_interleaved(stb_vorbis* const handle, const int channels, short* const buffer, const int shorts) {
    if (!valid_output(handle, channels, buffer, shorts)) return 0;
    return read_short(*handle, static_cast<unsigned int>(channels), buffer, nullptr, static_cast<unsigned int>(shorts / channels), false);
}
int stb_vorbis_get_frame_short_interleaved(stb_vorbis* const handle, const int channels, short* const buffer, const int shorts) {
    if (!valid_output(handle, channels, buffer, shorts)) return 0;
    return read_short(*handle, static_cast<unsigned int>(channels), buffer, nullptr, static_cast<unsigned int>(shorts / channels), true);
}
int stb_vorbis_get_samples_float(stb_vorbis* const handle, const int channels, float** const buffer, const int frames) {
    if (!valid_output(handle, channels, buffer, frames)) return 0;
    if (frames == 0) return 0;
    for (int channel = 0; channel < channels; ++channel) if (buffer[channel] == nullptr) return 0;
    return read_float(*handle, static_cast<unsigned int>(channels), nullptr, buffer, static_cast<unsigned int>(frames), false);
}
int stb_vorbis_get_samples_short(stb_vorbis* const handle, const int channels, short** const buffer, const int frames) {
    if (!valid_output(handle, channels, buffer, frames)) return 0;
    if (frames == 0) return 0;
    for (int channel = 0; channel < channels; ++channel) if (buffer[channel] == nullptr) return 0;
    return read_short(*handle, static_cast<unsigned int>(channels), nullptr, buffer, static_cast<unsigned int>(frames), false);
}
int stb_vorbis_get_frame_short(stb_vorbis* const handle, const int channels, short** const buffer, const int frames) {
    if (!valid_output(handle, channels, buffer, frames)) return 0;
    if (frames == 0) return 0;
    for (int channel = 0; channel < channels; ++channel) if (buffer[channel] == nullptr) return 0;
    return read_short(*handle, static_cast<unsigned int>(channels), nullptr, buffer, static_cast<unsigned int>(frames), true);
}
int stb_vorbis_seek(stb_vorbis* const handle, const unsigned int sample) {
    if (handle == nullptr || !(*handle).pull.has_value()) return 0;
    const Status status = (*(*handle).pull).seek(stx_vorbis::SamplePosition{0, sample});
    (*handle).stopped = status == Status::end;
    if (status == Status::ok || status == Status::end) { (*handle).known_position = true; return 1; }
    (*handle).error = VORBIS_seek_failed; return 0;
}
int stb_vorbis_seek_frame(stb_vorbis* const handle, const unsigned int sample) { return stb_vorbis_seek(handle, sample); }
int stb_vorbis_seek_start(stb_vorbis* const handle) { return stb_vorbis_seek(handle, 0); }
unsigned int stb_vorbis_stream_length_in_samples(stb_vorbis* const handle) {
    if (handle == nullptr || !(*handle).pull.has_value()) return 0;
    if ((*handle).length_known) return (*handle).length;
    const std::uint64_t position = (*handle).decoder().info().next_sample;
    if (position > UINT32_MAX) { (*handle).error = VORBIS_feature_not_supported; return 0; }
    while ((*handle).next() == Status::pcm) {
        stx_vorbis::Decoder& decoder = (*handle).decoder();
        const Status consumed = decoder.consume(decoder.output().frames); (void)consumed;
    }
    const unsigned int length = (*handle).length;
    const int restored = stb_vorbis_seek(handle, static_cast<unsigned int>(position));
    if (restored == 0) return 0;
    return length;
}
float stb_vorbis_stream_length_in_seconds(stb_vorbis* const handle) {
    const unsigned int length = stb_vorbis_stream_length_in_samples(handle);
    const stb_vorbis_info info = stb_vorbis_get_info(handle);
    if (info.sample_rate == 0) return 0;
    const float seconds = static_cast<float>(length) / static_cast<float>(info.sample_rate);
    return seconds;
}
int stb_vorbis_decode_filename(const char* const path, int* const channels, int* const sample_rate, short** const output) {
    stb_vorbis* const handle = stb_vorbis_open_filename(path, nullptr, nullptr);
    return decode_all(handle, channels, sample_rate, output);
}
int stb_vorbis_decode_memory(const unsigned char* const bytes, const int size, int* const channels, int* const sample_rate, short** const output) {
    stb_vorbis* const handle = stb_vorbis_open_memory(bytes, size, nullptr, nullptr);
    return decode_all(handle, channels, sample_rate, output);
}
stb_vorbis* stb_vorbis_open_pushdata(const unsigned char* const bytes, const int size, int* const consumed,
                                   int* const error, const stb_vorbis_alloc* const allocation) {
    if (size < 0 || (size != 0 && bytes == nullptr)) { set_error(error, VORBIS_invalid_stream); return nullptr; }
    stb_vorbis* handle = nullptr;
    try {
        handle = create(allocation);
        (*handle).push.emplace((*handle).limits, stx_vorbis::Recovery::resynchronize, stx_vorbis::Synthesis::automatic, (*handle).resource);
        std::size_t accepted = 0;
        while (true) {
            const Status status = (*handle).next(true);
            if (status == Status::ok) { if (consumed != nullptr) *consumed = static_cast<int>(accepted); set_error(error, 0); return handle; }
            if (status != Status::need_input || accepted == static_cast<std::size_t>(size)) {
                set_error(error, legacy_error(status)); stb_vorbis_close(handle); return nullptr;
            }
            const stx_vorbis::FeedResult fed = (*(*handle).push).push(std::span<const std::uint8_t>(bytes + accepted, static_cast<std::size_t>(size) - accepted));
            accepted += fed.accepted;
            if (fed.accepted == 0) { set_error(error, VORBIS_outofmem); stb_vorbis_close(handle); return nullptr; }
        }
    } catch (...) { stb_vorbis_close(handle); set_error(error, VORBIS_outofmem); return nullptr; }
}
int stb_vorbis_decode_frame_pushdata(stb_vorbis* const handle, const unsigned char* const bytes, const int size,
                                    int* const channels, float*** const output, int* const samples) {
    if (samples != nullptr) *samples = 0;
    if (output != nullptr) *output = nullptr;
    if (handle == nullptr || size < 0 || (size != 0 && bytes == nullptr)) return 0;
    if (!(*handle).push.has_value()) { (*handle).error = VORBIS_invalid_api_mixing; return 0; }
    const stx_vorbis::FeedResult fed = (*(*handle).push).push(std::span<const std::uint8_t>(bytes, static_cast<std::size_t>(size)));
    if (fed.status != Status::ok && fed.status != Status::need_output) (*handle).error = legacy_error(fed.status);
    const int count = frame(*handle, channels, output);
    if (samples != nullptr) *samples = count;
    return static_cast<int>(fed.accepted);
}
void stb_vorbis_flush_pushdata(stb_vorbis* const handle) {
    if (handle == nullptr || !(*handle).push.has_value()) return;
    const Status status = (*(*handle).push).discontinuity();
    if (status != Status::ok) (*handle).error = legacy_error(status);
    (*handle).stopped = false; (*handle).known_position = false;
}
} // extern C
