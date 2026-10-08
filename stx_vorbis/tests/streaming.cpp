#include "stx_vorbis/source.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace {
void check(const bool condition, const char* const message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}
std::vector<std::uint8_t> read_file(const char* const path) {
    std::FILE* const file = std::fopen(path, "rb"); check(file != nullptr, "open fixture");
    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t, 32768> buffer{};
    while (true) {
        const std::size_t count = std::fread(buffer.data(), 1, buffer.size(), file);
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count));
        if (count < buffer.size()) break;
    }
    check(std::ferror(file) == 0, "read fixture"); std::fclose(file);
    return bytes;
}
struct Decoded final { std::vector<float> samples{}; std::vector<stx_vorbis::StreamInfo> streams{}; };
Decoded decode(const std::span<const std::uint8_t> bytes, const std::size_t chunk, const stx_vorbis::Synthesis synthesis) {
    stx_vorbis::Decoder decoder({}, stx_vorbis::Recovery::strict, synthesis);
    std::size_t position = 0;
    bool final = false;
    Decoded result{};
    while (true) {
        const stx_vorbis::Status status = decoder.advance();
        if (status == stx_vorbis::Status::end) break;
        if (status == stx_vorbis::Status::need_input) {
            check(!final, "no input after final");
            const std::size_t count = std::min(chunk, bytes.size() - position);
            const stx_vorbis::FeedResult fed = decoder.push(bytes.subspan(position, count), position + count == bytes.size());
            check(fed.accepted <= count, "accepted bounded");
            check(fed.status == stx_vorbis::Status::ok || fed.status == stx_vorbis::Status::need_output, "feed status");
            position += fed.accepted;
            final = position == bytes.size();
            continue;
        }
        if (status == stx_vorbis::Status::event) {
            const stx_vorbis::Event event = decoder.event();
            check(event.kind != stx_vorbis::EventKind::diagnostic, "unexpected diagnostic");
            check(decoder.advance() == stx_vorbis::Status::event, "event backpressure");
            if (event.kind == stx_vorbis::EventKind::stream_end) result.streams.push_back(event.stream);
            decoder.acknowledge_event(); continue;
        }
        if (status != stx_vorbis::Status::pcm) {
            const stx_vorbis::Diagnostic error = decoder.diagnostic();
            std::fprintf(stderr, "status=%s byte=%llu packet=%llu\n", stx_vorbis::status_name(status),
                static_cast<unsigned long long>(error.byte_offset), static_cast<unsigned long long>(error.packet_index));
            check(false, "decode status");
        }
        const stx_vorbis::PcmView pcm = decoder.output();
        check(decoder.advance() == stx_vorbis::Status::pcm, "PCM backpressure");
        check(decoder.output().samples == pcm.samples, "stable PCM borrow");
        const std::size_t offset = result.samples.size();
        result.samples.resize(offset + std::size_t{pcm.frames} * pcm.channels);
        check(stx_vorbis::copy_interleaved(pcm, std::span<float>(result.samples).subspan(offset)) == stx_vorbis::Status::ok, "copy output");
        const unsigned int first = pcm.frames / 2;
        check(decoder.consume(first) == stx_vorbis::Status::ok, "partial consumption");
        check(decoder.consume(pcm.frames - first) == stx_vorbis::Status::ok, "remaining consumption");
    }
    return result;
}
void seek_tests(const std::span<const std::uint8_t> bytes, const Decoded& expected) {
    stx_vorbis::MemorySource source(bytes); stx_vorbis::PullDecoder pull(source);
    std::size_t chain_offset = 0;
    for (const stx_vorbis::StreamInfo& info : expected.streams) {
        const std::uint64_t positions[]{0, 1, info.total_frames / 2, info.total_frames - 1};
        for (const std::uint64_t sample : positions) {
            check(pull.seek(stx_vorbis::SamplePosition{info.chain_index, sample}) == stx_vorbis::Status::ok, "seek exact");
            const stx_vorbis::PcmView pcm = pull.decoder().output();
            check(pcm.first_sample == sample && pcm.chain_index == info.chain_index, "seek position");
            for (unsigned int channel = 0; channel < pcm.channels; ++channel)
                check(pcm.channel(channel)[0] == expected.samples[chain_offset + sample * pcm.channels + channel], "seek PCM");
        }
        chain_offset += static_cast<std::size_t>(info.total_frames) * info.channels;
    }
}
}
int main(const int argc, char** const argv) {
    check(argc > 1, "fixture arguments required");
    std::vector<std::uint8_t> chain;
    std::vector<float> expected_chain;
    for (int index = 1; index < argc; ++index) {
        const std::vector<std::uint8_t> bytes = read_file(argv[index]);
        const Decoded reference = decode(bytes, bytes.size(), stx_vorbis::Synthesis::scalar);
        const std::size_t chunks[]{1, 7, 255, 1023, 4096};
        for (const std::size_t chunk : chunks) {
            const Decoded streamed = decode(bytes, chunk, stx_vorbis::Synthesis::automatic);
            check(streamed.samples == reference.samples, "arbitrary chunks and SIMD exact agreement");
        }
        seek_tests(bytes, reference);
        chain.insert(chain.end(), bytes.begin(), bytes.end());
        expected_chain.insert(expected_chain.end(), reference.samples.begin(), reference.samples.end());
    }
    const Decoded chained = decode(chain, 17, stx_vorbis::Synthesis::automatic);
    check(chained.samples == expected_chain, "chain PCM");
    check(chained.streams.size() == static_cast<std::size_t>(argc - 1), "chain events");
    seek_tests(chain, chained);
    std::puts("streaming, backpressure, chaining, format-change and seeking tests passed");
}
