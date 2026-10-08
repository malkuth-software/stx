#include <bfft/bodft.h>
#include <stx_vorbis/source.hpp>

int main(const int argc, char** const argv) {
    if (argc != 2) return 2;
    bodft_storage_requirements storage{};
    if (bodft_query_storage(128, BODFT_PRECISION_F64, &storage) != BFFT_OK) return 1;
    if (storage.plan_bytes == 0 || storage.workspace_bytes == 0) return 1;
    stx_vorbis::FileSource source;
    if (source.open(argv[1]) != stx_vorbis::Status::ok) return 1;
    stx_vorbis::PullDecoder pull(source);
    stx_vorbis::Decoder& decoder = pull.decoder();
    std::uint64_t frames = 0;
    while (true) {
        const stx_vorbis::Status status = pull.advance();
        if (status == stx_vorbis::Status::end) break;
        if (status == stx_vorbis::Status::event) {
            decoder.acknowledge_event();
            continue;
        }
        if (status != stx_vorbis::Status::pcm) return 1;
        const stx_vorbis::PcmView pcm = decoder.output();
        frames += pcm.frames;
        if (decoder.consume(pcm.frames) != stx_vorbis::Status::ok) return 1;
    }
    if (frames == 0) return 1;
    return 0;
}
