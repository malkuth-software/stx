#include "stx_zlib/inflate.hpp"
#include "inflate_fixture.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#if defined(STX_ZLIB_INFLATE_ORACLE)
#include <zlib.h>
#endif

namespace {
using namespace stx_zlib;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

InflateOptions options(InflateFormat format = InflateFormat::raw_deflate) {
    const InflateOptions result{
        .format = format, .window_bits = 15,
        .limits = {.input_bytes = ByteCount{1000000}, .output_bytes = ByteCount{200000},
                   .work = WorkCount{20000000}}};
    return result;
}

struct Decoded final {
    std::vector<std::byte> bytes{};
    InflateResult result{};
};

// Buffers are allocated once before streaming. No input/output borrow survives
// its call. final_input is repeated on the unconsumed final extent.
Decoded decode(std::span<const std::byte> input, const InflateOptions& settings,
               std::size_t input_chunk, std::size_t output_chunk) {
    std::unique_ptr<InflateStream> owner = std::make_unique<InflateStream>();
    InflateStream& stream = *owner;
    require(stream.reset(settings) == InflateError::none, "reset rejected valid options");
    std::vector<std::byte> output(200001, std::byte{0xa5});
    std::size_t read = 0;
    std::size_t written = 0;
    InflateResult result{};
    for (std::size_t calls = 0; calls < 1000000; ++calls) {
        const std::size_t take = std::min(input_chunk, input.size() - read);
        const std::size_t room = std::min(output_chunk, output.size() - written);
        const std::span<const std::byte> source = input.subspan(read, take);
        const std::span<std::byte> destination(output.data() + written, room);
        result = stream.process(source, destination, read + take == input.size());
        require(result.input_consumed <= take && result.output_produced <= room, "invalid progress counts");
        read += result.input_consumed;
        written += result.output_produced;
        require(result.total_input.value == read && result.total_output.value == written, "invalid totals");
        require(result.total_work.value <= settings.limits.work.value, "work budget exceeded");
        if (result.status == InflateStatus::error || result.status == InflateStatus::stream_end) {
            const InflateResult terminal = stream.process({}, {}, true);
            require(terminal.status == result.status && terminal.error == result.error, "terminal not sticky");
            require(terminal.total_work.value == result.total_work.value, "terminal spent work");
            require(output[written] == std::byte{0xa5}, "output guard overwritten");
            output.resize(written);
            Decoded decoded{.bytes = std::move(output), .result = result};
            return decoded;
        }
        require(result.input_consumed != 0 || result.output_produced != 0, "stream stalled");
    }
    throw std::runtime_error("stream exceeded call bound");
}

// Test-only RFC bit writer: low-order bit fields and high-order canonical codes.
struct Bits final {
    std::vector<std::byte> bytes{};
    unsigned used = 0;
    void put(unsigned value, unsigned count) {
        for (unsigned i = 0; i < count; ++i) {
            if (used == 0) bytes.push_back(std::byte{0});
            const unsigned bit = (value >> i) & 1U;
            bytes.back() |= static_cast<std::byte>(bit << used);
            used = (used + 1U) % 8U;
        }
    }
    void canonical(unsigned code, unsigned count) {
        unsigned reversed = 0;
        for (unsigned i = 0; i < count; ++i) {
            reversed = (reversed << 1U) | (code & 1U);
            code >>= 1U;
        }
        put(reversed, count);
    }
    void fixed(unsigned symbol) {
        if (symbol < 144) canonical(48U + symbol, 8);
        else if (symbol < 256) canonical(400U + symbol - 144U, 9);
        else if (symbol < 280) canonical(symbol - 256U, 7);
        else canonical(192U + symbol - 280U, 8);
    }
};

void check_error(std::span<const std::byte> bytes, InflateError error,
                 const InflateOptions& settings = options()) {
    const Decoded whole = decode(bytes, settings, bytes.size() + 1U, 4096);
    const Decoded split = decode(bytes, settings, 1, 1);
    require(whole.result.status == InflateStatus::error, "malformed stream accepted");
    if (whole.result.error != error) {
        std::cerr << "expected error " << static_cast<int>(error) << " got "
                  << static_cast<int>(whole.result.error) << '\n';
    }
    require(whole.result.error == error && split.result.error == error, "wrong malformed result");
    require(whole.bytes == split.bytes, "malformed prefix differs with splits");
}

void stored_and_fixed() {
    const std::array<std::byte, 8> stored{
        std::byte{1}, std::byte{3}, std::byte{0}, std::byte{252},
        std::byte{255}, std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    const Decoded decoded = decode(stored, options(), 1, 1);
    require(decoded.result.status == InflateStatus::stream_end, "stored decode failed");
    require(decoded.bytes.size() == 3 && decoded.bytes[2] == std::byte{'c'}, "stored payload differs");

    Bits fixed{};
    fixed.put(3, 3);
    fixed.fixed('a');
    fixed.fixed(285); // 258-byte overlapping match, distance one.
    fixed.canonical(0, 5);
    fixed.fixed(256);
    fixed.bytes.push_back(std::byte{0xfe});
    for (std::size_t input_chunk = 1; input_chunk <= fixed.bytes.size(); ++input_chunk) {
        const Decoded result = decode(fixed.bytes, options(), input_chunk, 1);
        require(result.result.status == InflateStatus::stream_end, "fixed decode failed");
        require(result.result.total_input.value == fixed.bytes.size() - 1U, "read stream tail");
        require(result.bytes.size() == 259, "wrong match length");
        for (const std::byte value : result.bytes) require(value == std::byte{'a'}, "wrong overlap copy");
    }
    for (std::size_t end = 0; end + 1 < fixed.bytes.size(); ++end) {
        const std::span<const std::byte> prefix(fixed.bytes.data(), end);
        check_error(prefix, InflateError::truncated_input);
    }
    InflateOptions limited = options();
    limited.limits.output_bytes = ByteCount{258};
    check_error(fixed.bytes, InflateError::output_limit_exceeded, limited);
    limited = options();
    limited.limits.input_bytes = ByteCount{1};
    check_error(fixed.bytes, InflateError::input_limit_exceeded, limited);
    limited = options();
    limited.limits.work = WorkCount{1};
    check_error(fixed.bytes, InflateError::work_limit_exceeded, limited);
}

void malformed_blocks() {
    const std::array<std::byte, 1> reserved{std::byte{7}};
    check_error(reserved, InflateError::invalid_block_type);
    const std::array<std::byte, 5> bad_stored{
        std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    check_error(bad_stored, InflateError::stored_length_mismatch);
    Bits no_history{};
    no_history.put(3, 3);
    no_history.fixed(257);
    no_history.canonical(0, 5);
    check_error(no_history.bytes, InflateError::distance_before_history);
    Bits invalid_literal{};
    invalid_literal.put(3, 3);
    invalid_literal.fixed(286);
    check_error(invalid_literal.bytes, InflateError::invalid_literal_length);
    Bits invalid_distance{};
    invalid_distance.put(3, 3);
    invalid_distance.fixed(257);
    invalid_distance.canonical(30, 5);
    check_error(invalid_distance.bytes, InflateError::invalid_distance_code);
    Bits bad_counts{};
    bad_counts.put(5, 3);
    bad_counts.put(31, 14);
    check_error(bad_counts.bytes, InflateError::invalid_code_counts);
    Bits oversubscribed{};
    oversubscribed.put(5, 3);
    oversubscribed.put(0, 14);
    for (unsigned i = 0; i < 4; ++i) oversubscribed.put(1, 3);
    check_error(oversubscribed.bytes, InflateError::oversubscribed_tree);
    Bits incomplete{};
    incomplete.put(5, 3);
    incomplete.put(0, 14);
    incomplete.put(1, 3);
    for (unsigned i = 0; i < 3; ++i) incomplete.put(0, 3);
    check_error(incomplete.bytes, InflateError::incomplete_tree);
    Bits previous{};
    previous.put(5, 3);
    previous.put(0, 14);
    previous.put(1, 3); // 16 and 0 each have length one.
    previous.put(0, 3);
    previous.put(0, 3);
    previous.put(1, 3);
    previous.put(1, 1); // symbol 16 without preceding code length.
    check_error(previous.bytes, InflateError::invalid_code_length_repeat);
    Bits too_many{};
    too_many.put(5, 3);
    too_many.put(0, 14);
    too_many.put(0, 3);
    too_many.put(0, 3);
    too_many.put(1, 3); // 18 and 0 each have length one.
    too_many.put(1, 3);
    too_many.put(1, 1);
    too_many.put(127, 7); // 138 zero lengths.
    too_many.put(1, 1);
    too_many.put(127, 7); // Exceeds 258 lengths.
    check_error(too_many.bytes, InflateError::invalid_code_length_repeat);
}

// Minimal dynamic block: only EOB has length one; empty distance alphabet.
Bits empty_dynamic(bool eob, bool invalid_symbol = false) {
    Bits bits{};
    bits.put(5, 3);
    bits.put(14U << 10U, 14); // HCLEN=18 includes symbol 1.
    for (unsigned i = 0; i < 18; ++i) {
        unsigned length = 0;
        if (i == 3 || i == 17) length = 1; // Symbols 0 and 1.
        bits.put(length, 3);
    }
    for (unsigned i = 0; i < 256; ++i) bits.put(0, 1);
    bits.put(eob ? 1U : 0U, 1);
    bits.put(0, 1); // No distance code.
    bits.put(invalid_symbol ? 1U : 0U, 1); // EOB or unused one-bit code.
    return bits;
}

void dynamic_and_wrapper() {
    const Bits dynamic = empty_dynamic(true);
    const Decoded decoded = decode(dynamic.bytes, options(), 1, 1);
    require(decoded.result.status == InflateStatus::stream_end && decoded.bytes.empty(), "empty dynamic rejected");
    const Bits invalid_symbol = empty_dynamic(true, true);
    check_error(invalid_symbol.bytes, InflateError::invalid_huffman_code);
    Bits empty_alphabet{};
    empty_alphabet.put(5, 3);
    empty_alphabet.put(0, 14);
    empty_alphabet.put(0, 12);
    check_error(empty_alphabet.bytes, InflateError::incomplete_tree);
    const Bits no_eob = empty_dynamic(false);
    check_error(no_eob.bytes, InflateError::missing_end_of_block);
    const std::array<std::byte, 8> empty_zlib{
        std::byte{0x78}, std::byte{0x9c}, std::byte{3}, std::byte{0},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}};
    InflateOptions wrapped = options(InflateFormat::zlib);
    wrapped.limits.output_bytes = ByteCount{0};
    const Decoded empty = decode(empty_zlib, wrapped, 1, 1);
    require(empty.result.status == InflateStatus::stream_end, "zero-output stream rejected");
    std::array<std::byte, 8> bad = empty_zlib;
    bad[7] = std::byte{2};
    check_error(bad, InflateError::adler_mismatch, wrapped);
    bad = empty_zlib;
    bad[0] = std::byte{0x79};
    check_error(bad, InflateError::invalid_method, wrapped);
    bad[0] = std::byte{0x88};
    check_error(bad, InflateError::invalid_window, wrapped);
    bad = empty_zlib;
    bad[1] = std::byte{0};
    check_error(bad, InflateError::invalid_header_check, wrapped);
    const std::array<std::byte, 6> dictionary{
        std::byte{0x78}, std::byte{0x20}, std::byte{0x12},
        std::byte{0x34}, std::byte{0x56}, std::byte{0x78}};
    const Decoded dict = decode(dictionary, wrapped, 1, 1);
    require(dict.result.error == InflateError::dictionary_required, "dictionary not rejected");
    require(dict.result.dictionary_id == 0x12345678U, "dictionary ID differs");
}

void dynamic_fixture_and_window() {
    std::vector<std::byte> expected(40000);
    for (std::size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<std::byte>(i % 13U);
    for (std::size_t chunk = 1; chunk <= dynamic_fixture.size(); ++chunk) {
        const Decoded result = decode(dynamic_fixture, options(), chunk, 1);
        require(result.result.status == InflateStatus::stream_end, "dynamic fixture rejected");
        require(result.bytes == expected, "dynamic fixture bytes differ");
    }
    for (std::size_t chunk = 1; chunk <= 300; ++chunk) {
        const Decoded result = decode(dynamic_fixture, options(), dynamic_fixture.size(), chunk);
        require(result.result.status == InflateStatus::stream_end && result.bytes == expected, "match bulk boundary mismatch");
    }
    Bits distant{};
    distant.put(3, 3);
    for (unsigned i = 0; i < 257; ++i) distant.fixed('a');
    distant.fixed(257);
    distant.canonical(16, 5); // Distance 257 plus seven zero extra bits.
    distant.put(0, 7);
    distant.fixed(256);
    InflateOptions small = options();
    small.window_bits = 8;
    check_error(distant.bytes, InflateError::distance_exceeds_window, small);
    const Decoded valid = decode(distant.bytes, options(), 1, 1);
    require(valid.result.status == InflateStatus::stream_end && valid.bytes.size() == 260, "distance 257 rejected");
}

void stored_checksum_boundaries() {
    // Worst byte sum at the 4096-byte Adler batch bound, multiple ring wraps,
    // and a following stored block exercise both the reduction and its carry.
    constexpr unsigned size = 65535;
    std::vector<std::byte> encoded{std::byte{0x78}, std::byte{0x01}};
    for (unsigned block = 0; block < 2; ++block) {
        encoded.push_back(static_cast<std::byte>(block));
        encoded.push_back(std::byte{0xff});
        encoded.push_back(std::byte{0xff});
        encoded.push_back(std::byte{0});
        encoded.push_back(std::byte{0});
        encoded.insert(encoded.end(), size, std::byte{0xff});
    }
    std::uint64_t low = 1;
    std::uint64_t high = 0;
    for (unsigned i = 0; i < size * 2U; ++i) {
        low = (low + 255U) % 65521U;
        high = (high + low) % 65521U;
    }
    const std::uint32_t checksum = static_cast<std::uint32_t>((high << 16U) | low);
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned shift = 24U - i * 8U;
        encoded.push_back(static_cast<std::byte>((checksum >> shift) & 255U));
    }
    constexpr std::array<std::size_t, 7> chunks{1, 15, 4095, 4096, 4097, 32768, 200000};
    for (const std::size_t chunk : chunks) {
        const Decoded result = decode(encoded, options(InflateFormat::zlib), encoded.size(), chunk);
        require(result.result.status == InflateStatus::stream_end && result.bytes.size() == size * 2U, "stored Adler batch mismatch");
        for (const std::byte value : result.bytes) require(value == std::byte{0xff}, "stored copy mismatch");
    }
    InflateOptions limited = options(InflateFormat::zlib);
    limited.limits.input_bytes = ByteCount{4103};
    check_error(encoded, InflateError::input_limit_exceeded, limited);
    limited = options(InflateFormat::zlib);
    limited.limits.output_bytes = ByteCount{4097};
    check_error(encoded, InflateError::output_limit_exceeded, limited);
}

void match_copy_boundaries() {
    constexpr std::array<unsigned, 19> distances{
        1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 127, 128, 129, 257, 258, 259, 32767, 32768};
    constexpr std::array<std::size_t, 19> chunks{
        1, 2, 3, 7, 15, 16, 17, 31, 32, 127, 128, 129, 257, 258, 259, 4095, 4096, 4097, 200000};
    for (const unsigned distance : distances) {
        Bits bits{};
        bits.put(3, 3);
        std::vector<std::byte> expected{};
        // Position 137 in the ring makes a number of match seeds straddle its
        // physical end. The full-window cases also test overwritten history.
        for (unsigned i = 0; i < 32768 + 137; ++i) {
            const unsigned value = (i * 17U + i / 251U) & 255U;
            bits.fixed(value);
            expected.push_back(static_cast<std::byte>(value));
        }
        unsigned code = 0;
        unsigned extra = 0;
        unsigned base = 1;
        if (distance <= 4) code = distance - 1;
        else {
            for (unsigned candidate = 4; candidate < 30; ++candidate) {
                const unsigned width = candidate / 2U - 1U;
                const unsigned first = (1U << (width + 1U)) + 1U + (candidate & 1U) * (1U << width);
                if (distance >= first) {
                    code = candidate;
                    extra = width;
                    base = first;
                }
            }
        }
        for (unsigned repeat = 0; repeat < 3; ++repeat) {
            bits.fixed(285);
            bits.canonical(code, 5);
            if (extra != 0) bits.put(distance - base, extra);
            for (unsigned i = 0; i < 258; ++i) {
                const std::byte value = expected[expected.size() - distance];
                expected.push_back(value);
            }
        }
        bits.fixed(256);
        bits.bytes.push_back(std::byte{0xa5});
        bits.bytes.push_back(std::byte{0x5a});
        for (const std::size_t chunk : chunks) {
            const Decoded decoded = decode(bits.bytes, options(), bits.bytes.size(), chunk);
            require(decoded.result.status == InflateStatus::stream_end && decoded.bytes == expected, "match kernel boundary mismatch");
            require(decoded.result.total_input.value == bits.bytes.size() - 2, "lookup consumed stream tail");
        }
    }
}

void lifecycle() {
    std::unique_ptr<InflateStream> owner = std::make_unique<InflateStream>();
    InflateStream& stream = *owner;
    InflateResult result = stream.process({}, {}, false);
    require(result.error == InflateError::invalid_state, "uninitialized stream accepted");
    const InflateOptions valid = options();
    require(stream.reset(valid) == InflateError::none, "reset failed");
    result = stream.process({}, {}, false);
    require(result.status == InflateStatus::need_input, "empty call treated as EOF");
    InflateOptions invalid = valid;
    invalid.window_bits = 7;
    require(stream.reset(invalid) == InflateError::invalid_argument, "invalid reset accepted");
    const std::array<std::byte, 2> empty{std::byte{3}, std::byte{0}};
    result = stream.process(empty, {}, true);
    require(result.status == InflateStatus::stream_end, "invalid reset destroyed stream");
    require(stream.reset(valid) == InflateError::none, "reuse reset failed");
    std::array<std::byte, 8> overlap{};
    result = stream.process(overlap, overlap, true);
    require(result.error == InflateError::invalid_argument && result.input_consumed == 0, "overlap not rejected");
    require(stream.reset(valid) == InflateError::none, "reset after failure failed");
    Bits seed_history{};
    seed_history.put(3, 3);
    seed_history.fixed('a');
    seed_history.fixed(256);
    result = stream.process(seed_history.bytes, {}, true);
    require(result.status == InflateStatus::need_output && result.output_produced == 0, "empty output not suspended");
    const std::span<const std::byte> tail(seed_history.bytes.data() + result.input_consumed,
                                        seed_history.bytes.size() - result.input_consumed);
    result = stream.process(tail, overlap, true);
    require(result.status == InflateStatus::stream_end && result.output_produced == 1, "output resume failed");
    require(stream.reset(valid) == InflateError::none, "reset after data failed");
    Bits no_history{};
    no_history.put(3, 3);
    no_history.fixed(257);
    no_history.canonical(0, 5);
    result = stream.process(no_history.bytes, overlap, true);
    require(result.error == InflateError::distance_before_history, "reset leaked old history");
    require(stream.reset(valid) == InflateError::none, "reset for workspace alias failed");
    const std::span<InflateStream> object(&stream, 1);
    const std::span<std::byte> state_bytes = std::as_writable_bytes(object);
    result = stream.process({}, state_bytes, true);
    require(result.error == InflateError::invalid_argument, "workspace output alias accepted");
}

#if defined(STX_ZLIB_INFLATE_ORACLE)
std::vector<std::byte> compress_reference(std::span<const std::byte> input,
                                          int level, int window, int strategy) {
    z_stream stream{};
    const int initialized = deflateInit2(&stream, level, Z_DEFLATED, window, 8, strategy);
    require(initialized == Z_OK, "oracle deflate initialization failed");
    std::vector<std::byte> output(input.size() * 2U + 1024U);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    const int status = deflate(&stream, Z_FINISH);
    const std::size_t count = stream.total_out;
    const int ended = deflateEnd(&stream);
    require(status == Z_STREAM_END && ended == Z_OK, "oracle deflate failed");
    output.resize(count);
    return output;
}

void differential() {
    std::vector<std::byte> input(70000);
    std::uint32_t random = 0x6a09e667;
    for (std::size_t i = 0; i < input.size(); ++i) {
        random = random * 1664525U + 1013904223U;
        unsigned value = (random >> 24U) & 255U;
        if (i % 257U < 200) value = static_cast<unsigned>(i % 7U);
        input[i] = static_cast<std::byte>(value);
    }
    unsigned cases = 0;
    for (int level = 0; level <= 9; ++level) {
        for (int window = 9; window <= 15; ++window) {
            for (int strategy = 0; strategy <= Z_FIXED; ++strategy) {
                for (int wrapped = 0; wrapped < 2; ++wrapped) {
                    int window_mode = window;
                    InflateFormat format = InflateFormat::zlib;
                    if (wrapped == 0) {
                        window_mode = -window;
                        format = InflateFormat::raw_deflate;
                    }
                    const std::vector<std::byte> encoded = compress_reference(input, level, window_mode, strategy);
                    const Decoded result = decode(encoded, options(format), 1, 1);
                    require(result.result.status == InflateStatus::stream_end, "oracle stream rejected");
                    require(result.bytes == input, "oracle stream bytes differ");
                    const Decoded bulk = decode(encoded, options(format), encoded.size(), 200000);
                    require(bulk.result.status == InflateStatus::stream_end && bulk.bytes == input, "bulk oracle mismatch");
                    ++cases;
                }
            }
        }
    }
    std::cout << "oracle=" << zlibVersion() << " valid cases=" << cases << '\n';
}
#endif

} // namespace

int main() {
    try {
        stored_and_fixed();
        malformed_blocks();
        dynamic_and_wrapper();
        lifecycle();
        match_copy_boundaries();
        stored_checksum_boundaries();
        dynamic_fixture_and_window();
#if defined(STX_ZLIB_INFLATE_ORACLE)
        differential();
#endif
        std::cout << "inflate tests passed; workspace=" << sizeof(InflateStream) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
