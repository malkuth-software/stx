// Untimed decoder-owned allocation accounting; this is not process RSS.
#include "stx_vorbis/source.hpp"
#include <algorithm>
#include <cstdio>

int main(const int argc, char** const argv) {
    if (argc != 2) return 2;
    stx_vorbis::FileSource source;
    if (source.open(argv[1]) != stx_vorbis::Status::ok) return 2;
    // Source outlives its borrower; PCM is consumed before advancing again.
    stx_vorbis::PullDecoder pull(source);
    stx_vorbis::Decoder& decoder = pull.decoder();
    stx_vorbis::MemoryReport maximum{};
    std::uint64_t frames = 0;
    while (true) {
        const stx_vorbis::Status status = pull.advance();
        const stx_vorbis::StreamInfo info = decoder.info();
        maximum.setup_bytes = std::max(maximum.setup_bytes, info.memory.setup_bytes);
        maximum.workspace_bytes = std::max(maximum.workspace_bytes, info.memory.workspace_bytes);
        maximum.current_bytes = std::max(maximum.current_bytes, info.memory.current_bytes);
        maximum.peak_bytes = std::max(maximum.peak_bytes, info.memory.peak_bytes);
        if (status == stx_vorbis::Status::end) break;
        if (status == stx_vorbis::Status::event) {
            decoder.acknowledge_event();
            continue;
        }
        if (status != stx_vorbis::Status::pcm) {
            std::fprintf(stderr, "%s\n", stx_vorbis::status_name(status));
            return 1;
        }
        const stx_vorbis::PcmView pcm = decoder.output();
        frames += pcm.frames;
        if (decoder.consume(pcm.frames) != stx_vorbis::Status::ok) return 1;
    }
    if (frames == 0) return 1;
    const int written = std::printf("frames,setup_bytes,workspace_bytes,max_current_bytes,peak_bytes\n"
        "%llu,%zu,%zu,%zu,%zu\n", static_cast<unsigned long long>(frames),
        maximum.setup_bytes, maximum.workspace_bytes, maximum.current_bytes, maximum.peak_bytes);
    if (written < 0 || std::fflush(stdout) != 0) return 1;
    return 0;
}
