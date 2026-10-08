#include "bounded_imdct.hpp"
#include "memory.hpp"
#include "stx_vorbis/decoder.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>

namespace {
void check(const bool condition, const char* const message) {
    if (condition) return;
    std::fprintf(stderr, "%s\n", message);
    std::abort();
}
struct Allocation final {
    void* address{nullptr};
    std::size_t bytes{0};
    std::size_t alignment{0};
};
class Tracking final : public std::pmr::memory_resource {
public:
    std::size_t calls{0};
    std::size_t fail_at{0};
    std::size_t current{0};
    std::size_t peak{0};
    bool forbid{false};
private:
    void* do_allocate(const std::size_t bytes, const std::size_t alignment) override {
        ++calls;
        if (forbid || calls == fail_at) throw std::bad_alloc();
        for (Allocation& record : allocations_) {
            if (record.address != nullptr) continue;
            void* const storage = (*std::pmr::new_delete_resource()).allocate(bytes, alignment);
            record = Allocation{storage, bytes, alignment};
            current += bytes;
            peak = std::max(peak, current);
            return storage;
        }
        throw std::bad_alloc();
    }
    void do_deallocate(void* const pointer, const std::size_t bytes, const std::size_t alignment) override {
        for (Allocation& record : allocations_) {
            if (record.address != pointer) continue;
            check(bytes == record.bytes && alignment == record.alignment, "allocation release shape");
            (*std::pmr::new_delete_resource()).deallocate(pointer, bytes, alignment);
            current -= bytes;
            record = Allocation{};
            return;
        }
        check(false, "release of unknown allocation");
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    std::array<Allocation, 1024> allocations_{};
};

void transform_trial(Tracking& upstream, const std::size_t limit, const unsigned int small_block,
                     const unsigned int large_block) {
    stx_vorbis::detail::Memory memory(limit, &upstream);
    const stx_vorbis::experiment::BfftPlan small(small_block, memory);
    const stx_vorbis::experiment::BfftPlan large(large_block, memory);
    stx_vorbis::experiment::BfftWorkspace workspace(memory);
    workspace.prepare(small, large);
    const std::size_t bytes = small.bytes() + large.bytes() + workspace.bytes();
    check(memory.current == bytes && upstream.current == bytes, "all transform storage accounted");
    std::vector<double> input(large_block / 2, 0.0);
    std::vector<double> output(large_block, 1.0);
    upstream.forbid = true;
    const std::size_t calls = upstream.calls;
    for (unsigned int repeat = 0; repeat < 32; ++repeat) {
        workspace.execute(small, 0, std::span<const double>(input).first(small_block / 2),
            std::span<double>(output).first(small_block));
        workspace.execute(large, 1, input, output);
    }
    check(upstream.calls == calls && memory.current == bytes, "packet transforms never request allocation");
    for (const double sample : output) check(sample == 0.0, "zero transform");
    upstream.forbid = false;
}

void transform_bounds(const unsigned int small, const unsigned int large) {
    Tracking measured;
    transform_trial(measured, std::numeric_limits<std::size_t>::max(), small, large);
    check(measured.current == 0, "transform lifetime cleanup");
    Tracking exact;
    transform_trial(exact, measured.peak, small, large);
    Tracking short_budget;
    bool rejected = false;
    try { transform_trial(short_budget, measured.peak - 1, small, large); }
    catch (const std::bad_alloc&) { rejected = true; }
    check(rejected && short_budget.current == 0, "one-byte-short transform budget rejected cleanly");
    for (std::size_t failure = 1; failure <= measured.calls; ++failure) {
        Tracking failing;
        failing.fail_at = failure;
        rejected = false;
        try { transform_trial(failing, measured.peak, small, large); }
        catch (const std::bad_alloc&) { rejected = true; }
        check(rejected && failing.current == 0, "partial provision releases all allocations");
    }
    std::printf("transform,%u,%u,bytes=%zu,allocations=%zu\n", small, large, measured.peak, measured.calls);
}

std::vector<std::uint8_t> read_fixture(const char* const path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    check(file.good(), "fixture open");
    const std::streamoff length = file.tellg();
    check(length > 0 && length < 256 * 1024, "fixture extent");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), length);
    check(file.good(), "fixture read");
    return bytes;
}

stx_vorbis::Status decode(stx_vorbis::Decoder& decoder, const std::span<const std::uint8_t> input,
                        Tracking& memory) {
    check(decoder.push(input, true).accepted == input.size(), "complete fixture feed");
    for (;;) {
        const stx_vorbis::Status status = decoder.advance();
        const stx_vorbis::MemoryReport report = decoder.info().memory;
        check(report.current_bytes == memory.current && report.peak_bytes == memory.peak,
            "decoder reports exact upstream storage including BODFT");
        if (status == stx_vorbis::Status::event) { decoder.acknowledge_event(); continue; }
        if (status != stx_vorbis::Status::pcm) return status;
        check(decoder.consume(decoder.output().frames) == stx_vorbis::Status::ok, "consume");
    }
}
void decoder_bounds(const std::span<const std::uint8_t> input) {
    Tracking measured;
    {
        stx_vorbis::Decoder decoder({}, stx_vorbis::Recovery::strict, stx_vorbis::Synthesis::automatic, &measured);
        check(decode(decoder, input, measured) == stx_vorbis::Status::end, "measured decode");
        decoder.reset();
        check(decode(decoder, input, measured) == stx_vorbis::Status::end, "reset provisions replacement plans");
    }
    check(measured.current == 0, "decoder cleanup");
    for (std::size_t shortage = 0; shortage <= 1; ++shortage) {
        Tracking limited;
        stx_vorbis::Limits limits{};
        limits.memory_bytes = measured.peak - shortage;
        {
            stx_vorbis::Decoder decoder(limits, stx_vorbis::Recovery::strict, stx_vorbis::Synthesis::automatic, &limited);
            stx_vorbis::Status status = decode(decoder, input, limited);
            if (status == stx_vorbis::Status::end) {
                decoder.reset();
                status = decode(decoder, input, limited);
            }
            check(status == (shortage == 0 ? stx_vorbis::Status::end : stx_vorbis::Status::resource_limit),
                "exact/one-byte-short decoder budget");
            check(limited.peak <= limits.memory_bytes, "decoder never exceeds byte cap");
        }
        check(limited.current == 0, "limited decoder cleanup");
    }
    std::printf("decoder,peak=%zu,allocations=%zu\n", measured.peak, measured.calls);
}
} // namespace

int main(const int argc, char** const argv) {
    check(argc >= 2, "fixture paths required");
    transform_bounds(64, 8192);
    transform_bounds(8192, 8192);
    std::vector<std::uint8_t> chain;
    for (int index = 1; index < argc; ++index) {
        const std::vector<std::uint8_t> input = read_fixture(argv[index]);
        decoder_bounds(input);
        chain.insert(chain.end(), input.begin(), input.end());
    }
    decoder_bounds(chain);
    return 0;
}
