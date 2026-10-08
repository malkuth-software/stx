#include "stx_vorbis/source.hpp"
#include "setup.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory_resource>
#include <vector>
namespace {
void check(const bool condition, const char* const message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}
std::vector<std::uint8_t> read_fixture(const char* const path) {
    std::FILE* const file = std::fopen(path, "rb"); check(file != nullptr, "fixture open");
    std::vector<std::uint8_t> bytes(65536, 0);
    const std::size_t count = std::fread(bytes.data(), 1, bytes.size(), file);
    check(std::ferror(file) == 0 && std::feof(file) != 0, "fixture size");
    bytes.resize(count); std::fclose(file);
    return bytes;
}
void little32(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::uint32_t value) {
    for (unsigned int index = 0; index < 4; ++index) bytes[offset + index] = static_cast<std::uint8_t>(value >> (8 * index));
}
std::vector<std::uint8_t> make_page(const std::uint32_t sequence, const std::uint8_t flags,
                                   const std::span<const std::uint8_t> lacing, const std::span<const std::uint8_t> body) {
    std::vector<std::uint8_t> page(27 + lacing.size() + body.size(), 0);
    page[0] = 'O'; page[1] = 'g'; page[2] = 'g'; page[3] = 'S'; page[5] = flags;
    little32(page, 14, 19); little32(page, 18, sequence);
    page[26] = static_cast<std::uint8_t>(lacing.size());
    std::copy(lacing.begin(), lacing.end(), page.begin() + 27);
    std::copy(body.begin(), body.end(), page.begin() + static_cast<std::ptrdiff_t>(27 + lacing.size()));
    little32(page, 22, stx_vorbis::ogg_crc(page));
    return page;
}
void continuation() {
    std::vector<std::uint8_t> expected(70000, 0);
    for (std::size_t index = 0; index < expected.size(); ++index) expected[index] = static_cast<std::uint8_t>(index * 17);
    std::array<std::uint8_t, 255> lacing{}; lacing.fill(255);
    std::vector<std::uint8_t> input = make_page(0, 2, lacing, std::span<const std::uint8_t>(expected).first(65025));
    std::array<std::uint8_t, 20> tail{}; tail.fill(255); tail[19] = 130;
    const std::vector<std::uint8_t> second = make_page(1, 5, tail, std::span<const std::uint8_t>(expected).subspan(65025));
    input.insert(input.end(), second.begin(), second.end());
    stx_vorbis::OggDemuxer demux;
    std::size_t position = 0; unsigned int packets = 0;
    while (true) {
        stx_vorbis::OggPacket packet{};
        const stx_vorbis::Status status = demux.next_packet(packet);
        if (status == stx_vorbis::Status::need_input) {
            const std::size_t count = std::min<std::size_t>(31, input.size() - position);
            const stx_vorbis::FeedResult fed = demux.push(std::span<const std::uint8_t>(input).subspan(position, count), position + count == input.size());
            check(fed.accepted == count, "continuation input"); position += count;
        } else if (status == stx_vorbis::Status::packet) {
            check(packet.bytes.size() == expected.size() && std::equal(packet.bytes.begin(), packet.bytes.end(), expected.begin()), "continued packet contents");
            check(packet.beginning && packet.ending, "continued BOS/EOS"); ++packets;
        } else { check(status == stx_vorbis::Status::end, "continuation end"); break; }
    }
    check(packets == 1, "continued packet count");
}
class FailingMemory final : public std::pmr::memory_resource {
public:
    std::size_t fail_at{0}; std::size_t calls{0}; std::size_t live{0};
private:
    void* do_allocate(const std::size_t bytes, const std::size_t alignment) override {
        ++calls;
        if (calls == fail_at) throw std::bad_alloc();
        void* const storage = (*std::pmr::new_delete_resource()).allocate(bytes, alignment);
        ++live; return storage;
    }
    void do_deallocate(void* const storage, const std::size_t bytes, const std::size_t alignment) override {
        check(live != 0, "allocator release balance"); --live;
        (*std::pmr::new_delete_resource()).deallocate(storage, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};
stx_vorbis::Status drain(stx_vorbis::Decoder& decoder) {
    while (true) {
        const stx_vorbis::Status status = decoder.advance();
        if (status == stx_vorbis::Status::event) decoder.acknowledge_event();
        else if (status == stx_vorbis::Status::pcm) check(decoder.consume(decoder.output().frames) == stx_vorbis::Status::ok, "consume");
        else return status;
    }
}
void allocation_failures(const std::span<const std::uint8_t> bytes) {
    bool reached_success = false;
    for (std::size_t failure = 1; failure <= 1024; ++failure) {
        FailingMemory memory; memory.fail_at = failure;
        try {
            stx_vorbis::Decoder decoder({}, stx_vorbis::Recovery::strict, stx_vorbis::Synthesis::scalar, &memory);
            check(decoder.push(bytes, true).accepted == bytes.size(), "allocation fixture input");
            const stx_vorbis::Status status = drain(decoder);
            check(status == stx_vorbis::Status::allocation_failed || status == stx_vorbis::Status::end, "allocation failure result");
            if (status == stx_vorbis::Status::end) reached_success = true;
            else check(decoder.advance() == status && decoder.output().frames == 0, "sticky allocation failure");
        } catch (const std::bad_alloc&) {}
        check(memory.live == 0, "allocation failure cleanup");
        if (reached_success) break;
    }
    check(reached_success, "complete allocation sweep");
}
void corruption(const std::span<const std::uint8_t> bytes) {
    std::vector<std::uint8_t> damaged(bytes.begin(), bytes.end());
    damaged[28] ^= 1;
    stx_vorbis::Decoder strict;
    check(strict.push(damaged, true).accepted == damaged.size(), "corrupt input");
    check(drain(strict) == stx_vorbis::Status::corrupt_page, "CRC rejection");
    stx_vorbis::Limits limits{}; limits.metadata_bytes = 2;
    stx_vorbis::Decoder limited(limits);
    check(limited.push(bytes, true).accepted == bytes.size(), "limited input");
    check(drain(limited) == stx_vorbis::Status::resource_limit, "metadata bound");
    stx_vorbis::OggDemuxer recover({}, stx_vorbis::Recovery::resynchronize);
    check(recover.push(damaged, true).accepted == damaged.size(), "recovery input");
    unsigned int diagnostics = 0; unsigned int packets = 0;
    for (unsigned int step = 0; step < 100; ++step) {
        stx_vorbis::OggPacket packet{};
        const stx_vorbis::Status status = recover.next_packet(packet);
        if (status == stx_vorbis::Status::packet) ++packets;
        else if (status == stx_vorbis::Status::end) break;
        else { check(recover.diagnostic().recoverable, "recoverable framing error"); ++diagnostics; }
    }
    check(diagnostics >= 1 && packets >= 1, "CRC recovery progress");
}
void huffman() {
    stx_vorbis::detail::Codebook book(std::pmr::new_delete_resource());
    const std::uint8_t lengths[]{2, 4, 4, 4, 4, 2, 3, 3};
    stx_vorbis::detail::build_huffman(book, lengths);
    const std::uint32_t codes[]{0, 2, 10, 6, 14, 1, 3, 7};
    for (unsigned int index = 0; index < 8; ++index) {
        const std::uint8_t bytes[]{static_cast<std::uint8_t>(codes[index]), 0};
        stx_vorbis::BitReader reader(bytes); std::uint32_t entry = 0;
        check(book.decode(reader, entry) == stx_vorbis::Status::ok && entry == index && reader.position() == lengths[index], "entry-order Huffman");
    }
    const std::uint8_t overfull[]{1, 1, 1};
    bool rejected = false;
    try { stx_vorbis::detail::build_huffman(book, overfull); }
    catch (const stx_vorbis::detail::DecodeFailure&) { rejected = true; }
    check(rejected, "overfull Huffman rejected");
}
}
int main(const int argc, char** const argv) {
    check(argc == 2, "fixture argument");
    const std::vector<std::uint8_t> bytes = read_fixture(argv[1]);
    continuation(); huffman(); allocation_failures(bytes); corruption(bytes);
    std::puts("continued packets, Huffman, allocation-failure sweep and corruption tests passed");
}
