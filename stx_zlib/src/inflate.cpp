// Altered C++ implementation: canonical Huffman construction/decoding derived
// from Mark Adler's puff (zlib pin in PROVENANCE.md). Original notice is retained
// in LICENSE-puff.txt. Streaming, ownership, quotas and wrapper are rewritten.
#include "stx_zlib/inflate.hpp"

#include <limits>
#include <algorithm>
#include <cstring>

namespace stx_zlib {
namespace {

// Integer-address ordering is confined to this buffer-boundary overlap check.
// Subtraction avoids overflowing end-address arithmetic. Spans must themselves
// denote valid live storage; no decoder can validate arbitrary caller pointers.
bool overlaps(const void* first, std::size_t first_size,
              const void* second, std::size_t second_size) noexcept {
    if (first_size == 0 || second_size == 0) return false;
    const std::uintptr_t a = reinterpret_cast<std::uintptr_t>(first);
    const std::uintptr_t b = reinterpret_cast<std::uintptr_t>(second);
    if (a <= b) {
        const std::uintptr_t distance = b - a;
        const bool overlap = distance < first_size;
        return overlap;
    }
    const std::uintptr_t distance = a - b;
    const bool overlap = distance < second_size;
    return overlap;
}

constexpr std::array<unsigned, 29> length_base{
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<unsigned, 29> length_extra{
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<unsigned, 30> distance_base{
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
    6145, 8193, 12289, 16385, 24577};
constexpr std::array<unsigned, 30> distance_extra{
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
constexpr std::array<unsigned, 19> alphabet_order{
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

} // namespace

InflateError InflateStream::reset(const InflateOptions& options) noexcept {
    const std::uint64_t unlimited = std::numeric_limits<std::uint64_t>::max();
    if ((options.format != InflateFormat::zlib && options.format != InflateFormat::raw_deflate) ||
        options.window_bits < 8 || options.window_bits > 15 ||
        options.limits.input_bytes.value == unlimited ||
        options.limits.output_bytes.value == unlimited || options.limits.work.value == unlimited) {
        return InflateError::invalid_argument;
    }
    options_ = options;
    phase_ = Phase::header;
    if (options.format == InflateFormat::raw_deflate) phase_ = Phase::block;
    error_ = InflateError::none;
    input_total_ = ByteCount{};
    output_total_ = ByteCount{};
    work_total_ = WorkCount{};
    history_next_ = 0;
    history_live_ = 0;
    window_size_ = std::size_t{1} << options.window_bits;
    code_ = CodeCursor{};
    bit_buffer_ = 0;
    bit_count_ = 0;
    literal_count_ = 0;
    distance_count_ = 0;
    alphabet_count_ = 0;
    length_index_ = 0;
    repeat_symbol_ = 0;
    match_length_ = 0;
    match_distance_ = 0;
    extra_bits_ = 0;
    literal_byte_ = 0;
    stored_left_ = 0;
    final_block_ = false;
    adler_low_ = 1;
    adler_high_ = 0;
    trailer_ = 0;
    trailer_bytes_ = 0;
    dictionary_id_ = 0;
    // Old history and trees are outside the live ranges until replaced. Reset
    // does not clear caller output or read history belonging to an old stream.
    return InflateError::none;
}

bool InflateStream::fail(InflateError error) noexcept {
    error_ = error;
    phase_ = Phase::failed;
    return false;
}

bool InflateStream::charge(std::uint64_t amount) noexcept {
    const std::uint64_t remaining = options_.limits.work.value - work_total_.value;
    if (amount > remaining) {
        fail(InflateError::work_limit_exceeded);
        return false;
    }
    work_total_.value += amount;
    return true;
}

bool InflateStream::bits(Call& call, unsigned count, unsigned& value) noexcept {
    // All callers request at most 16 bits. At most seven surplus bits survive.
    while (bit_count_ < count) {
        if (call.read == call.input.size()) {
            if (call.final_input) fail(InflateError::truncated_input);
            call.status = InflateStatus::need_input;
            return false;
        }
        if (input_total_.value == options_.limits.input_bytes.value) {
            fail(InflateError::input_limit_exceeded);
            return false;
        }
        if (!charge(1)) return false;
        const std::uint32_t next = std::to_integer<std::uint32_t>(call.input[call.read]);
        bit_buffer_ |= next << bit_count_;
        bit_count_ += 8;
        ++call.read;
        ++input_total_.value;
    }
    if (!charge(1)) return false;
    const std::uint32_t mask = (std::uint32_t{1} << count) - 1U;
    value = bit_buffer_ & mask;
    bit_buffer_ >>= count;
    bit_count_ -= count;
    return true;
}

bool InflateStream::symbol(Call& call, const Tree& tree, unsigned& value) noexcept {
    if (code_.length == 1) {
        // Peek only live, budget-admitted bytes, without consuming a trailer or
        // the next stream. bits() below consumes exactly the matched code.
        const unsigned bytes_needed = (16U - bit_count_) / 8U;
        const std::size_t available = call.input.size() - call.read;
        const std::uint64_t budget = options_.limits.input_bytes.value - input_total_.value;
        if (available >= bytes_needed && budget >= bytes_needed) {
            if (!charge(1)) return false;
            std::uint32_t peek = bit_buffer_;
            const std::uint32_t first = std::to_integer<std::uint32_t>(call.input[call.read]);
            peek |= first << bit_count_;
            if (bytes_needed == 2) {
                const std::uint32_t second = std::to_integer<std::uint32_t>(call.input[call.read + 1]);
                peek |= second << (bit_count_ + 8U);
            }
            const unsigned entry = tree.lookup[peek & 511U];
            const unsigned length = entry & 15U;
            if (length != 0) {
                unsigned discarded = 0;
                if (!bits(call, length, discarded)) return false;
                value = entry >> 4U;
                return true;
            }
        }
    }
    while (code_.length <= tree.maximum_length) {
        unsigned bit = 0;
        if (!bits(call, 1, bit)) return false;
        code_.code |= bit;
        const unsigned count = tree.counts[code_.length];
        const unsigned end = code_.first + count;
        if (code_.code >= code_.first && code_.code < end) {
            const unsigned offset = code_.code - code_.first;
            value = tree.symbols[code_.index + offset];
            code_ = CodeCursor{};
            return true;
        }
        code_.index += count;
        code_.first = end << 1U;
        code_.code <<= 1U;
        ++code_.length;
    }
    fail(InflateError::invalid_huffman_code);
    return false;
}

bool InflateStream::build_tree(Tree& tree, std::span<const std::uint8_t> lengths,
                               bool code_alphabet) noexcept {
    // Precharge all count/offset initialization, two symbol passes and 15
    // Kraft-space steps. No table size depends on unvalidated stream values.
    // Lookup construction is bounded by 512 entries plus at most nine bit
    // reversals per live symbol; reserve that whole cost before mutation.
    const std::uint64_t work = 1088U + 11U * lengths.size();
    if (!charge(work)) return false;
    tree.counts.fill(0);
    tree.lookup.fill(0);
    tree.maximum_length = 0;
    for (std::size_t i = 0; i < lengths.size(); ++i) {
        const unsigned length = lengths[i];
        ++tree.counts[length];
        if (length > tree.maximum_length) tree.maximum_length = length;
    }
    if (tree.maximum_length == 0) {
        if (code_alphabet) {
            fail(InflateError::incomplete_tree);
            return false;
        }
        return true; // Empty distance alphabet is legal until a match uses it.
    }
    int available = 1;
    for (unsigned length = 1; length <= 15; ++length) {
        available *= 2;
        available -= tree.counts[length];
        if (available < 0) {
            fail(InflateError::oversubscribed_tree);
            return false;
        }
    }
    if (available != 0 && (code_alphabet || tree.maximum_length != 1)) {
        fail(InflateError::incomplete_tree);
        return false;
    }
    std::array<unsigned, 16> offsets{};
    for (unsigned length = 1; length < 15; ++length) {
        offsets[length + 1] = offsets[length] + tree.counts[length];
    }
    for (std::size_t i = 0; i < lengths.size(); ++i) {
        const unsigned length = lengths[i];
        if (length != 0) {
            const unsigned offset = offsets[length];
            tree.symbols[offset] = static_cast<std::uint16_t>(i);
            ++offsets[length];
        }
    }
    unsigned code = 0;
    unsigned symbol_index = 0;
    const unsigned root_length = std::min(tree.maximum_length, 9U);
    for (unsigned length = 1; length <= root_length; ++length) {
        const unsigned end = symbol_index + tree.counts[length];
        while (symbol_index < end) {
            unsigned forward = code;
            unsigned reversed = 0;
            for (unsigned bit = 0; bit < length; ++bit) {
                reversed = (reversed << 1U) | (forward & 1U);
                forward >>= 1U;
            }
            const unsigned symbol_value = tree.symbols[symbol_index];
            const std::uint16_t entry = static_cast<std::uint16_t>((symbol_value << 4U) | length);
            const unsigned stride = 1U << length;
            for (unsigned index = reversed; index < 512; index += stride) tree.lookup[index] = entry;
            ++code;
            ++symbol_index;
        }
        code <<= 1U;
    }
    return true;
}

constexpr InflateStream::Tree InflateStream::make_fixed_tree(bool distance_alphabet) noexcept {
    // RFC 1951's immutable fixed alphabets are prepared at compile time. Their
    // canonical ordering is independent of input, stream lifetime or ISA.
    Tree tree{};
    unsigned count = 288;
    tree.maximum_length = 9;
    if (distance_alphabet) {
        count = 32;
        tree.maximum_length = 5;
    }
    for (unsigned symbol_value = 0; symbol_value < count; ++symbol_value) {
        unsigned length = 5;
        unsigned code = symbol_value;
        unsigned index = symbol_value;
        if (!distance_alphabet) {
            if (symbol_value < 144) {
                length = 8;
                code = 48U + symbol_value;
                index = 24U + symbol_value;
            } else if (symbol_value < 256) {
                length = 9;
                code = 400U + symbol_value - 144U;
                index = 176U + symbol_value - 144U;
            } else if (symbol_value < 280) {
                length = 7;
                code = symbol_value - 256U;
                index = symbol_value - 256U;
            } else {
                length = 8;
                code = 192U + symbol_value - 280U;
                index = 168U + symbol_value - 280U;
            }
        }
        ++tree.counts[length];
        tree.symbols[index] = static_cast<std::uint16_t>(symbol_value);
        unsigned reversed = 0;
        for (unsigned bit = 0; bit < length; ++bit) {
            reversed = (reversed << 1U) | (code & 1U);
            code >>= 1U;
        }
        const std::uint16_t entry = static_cast<std::uint16_t>((symbol_value << 4U) | length);
        const unsigned stride = 1U << length;
        for (unsigned slot = reversed; slot < 512; slot += stride) tree.lookup[slot] = entry;
    }
    return tree;
}

bool InflateStream::fixed_trees() noexcept {
    static constexpr Tree literal_tree = make_fixed_tree(false);
    static constexpr Tree distance_tree = make_fixed_tree(true);
    if (!charge(2U * sizeof(Tree))) return false;
    literals_ = literal_tree;
    distances_ = distance_tree;
    return true;
}

bool InflateStream::output_ready(Call& call) noexcept {
    if (output_total_.value == options_.limits.output_bytes.value) {
        fail(InflateError::output_limit_exceeded);
        return false;
    }
    if (call.written == call.output.size()) {
        call.status = InflateStatus::need_output;
        return false;
    }
    const bool charged = charge(1);
    return charged;
}

void InflateStream::emit(Call& call, std::byte value) noexcept {
    call.output[call.written] = value;
    ++call.written;
    ++output_total_.value;
    history_[history_next_] = value;
    history_next_ = (history_next_ + 1U) % history_.size();
    if (history_live_ < history_.size()) ++history_live_;
    if (options_.format == InflateFormat::raw_deflate) return;
    // Each sum is less than 65521 before adding. The largest intermediate is
    // 131040, independent of stream length; no deferred-reduction overflow.
    adler_low_ += std::to_integer<std::uint32_t>(value);
    if (adler_low_ >= 65521U) adler_low_ -= 65521U;
    adler_high_ += adler_low_;
    if (adler_high_ >= 65521U) adler_high_ -= 65521U;
}

void InflateStream::publish(Call& call, std::size_t count) noexcept {
    // The caller has filled and charged a disjoint output span of at most 4096
    // bytes. Publish it to the ring in two contiguous pieces, with no wrap test
    // inside a copy loop. No input/output borrow survives this call.
    const std::byte* source = call.output.data() + call.written;
    const std::size_t first = std::min(count, history_.size() - history_next_);
    std::memcpy(history_.data() + history_next_, source, first);
    std::memcpy(history_.data(), source + first, count - first);
    history_next_ = (history_next_ + count) % history_.size();
    history_live_ = std::min(history_live_ + count, history_.size());
    call.written += count;
    output_total_.value += count;
    if (options_.format == InflateFormat::raw_deflate) return;

    // For n <= 4096, n*65520 + 255*n*(n+1)/2 + 65520 fits uint32.
    // Independent reductions expose SIMD without a serial per-byte Adler
    // dependency. Unaligned bytes and arbitrary tails are valid inputs.
    std::uint32_t sum = 0;
    std::uint32_t weighted = 0;
    const std::uint32_t length = static_cast<std::uint32_t>(count);
    for (std::uint32_t i = 0; i < length; ++i) {
        const std::uint32_t byte = std::to_integer<std::uint32_t>(source[i]);
        sum += byte;
        weighted += (length - i) * byte;
    }
    const std::uint32_t high = adler_high_ + length * adler_low_ + weighted;
    adler_high_ = high % 65521U;
    adler_low_ = (adler_low_ + sum) % 65521U;
}

bool InflateStream::literal_run(Call& call) noexcept {
    if (code_.length != 1) return false;
    // At most two input bytes and five work units per literal. All capacities
    // are proved before the loop. A long code, match or end marker is left for
    // the resumable parser, without consuming any part of that symbol.
    std::size_t capacity = std::min<std::size_t>(4096, call.output.size() - call.written);
    capacity = std::min(capacity, (call.input.size() - call.read) / 2U);
    const std::uint64_t input_left = (options_.limits.input_bytes.value - input_total_.value) / 2U;
    const std::uint64_t output_left = options_.limits.output_bytes.value - output_total_.value;
    const std::uint64_t work_left = (options_.limits.work.value - work_total_.value) / 5U;
    if (input_left < capacity) capacity = static_cast<std::size_t>(input_left);
    if (output_left < capacity) capacity = static_cast<std::size_t>(output_left);
    if (work_left < capacity) capacity = static_cast<std::size_t>(work_left);
    if (capacity == 0) return false;
    std::size_t read = call.read;
    std::size_t written = 0;
    std::uint32_t buffer = bit_buffer_;
    unsigned live_bits = bit_count_;
    while (written < capacity) {
        const std::uint32_t low = std::to_integer<std::uint32_t>(call.input[read]);
        const std::uint32_t high = std::to_integer<std::uint32_t>(call.input[read + 1]);
        const std::uint32_t pair = low | (high << 8U);
        const std::uint32_t peek = buffer | (pair << live_bits);
        const unsigned entry = literals_.lookup[peek & 511U];
        if (entry == 0 || entry >= 4096) break;
        const unsigned length = entry & 15U;
        const unsigned consumed = (length + 7U - live_bits) / 8U;
        live_bits += consumed * 8U;
        live_bits -= length;
        const std::uint32_t mask = (std::uint32_t{1} << live_bits) - 1U;
        buffer = (peek >> length) & mask;
        read += consumed;
        call.output[call.written + written] = static_cast<std::byte>(entry >> 4U);
        ++written;
    }
    if (written == 0) return false;
    const std::size_t consumed = read - call.read;
    // The preflight bound proves these additions cannot exceed any quota.
    work_total_.value += 3U * written + consumed;
    input_total_.value += consumed;
    call.read = read;
    bit_buffer_ = buffer;
    bit_count_ = live_bits;
    publish(call, written);
    return true;
}

bool InflateStream::stored_bytes(Call& call) noexcept {
    if (!output_ready(call)) return false;
    if (call.read == call.input.size()) {
        if (call.final_input) fail(InflateError::truncated_input);
        call.status = InflateStatus::need_input;
        return false;
    }
    if (input_total_.value == options_.limits.input_bytes.value) {
        fail(InflateError::input_limit_exceeded);
        return false;
    }
    // Stored-block headers leave the bit buffer empty. Bounds and quotas are
    // settled once for this kernel, before it writes any caller storage.
    std::size_t count = std::min<std::size_t>(stored_left_, 4096);
    count = std::min(count, call.output.size() - call.written);
    count = std::min(count, call.input.size() - call.read);
    const std::uint64_t input_left = options_.limits.input_bytes.value - input_total_.value;
    const std::uint64_t output_left = options_.limits.output_bytes.value - output_total_.value;
    if (input_left < count) count = static_cast<std::size_t>(input_left);
    if (output_left < count) count = static_cast<std::size_t>(output_left);
    // One output byte was charged by output_ready; charge the rest plus input.
    if (!charge(2U * count - 1U)) return false;
    std::memcpy(call.output.data() + call.written, call.input.data() + call.read, count);
    call.read += count;
    input_total_.value += count;
    stored_left_ -= static_cast<unsigned>(count);
    publish(call, count);
    return true;
}

bool InflateStream::copy_match(Call& call) noexcept {
    if (!output_ready(call)) return false;
    std::size_t count = std::min<std::size_t>(match_length_, call.output.size() - call.written);
    const std::uint64_t output_left = options_.limits.output_bytes.value - output_total_.value;
    if (output_left < count) count = static_cast<std::size_t>(output_left);
    if (!charge(count - 1U)) return false;
    const std::size_t source = (history_next_ + history_.size() - match_distance_) % history_.size();
    std::byte* destination = call.output.data() + call.written;
    if (match_distance_ == 1) {
        const int byte = std::to_integer<int>(history_[source]);
        std::memset(destination, byte, count);
    } else {
        const std::size_t seed = std::min<std::size_t>(count, match_distance_);
        const std::size_t first = std::min(seed, history_.size() - source);
        std::memcpy(destination, history_.data() + source, first);
        std::memcpy(destination + first, history_.data(), seed - first);
        // LZ copies repeat newly produced bytes. Grow the initialized prefix;
        // every memcpy is disjoint, including distance < length. A memmove of
        // the original history would not implement this recurrence.
        std::size_t filled = seed;
        while (filled < count) {
            const std::size_t copied = std::min(filled, count - filled);
            std::memcpy(destination + filled, destination, copied);
            filled += copied;
        }
    }
    match_length_ -= static_cast<unsigned>(count);
    publish(call, count);
    return true;
}

void InflateStream::end_block() noexcept {
    phase_ = Phase::block;
    if (final_block_) {
        // No speculative byte consumption: only final-byte padding remains.
        bit_buffer_ = 0;
        bit_count_ = 0;
        phase_ = Phase::trailer;
        if (options_.format == InflateFormat::raw_deflate) phase_ = Phase::done;
    }
}

bool InflateStream::step(Call& call) noexcept {
    unsigned value = 0;
    switch (phase_) {
    case Phase::header: {
        if (!bits(call, 16, value)) return false;
        const unsigned cmf = value & 255U;
        const unsigned flags = value >> 8U;
        if ((cmf & 15U) != 8) {
            fail(InflateError::invalid_method);
            return false;
        }
        const unsigned window_bits = (cmf >> 4U) + 8U;
        if (window_bits > options_.window_bits || window_bits > 15) {
            fail(InflateError::invalid_window);
            return false;
        }
        const unsigned header = (cmf << 8U) | flags;
        if (header % 31U != 0) {
            fail(InflateError::invalid_header_check);
            return false;
        }
        window_size_ = std::size_t{1} << window_bits;
        phase_ = Phase::block;
        if ((flags & 32U) != 0) phase_ = Phase::dictionary;
        return true;
    }
    case Phase::dictionary:
        if (!bits(call, 8, value)) return false;
        dictionary_id_ = (dictionary_id_ << 8U) | value;
        ++trailer_bytes_;
        if (trailer_bytes_ == 4) {
            fail(InflateError::dictionary_required);
            return false;
        }
        return true;
    case Phase::block: {
        if (!bits(call, 3, value)) return false;
        final_block_ = (value & 1U) != 0;
        const unsigned type = value >> 1U;
        if (type == 3) {
            fail(InflateError::invalid_block_type);
            return false;
        }
        if (type == 0) {
            bit_buffer_ = 0;
            bit_count_ = 0;
            phase_ = Phase::stored_length;
        } else if (type == 1) {
            if (!fixed_trees()) return false;
            phase_ = Phase::literal;
        } else {
            phase_ = Phase::dynamic_counts;
        }
        return true;
    }
    case Phase::stored_length:
        if (!bits(call, 16, stored_left_)) return false;
        phase_ = Phase::stored_complement;
        return true;
    case Phase::stored_complement:
        if (!bits(call, 16, value)) return false;
        if ((stored_left_ ^ value) != 65535U) {
            fail(InflateError::stored_length_mismatch);
            return false;
        }
        phase_ = Phase::stored_bytes;
        return true;
    case Phase::stored_bytes: {
        if (stored_left_ == 0) {
            end_block();
            return true;
        }
        const bool copied = stored_bytes(call);
        return copied;
    }
    case Phase::dynamic_counts:
        if (!bits(call, 14, value)) return false;
        literal_count_ = (value & 31U) + 257U;
        distance_count_ = ((value >> 5U) & 31U) + 1U;
        alphabet_count_ = (value >> 10U) + 4U;
        if (literal_count_ > 286 || distance_count_ > 30) {
            fail(InflateError::invalid_code_counts);
            return false;
        }
        if (!charge(19)) return false;
        alphabet_lengths_.fill(0);
        length_index_ = 0;
        phase_ = Phase::code_lengths;
        return true;
    case Phase::code_lengths:
        if (length_index_ < alphabet_count_) {
            if (!bits(call, 3, value)) return false;
            const unsigned index = alphabet_order[length_index_];
            alphabet_lengths_[index] = static_cast<std::uint8_t>(value);
            ++length_index_;
            return true;
        }
        if (!build_tree(code_lengths_, alphabet_lengths_, true)) return false;
        length_index_ = 0;
        phase_ = Phase::expanded_lengths;
        return true;
    case Phase::expanded_lengths: {
        const unsigned total = literal_count_ + distance_count_;
        if (length_index_ == total) {
            if (lengths_[256] == 0) {
                fail(InflateError::missing_end_of_block);
                return false;
            }
            const std::span<const std::uint8_t> literal_lengths(lengths_.data(), literal_count_);
            if (!build_tree(literals_, literal_lengths, false)) return false;
            const std::span<const std::uint8_t> distance_lengths(lengths_.data() + literal_count_, distance_count_);
            if (!build_tree(distances_, distance_lengths, false)) return false;
            phase_ = Phase::literal;
            return true;
        }
        if (!symbol(call, code_lengths_, value)) return false;
        if (value < 16) {
            lengths_[length_index_] = static_cast<std::uint8_t>(value);
            ++length_index_;
        } else {
            if (value == 16 && length_index_ == 0) {
                fail(InflateError::invalid_code_length_repeat);
                return false;
            }
            repeat_symbol_ = value;
            phase_ = Phase::repeat;
        }
        return true;
    }
    case Phase::repeat: {
        unsigned count_bits = 2;
        unsigned base = 3;
        std::uint8_t length = 0;
        if (repeat_symbol_ == 16) length = lengths_[length_index_ - 1];
        if (repeat_symbol_ == 17) count_bits = 3;
        if (repeat_symbol_ == 18) {
            count_bits = 7;
            base = 11;
        }
        if (!bits(call, count_bits, value)) return false;
        const unsigned repeat = base + value;
        const unsigned remaining = literal_count_ + distance_count_ - length_index_;
        if (repeat > remaining) {
            fail(InflateError::invalid_code_length_repeat);
            return false;
        }
        if (!charge(repeat)) return false;
        for (unsigned i = 0; i < repeat; ++i) {
            lengths_[length_index_] = length;
            ++length_index_;
        }
        phase_ = Phase::expanded_lengths;
        return true;
    }
    case Phase::literal:
        if (literal_run(call)) return true;
        if (!symbol(call, literals_, value)) return false;
        if (value == 256) {
            end_block();
            return true;
        }
        if (value > 285) {
            fail(InflateError::invalid_literal_length);
            return false;
        }
        if (value > 256) {
            const unsigned index = value - 257U;
            match_length_ = length_base[index];
            extra_bits_ = length_extra[index];
            phase_ = Phase::length_extra;
            return true;
        }
        literal_byte_ = value;
        phase_ = Phase::emit_literal;
        [[fallthrough]];
    case Phase::emit_literal:
        if (!output_ready(call)) return false;
        emit(call, static_cast<std::byte>(literal_byte_));
        phase_ = Phase::literal;
        return true;
    case Phase::length_extra:
        if (!bits(call, extra_bits_, value)) return false;
        match_length_ += value;
        phase_ = Phase::distance;
        return true;
    case Phase::distance:
        if (!symbol(call, distances_, value)) return false;
        if (value > 29) {
            fail(InflateError::invalid_distance_code);
            return false;
        }
        match_distance_ = distance_base[value];
        extra_bits_ = distance_extra[value];
        phase_ = Phase::distance_extra;
        return true;
    case Phase::distance_extra:
        if (!bits(call, extra_bits_, value)) return false;
        match_distance_ += value;
        if (match_distance_ > window_size_) {
            fail(InflateError::distance_exceeds_window);
            return false;
        }
        if (match_distance_ > history_live_) {
            fail(InflateError::distance_before_history);
            return false;
        }
        phase_ = Phase::match;
        return true;
    case Phase::match: {
        if (match_length_ == 0) {
            phase_ = Phase::literal;
            return true;
        }
        const bool copied = copy_match(call);
        return copied;
    }
    case Phase::trailer:
        if (!bits(call, 8, value)) return false;
        trailer_ = (trailer_ << 8U) | value;
        ++trailer_bytes_;
        if (trailer_bytes_ == 4) {
            const std::uint32_t adler = (adler_high_ << 16U) | adler_low_;
            if (trailer_ != adler) {
                fail(InflateError::adler_mismatch);
                return false;
            }
            phase_ = Phase::done;
        }
        return true;
    case Phase::idle:
        fail(InflateError::invalid_state);
        return false;
    case Phase::failed:
    case Phase::done:
        return false;
    }
    fail(InflateError::invalid_state);
    return false;
}

InflateResult InflateStream::process(std::span<const std::byte> input,
                                     std::span<std::byte> output,
                                     bool final_input) noexcept {
    Call call{.input = input, .output = output, .final_input = final_input};
    const bool input_output_overlap = overlaps(input.data(), input.size(), output.data(), output.size());
    const bool input_state_overlap = overlaps(input.data(), input.size(), this, sizeof(*this));
    const bool output_state_overlap = overlaps(output.data(), output.size(), this, sizeof(*this));
    if (phase_ != Phase::failed && phase_ != Phase::done &&
        (input_output_overlap || input_state_overlap || output_state_overlap)) {
        fail(InflateError::invalid_argument);
    }
    if (phase_ == Phase::idle) fail(InflateError::invalid_state);
    while (phase_ != Phase::done && phase_ != Phase::failed) {
        if (!charge(1)) break;
        if (!step(call)) break;
    }
    if (phase_ == Phase::done) call.status = InflateStatus::stream_end;
    if (phase_ == Phase::failed) call.status = InflateStatus::error;
    const InflateResult result{
        .status = call.status, .error = error_,
        .input_consumed = call.read, .output_produced = call.written,
        .total_input = input_total_, .total_output = output_total_,
        .total_work = work_total_, .dictionary_id = dictionary_id_};
    return result;
}

} // namespace stx_zlib
