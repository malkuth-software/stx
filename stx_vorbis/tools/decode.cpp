#include "stx_vorbis/source.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
int main(const int argc, char** const argv) {
    if (argc != 3 && argc != 4) return 2;
    stx_vorbis::Synthesis synthesis = stx_vorbis::Synthesis::automatic;
    if (argc == 4) {
        if (std::strcmp(argv[3], "--scalar") != 0) return 2;
        synthesis = stx_vorbis::Synthesis::scalar;
    }
    stx_vorbis::FileSource source;
    if (source.open(argv[1]) != stx_vorbis::Status::ok) return 2;
    stx_vorbis::PullDecoder pull(source, {}, stx_vorbis::Recovery::strict, synthesis);
    stx_vorbis::Decoder& decoder = pull.decoder();
    std::FILE* const output = std::fopen(argv[2], "wb");
    if (output == nullptr) return 2;
    std::vector<float> interleaved(255 * 4096, 0);
    std::uint64_t frames = 0;
    while (true) {
        const stx_vorbis::Status status = pull.advance();
        if (status == stx_vorbis::Status::end) break;
        if (status == stx_vorbis::Status::event) {
            const stx_vorbis::Event event = decoder.event();
            std::fprintf(stderr, "event=%u chain=%u rate=%u channels=%u frames=%llu memory=%zu\n",
                static_cast<unsigned int>(event.kind), event.stream.chain_index, event.stream.sample_rate, event.stream.channels,
                static_cast<unsigned long long>(event.stream.emitted_frames), event.stream.memory.current_bytes);
            decoder.acknowledge_event(); continue;
        }
        if (status != stx_vorbis::Status::pcm) {
            const stx_vorbis::Diagnostic error = decoder.diagnostic();
            std::fprintf(stderr, "%s at byte=%llu packet=%llu bit=%zu frames=%llu\n", stx_vorbis::status_name(status),
                static_cast<unsigned long long>(error.byte_offset), static_cast<unsigned long long>(error.packet_index),
                error.bit_offset, static_cast<unsigned long long>(frames));
            std::fclose(output); return 1;
        }
        const stx_vorbis::PcmView pcm = decoder.output();
        const stx_vorbis::Status copied = stx_vorbis::copy_interleaved(pcm, interleaved);
        if (copied != stx_vorbis::Status::ok) return 1;
        const std::size_t count = std::size_t{pcm.frames} * pcm.channels;
        if (std::fwrite(interleaved.data(), sizeof(float), count, output) != count) return 1;
        frames += pcm.frames;
        const stx_vorbis::Status consumed = decoder.consume(pcm.frames);
        if (consumed != stx_vorbis::Status::ok) return 1;
    }
    std::fclose(output);
    std::fprintf(stderr, "decoded %llu frames\n", static_cast<unsigned long long>(frames));
}
