// Test-only oracle/generator. Never linked into stx_vorbis.
#include <vorbis/vorbisfile.h>
#include <vorbis/vorbisenc.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <vector>
namespace {
int decode(const char* const path, const char* const output_path) {
    OggVorbis_File file{};
    if (ov_fopen(path, &file) != 0) return 1;
    std::FILE* const output = std::fopen(output_path, "wb");
    if (output == nullptr) { ov_clear(&file); return 1; }
    std::vector<float> interleaved(4096 * 255, 0);
    int link = 0;
    while (true) {
        float** pcm = nullptr;
        const long count = ov_read_float(&file, &pcm, 4096, &link);
        if (count == 0) break;
        if (count < 0) { std::fprintf(stderr, "libvorbis error %ld\n", count); ov_clear(&file); std::fclose(output); return 1; }
        const vorbis_info& info = *ov_info(&file, link);
        for (long sample = 0; sample < count; ++sample)
            for (int channel = 0; channel < info.channels; ++channel)
                interleaved[static_cast<std::size_t>(sample * info.channels + channel)] = pcm[channel][sample];
        const std::size_t size = static_cast<std::size_t>(count * info.channels);
        if (std::fwrite(interleaved.data(), sizeof(float), size, output) != size) { ov_clear(&file); std::fclose(output); return 1; }
    }
    ov_clear(&file); std::fclose(output);
    return 0;
}
bool write_page(std::FILE* const output, const ogg_page& page) {
    const std::size_t header = static_cast<std::size_t>(page.header_len);
    const std::size_t body = static_cast<std::size_t>(page.body_len);
    const bool written = std::fwrite(page.header, 1, header, output) == header && std::fwrite(page.body, 1, body, output) == body;
    return written;
}
int encode(const char* const path, const int channels, const int rate, const int frames, const float quality) {
    if (channels < 1 || channels > 8 || rate < 1 || frames < 1) return 2;
    vorbis_info info{}; vorbis_info_init(&info);
    if (vorbis_encode_init_vbr(&info, channels, rate, quality) != 0) { vorbis_info_clear(&info); return 1; }
    vorbis_comment comments{}; vorbis_comment_init(&comments);
    vorbis_comment_add_tag(&comments, "TITLE", "deterministic stx_vorbis fixture");
    vorbis_comment_add_tag(&comments, "ARTIST", "test signal");
    vorbis_dsp_state dsp{}; vorbis_block block{}; ogg_stream_state stream{};
    vorbis_analysis_init(&dsp, &info); vorbis_block_init(&dsp, &block); ogg_stream_init(&stream, 0x535458 + channels + rate);
    ogg_packet identification{}; ogg_packet metadata{}; ogg_packet setup{};
    vorbis_analysis_headerout(&dsp, &comments, &identification, &metadata, &setup);
    ogg_stream_packetin(&stream, &identification); ogg_stream_packetin(&stream, &metadata); ogg_stream_packetin(&stream, &setup);
    std::FILE* const output = std::fopen(path, "wb");
    if (output == nullptr) return 1;
    ogg_page page{};
    while (ogg_stream_flush(&stream, &page) != 0) if (!write_page(output, page)) return 1;
    int position = 0; bool finished = false;
    std::uint32_t noise = 12345;
    while (!finished) {
        const int count = std::min(1024, frames - position);
        float** const samples = vorbis_analysis_buffer(&dsp, 1024);
        for (int sample = 0; sample < count; ++sample) {
            for (int channel = 0; channel < channels; ++channel) {
                noise = noise * 1664525U + 1013904223U;
                const double t = static_cast<double>(position + sample) / rate;
                const double frequency = 173 + channel * 137;
                const double random = static_cast<double>(noise >> 8) / 16777216.0 - 0.5;
                const bool impulse = (position + sample) % 1709 == 0;
                const double signal = 0.3 * std::sin(2 * std::numbers::pi * frequency * t) + 0.05 * random + (impulse ? 0.4 : 0.0);
                samples[channel][sample] = static_cast<float>(signal);
            }
        }
        position += count; vorbis_analysis_wrote(&dsp, count);
        while (vorbis_analysis_blockout(&dsp, &block) == 1) {
            vorbis_analysis(&block, nullptr); vorbis_bitrate_addblock(&block);
            ogg_packet packet{};
            while (vorbis_bitrate_flushpacket(&dsp, &packet) != 0) {
                ogg_stream_packetin(&stream, &packet);
                while (ogg_stream_pageout(&stream, &page) != 0) {
                    if (!write_page(output, page)) return 1;
                    if (ogg_page_eos(&page) != 0) finished = true;
                }
            }
        }
    }
    std::fclose(output); ogg_stream_clear(&stream); vorbis_block_clear(&block);
    vorbis_dsp_clear(&dsp); vorbis_comment_clear(&comments); vorbis_info_clear(&info);
    return 0;
}
}
int main(const int argc, char** const argv) {
    if (argc == 4 && std::strcmp(argv[1], "decode") == 0) return decode(argv[2], argv[3]);
    if (argc == 7 && std::strcmp(argv[1], "encode") == 0)
        return encode(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]), static_cast<float>(std::atof(argv[6])));
    return 2;
}
