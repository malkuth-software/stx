// One source, separate executables: oracle symbols never enter the production library.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(STX_BENCH_LIBVORBIS)
#include <vorbis/vorbisfile.h>
#elif defined(STX_BENCH_TREMOR)
#include <ivorbisfile.h>
#elif defined(STX_BENCH_STB)
#include "stx_vorbis/stb_compatible.h"
#else
#include "stx_vorbis/source.hpp"
#endif
namespace {
using Clock = std::chrono::steady_clock;
struct Measurement final {
    Clock::time_point start{Clock::now()};
    Clock::time_point first{};
    Clock::time_point end{};
    std::uint64_t frames{0};
    std::uint64_t samples{0};
    double probe{0};
    void block(const std::uint32_t count, const std::uint32_t channels) {
        if (frames == 0) first = Clock::now();
        frames += count;
        samples += std::uint64_t{count} * channels;
    }
    void finish() {
        end = Clock::now();
        if (frames == 0 || !std::isfinite(probe)) throw std::runtime_error("empty or nonfinite output");
    }
};
double milliseconds(const Clock::time_point begin, const Clock::time_point end) {
    const std::chrono::duration<double, std::milli> elapsed = end - begin;
    return elapsed.count();
}
std::vector<std::uint8_t> read_input(const char* const path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open input");
    const std::streamoff length = file.tellg();
    if (length <= 0 || length > std::numeric_limits<int>::max()) throw std::runtime_error("invalid input size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!file) throw std::runtime_error("cannot read input");
    return bytes;
}
#if defined(STX_BENCH_LIBVORBIS) || defined(STX_BENCH_TREMOR)
struct Input final { const std::vector<std::uint8_t>& bytes; std::size_t position{0}; };
std::size_t read_memory(void* const output, const std::size_t size, const std::size_t count, void* const context) {
    Input& input = *static_cast<Input*>(context);
    if (size == 0) return 0;
    const std::size_t available = (input.bytes.size() - input.position) / size;
    const std::size_t items = std::min(available, count);
    const std::size_t copied = items * size;
    std::memcpy(output, input.bytes.data() + input.position, copied);
    input.position += copied;
    return items;
}
struct VorbisOwner final {
    OggVorbis_File file{};
    bool open{false};
    ~VorbisOwner() { if (open) ov_clear(&file); }
};
Measurement run(const std::vector<std::uint8_t>& bytes, const std::string&) {
    Measurement result{};
    Input input{bytes};
    VorbisOwner owner{};
    const ov_callbacks callbacks{read_memory, nullptr, nullptr, nullptr};
    if (ov_open_callbacks(&input, &owner.file, nullptr, 0, callbacks) != 0) throw std::runtime_error("oracle setup failed");
    owner.open = true;
    int section = 0;
#if defined(STX_BENCH_TREMOR)
    std::array<std::int16_t, 16384> output{};
    for (;;) {
        const long count = ov_read(&owner.file, reinterpret_cast<char*>(output.data()), static_cast<int>(sizeof(output)), &section);
        if (count < 0) throw std::runtime_error("Tremor decode failed");
        if (count == 0) break;
        const vorbis_info& info = *ov_info(&owner.file, -1);
        const std::size_t samples = static_cast<std::size_t>(count) / sizeof(std::int16_t);
        if (info.channels <= 0 || samples % static_cast<unsigned int>(info.channels) != 0) throw std::runtime_error("invalid output shape");
        result.block(static_cast<std::uint32_t>(samples / static_cast<unsigned int>(info.channels)), static_cast<std::uint32_t>(info.channels));
        result.probe += static_cast<double>(output[0]) / 32768.0;
        result.probe += static_cast<double>(output[samples - 1]) / 32768.0;
    }
#else
    for (;;) {
        float** output = nullptr;
        const long count = ov_read_float(&owner.file, &output, 4096, &section);
        if (count < 0) throw std::runtime_error("libvorbis decode failed");
        if (count == 0) break;
        const vorbis_info& info = *ov_info(&owner.file, -1);
        result.block(static_cast<std::uint32_t>(count), static_cast<std::uint32_t>(info.channels));
        for (int channel = 0; channel < info.channels; ++channel) {
            result.probe += output[channel][0];
            result.probe += output[channel][count - 1];
        }
    }
#endif
    result.finish();
    return result;
}
#elif defined(STX_BENCH_STB)
struct StbOwner final {
    stb_vorbis* decoder{nullptr};
    ~StbOwner() { if (decoder != nullptr) stb_vorbis_close(decoder); }
};
Measurement run(const std::vector<std::uint8_t>& bytes, const std::string&) {
    Measurement result{};
    int error = 0;
    StbOwner owner{};
    owner.decoder = stb_vorbis_open_memory(bytes.data(), static_cast<int>(bytes.size()), &error, nullptr);
    if (owner.decoder == nullptr) throw std::runtime_error("stb setup failed");
    for (;;) {
        int channels = 0;
        float** output = nullptr;
        const int count = stb_vorbis_get_frame_float(owner.decoder, &channels, &output);
        if (count == 0) break;
        result.block(static_cast<std::uint32_t>(count), static_cast<std::uint32_t>(channels));
        for (int channel = 0; channel < channels; ++channel) {
            result.probe += output[channel][0];
            result.probe += output[channel][count - 1];
        }
    }
    if (stb_vorbis_get_error(owner.decoder) != 0) throw std::runtime_error("stb decode failed");
    result.finish();
    return result;
}
#else
stx_vorbis::Synthesis implementation(const std::string& name) {
    if (name == "scalar") return stx_vorbis::Synthesis::scalar;
    if (name == "neon") return stx_vorbis::Synthesis::neon;
    if (name == "sse2") return stx_vorbis::Synthesis::sse2;
    if (name == "avx2") return stx_vorbis::Synthesis::avx2;
    if (name == "automatic") return stx_vorbis::Synthesis::automatic;
    throw std::runtime_error("unknown synthesis implementation");
}
Measurement run(const std::vector<std::uint8_t>& bytes, const std::string& mode) {
    const stx_vorbis::Synthesis synthesis = implementation(mode);
    if (!stx_vorbis::synthesis_available(synthesis)) throw std::runtime_error("synthesis unavailable");
    Measurement result{};
    stx_vorbis::MemorySource source(bytes);
    stx_vorbis::PullDecoder pull(source, {}, stx_vorbis::Recovery::strict, synthesis);
    stx_vorbis::Decoder& decoder = pull.decoder();
    for (;;) {
        const stx_vorbis::Status status = pull.advance();
        if (status == stx_vorbis::Status::end) break;
        if (status == stx_vorbis::Status::event) { decoder.acknowledge_event(); continue; }
        if (status != stx_vorbis::Status::pcm) throw std::runtime_error(stx_vorbis::status_name(status));
        const stx_vorbis::PcmView output = decoder.output();
        result.block(output.frames, output.channels);
        for (std::uint32_t channel = 0; channel < output.channels; ++channel) {
            const std::span<const float> samples = output.channel(channel);
            result.probe += samples[0];
            result.probe += samples[output.frames - 1];
        }
        if (decoder.consume(output.frames) != stx_vorbis::Status::ok) throw std::runtime_error("consume failed");
    }
    result.finish();
    return result;
}
#endif
unsigned int count_argument(const char* const text) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (*text == '\0' || *end != '\0' || value == 0 || value > 1000000) throw std::runtime_error("invalid iteration count");
    return static_cast<unsigned int>(value);
}
}
int main(const int argc, const char* const* const argv) {
    if (argc < 4 || argc > 5) {
        std::fprintf(stderr, "usage: benchmark input.ogg iterations warmups [automatic|scalar|neon|sse2|avx2]\n");
        return 2;
    }
    try {
        const std::vector<std::uint8_t> bytes = read_input(argv[1]);
        const unsigned int iterations = count_argument(argv[2]);
        const unsigned int warmups = count_argument(argv[3]);
        const std::string mode = argc == 5 ? argv[4] : "automatic";
        Measurement reference{};
        for (unsigned int index = 0; index < warmups; ++index) reference = run(bytes, mode);
        std::vector<Measurement> results;
        results.reserve(iterations);
        for (unsigned int index = 0; index < iterations; ++index) {
            const Measurement result = run(bytes, mode);
            if (result.frames != reference.frames || result.samples != reference.samples || result.probe != reference.probe)
                throw std::runtime_error("non-reproducible output");
            results.push_back(result);
        }
        std::printf("iteration,first_pcm_ms,remaining_ms,total_ms,frames,samples,probe\n");
        for (unsigned int index = 0; index < iterations; ++index) {
            const Measurement& result = results[index];
            std::printf("%u,%.9f,%.9f,%.9f,%llu,%llu,%.17g\n", index,
                milliseconds(result.start, result.first), milliseconds(result.first, result.end),
                milliseconds(result.start, result.end), static_cast<unsigned long long>(result.frames),
                static_cast<unsigned long long>(result.samples), result.probe);
        }
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "%s\n", failure.what()); return 1;
    }
    return 0;
}
