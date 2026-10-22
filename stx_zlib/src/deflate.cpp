#include "stx_zlib/deflate.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace stx_zlib {
namespace {
constexpr unsigned absent = std::numeric_limits<std::uint32_t>::max();
constexpr std::array<unsigned, 29> length_base{
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
constexpr std::array<unsigned, 29> length_extra{
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
constexpr std::array<unsigned, 30> distance_base{
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,
    2049,3073,4097,6145,8193,12289,16385,24577};
constexpr std::array<unsigned, 19> code_order{16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

constexpr std::array<unsigned, 259> make_length_symbols() noexcept {
    std::array<unsigned, 259> result{};
    for (unsigned index = 0; index < length_base.size(); ++index) {
        const unsigned end = index == 28 ? 259 : length_base[index + 1];
        for (unsigned length = length_base[index]; length < end; ++length) result[length] = index;
    }
    return result;
}
constexpr std::array<unsigned, 259> length_symbols = make_length_symbols();

unsigned distance_symbol(unsigned distance) noexcept {
    if (distance <= 4) return distance - 1;
    const unsigned high = static_cast<unsigned>(std::bit_width(distance - 1) - 1);
    const unsigned code = 2 * high + (((distance - 1) >> (high - 1)) & 1U);
    return code;
}
constexpr unsigned reversed(unsigned value, unsigned count) noexcept {
    unsigned result = 0;
    for (unsigned index = 0; index < count; ++index) {
        result = (result << 1U) | (value & 1U);
        value >>= 1U;
    }
    return result;
}
constexpr unsigned fixed_length(unsigned symbol) noexcept {
    if (symbol < 144) return 8;
    if (symbol < 256) return 9;
    if (symbol < 280) return 7;
    return 8;
}
constexpr unsigned fixed_code(unsigned symbol) noexcept {
    unsigned code = 0;
    if (symbol < 144) code = 0x30U + symbol;
    else if (symbol < 256) code = 0x190U + symbol - 144;
    else if (symbol < 280) code = symbol - 256;
    else code = 0xc0U + symbol - 280;
    const unsigned result = reversed(code, fixed_length(symbol));
    return result;
}
struct FixedTables final {
    std::array<std::uint8_t, 286> literal_lengths{};
    std::array<std::uint16_t, 286> literal_codes{};
    std::array<std::uint8_t, 30> distance_lengths{};
    std::array<std::uint16_t, 30> distance_codes{};
};
constexpr FixedTables make_fixed_tables() noexcept {
    FixedTables result{};
    for (unsigned symbol = 0; symbol < 286; ++symbol) {
        result.literal_lengths[symbol] = static_cast<std::uint8_t>(fixed_length(symbol));
        result.literal_codes[symbol] = static_cast<std::uint16_t>(fixed_code(symbol));
    }
    for (unsigned symbol = 0; symbol < 30; ++symbol) {
        result.distance_lengths[symbol] = 5;
        result.distance_codes[symbol] = static_cast<std::uint16_t>(reversed(symbol, 5));
    }
    return result;
}
constexpr FixedTables fixed_tables = make_fixed_tables();
bool overlaps(const void* first, std::size_t first_size, const void* second, std::size_t second_size) noexcept {
    if (first_size == 0 || second_size == 0) return false;
    const std::uintptr_t a = reinterpret_cast<std::uintptr_t>(first);
    const std::uintptr_t b = reinterpret_cast<std::uintptr_t>(second);
    const bool result = a <= b ? b - a < first_size : a - b < second_size;
    return result;
}
} // namespace

bool DeflateStream::fail(DeflateError error) noexcept {
    error_ = error;
    phase_ = Phase::failed;
    return false;
}
bool DeflateStream::charge(std::uint64_t amount) noexcept {
    if (amount > options_.work_limit.value - work_total_.value) {
        fail(DeflateError::work_limit_exceeded);
        return false;
    }
    work_total_.value += amount;
    return true;
}
DeflateError DeflateStream::reset(const DeflateOptions& options) noexcept {
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    const bool strategy_valid = options.strategy == DeflateStrategy::normal ||
        options.strategy == DeflateStrategy::filtered || options.strategy == DeflateStrategy::huffman_only ||
        options.strategy == DeflateStrategy::rle || options.strategy == DeflateStrategy::fixed;
    if ((options.format != DeflateFormat::zlib && options.format != DeflateFormat::raw_deflate) ||
        !strategy_valid || options.level < -1 || options.level > 9 ||
        options.window_bits < 8 || options.window_bits > 15 ||
        options.memory_level < 1 || options.memory_level > 9 ||
        options.input_limit.value == maximum || options.output_limit.value == maximum ||
        options.work_limit.value == maximum) return DeflateError::invalid_argument;
    options_ = options;
    level_ = options.level == -1 ? 6U : static_cast<unsigned>(options.level);
    // Match the reference's level policy; algorithm measurements must not
    // silently obtain speed by assigning a smaller search to the same level.
    constexpr std::array<unsigned, 10> chains{0,4,8,32,16,32,128,256,1024,4096};
    constexpr std::array<unsigned, 10> nice{0,8,16,32,16,32,128,128,258,258};
    constexpr std::array<unsigned, 10> good{0,4,4,4,4,8,8,8,32,32};
    constexpr std::array<unsigned, 10> lazy_limits{0,4,5,6,4,16,16,32,128,258};
    chain_limit_ = chains[level_];
    nice_length_ = nice[level_];
    good_length_ = good[level_];
    lazy_limit_ = lazy_limits[level_];
    hash_count_ = 1U << (options.memory_level + 7);
    const unsigned window_bits = options.format == DeflateFormat::zlib && options.window_bits == 8 ?
        9 : options.window_bits;
    window_size_ = std::size_t{1} << window_bits;
    phase_ = Phase::active;
    error_ = DeflateError::none;
    input_total_ = {};
    output_total_ = {};
    work_total_ = {};
    history_live_ = 0;
    block_live_ = 0;
    hash_ready_ = false;
    pending_size_ = 0;
    pending_read_ = 0;
    bit_buffer_ = 0;
    bit_count_ = 0;
    adler_low_ = 1;
    adler_high_ = 0;
    if (options.format == DeflateFormat::zlib) {
        const unsigned cmf = ((window_bits - 8) << 4U) | 8U;
        const unsigned hint = level_ <= 1 ? 0U : (level_ <= 5 ? 1U : (level_ == 6 ? 2U : 3U));
        unsigned header = (cmf << 8U) | (hint << 6U);
        header += (31U - header % 31U) % 31U;
        pending_[0] = static_cast<std::byte>(header >> 8U);
        pending_[1] = static_cast<std::byte>(header & 255U);
        pending_size_ = 2;
    }
    return DeflateError::none;
}
void DeflateStream::adler(std::span<const std::byte> bytes) noexcept {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t count = std::min<std::size_t>(5552, bytes.size() - offset);
        const std::uint32_t length = static_cast<std::uint32_t>(count);
        std::uint32_t sum = 0;
        std::uint32_t weighted = 0;
        for (std::size_t index = 0; index < count; ++index) {
            const std::uint32_t byte = std::to_integer<std::uint32_t>(bytes[offset + index]);
            sum += byte;
            weighted += (length - static_cast<std::uint32_t>(index)) * byte;
        }
        // 5552 bounds both sums, including the previous residues, in uint32.
        // Independent reductions expose common-width vectorization without
        // changing the integer result or carrying state through every byte.
        const std::uint32_t high = adler_high_ + length * adler_low_ + weighted;
        adler_low_ = (adler_low_ + sum) % 65521U;
        adler_high_ = high % 65521U;
        offset += count;
    }
}
DeflateStream::Prefix DeflateStream::prefix_at(std::size_t position) const noexcept {
    const unsigned first = std::to_integer<unsigned>(data_[position]);
    const unsigned second = std::to_integer<unsigned>(data_[position + 1]);
    const unsigned third = std::to_integer<unsigned>(data_[position + 2]);
    const unsigned pair = (first << hash_shift_) ^ second;
    const unsigned bucket = ((pair << hash_shift_) ^ third) & (hash_count_ - 1);
    const Prefix result{bucket, first | (second << 8U)};
    return result;
}
void DeflateStream::insert(std::size_t position) noexcept {
    const Prefix prefix = prefix_at(position);
    previous_[position] = heads_[prefix.bucket];
    heads_[prefix.bucket] = (prefix.tag << 16U) | static_cast<std::uint32_t>(position);
}
void DeflateStream::rebase_links(std::span<const std::uint32_t> source,
    std::span<std::uint32_t> destination, unsigned discarded, unsigned retained) noexcept {
    // Equal extents; destination is identical to source or starts before it.
    // Forward traversal permits overlap and fuses movement with rebasing.
    for (std::size_t index = 0; index < source.size(); ++index) {
        const std::uint32_t link = source[index];
        const unsigned position = link & 65535U;
        const unsigned shifted = position - discarded;
        // Unsigned underflow rejects positions before the retained window.
        // Absent is separate: 65535 itself is never an inserted position.
        const bool live = position != 65535U && shifted < retained;
        destination[index] = live ? (link & 0xffff0000U) | shifted : absent;
    }
}
template<bool Checked>
[[gnu::always_inline]] inline DeflateStream::Match DeflateStream::search_match(
    std::size_t position, std::size_t maximum, unsigned attempts, unsigned link, unsigned tag,
    unsigned minimum_length, std::uint64_t allowance) const noexcept {
    Match best{};
    best.length = minimum_length;
    // Search visits at most 4096 candidates, each comparing at most 258
    // bytes. Its effort fits uint32; publish it once after this read-only
    // kernel, keeping stream-state stores out of the dependent lookup chain.
    const std::size_t oldest = position > window_size_ ? position - window_size_ : 0;
    for (unsigned searched = 0; searched < attempts && (link & 65535U) != 65535U; ++searched) {
        if constexpr (Checked) {
            if (best.work == allowance) {
                best.exhausted = true;
                return best;
            }
        }
        ++best.work;
        const unsigned candidate = link & 65535U;
        // Links are strictly older than this position. Compare with the
        // prepared lower bound; calculate a distance only for a winning match.
        if (candidate < oldest) break;
        // Reject collisions from the packed link before loading candidate
        // bytes. The link's successor is then the only dependent memory read.
        if ((link >> 16U) == tag &&
            (best.length == 0 ||
                (data_[candidate + best.length] == data_[position + best.length] &&
                 data_[candidate + best.length - 1] == data_[position + best.length - 1]))) {
            // Equal first-two-byte tags and an equal bucket imply an equal
            // third byte: all hash masks retain its eight low bits. Reuse
            // that proof instead of comparing the three prefix bytes again.
            std::size_t length = 3;
            bool mismatch = false;
            // Unaligned word equality is endian-independent; a mismatch is
            // resolved in byte order. Every load is inside the live extent.
            while (maximum - length >= 8) {
                if constexpr (Checked) {
                    if (allowance - best.work < 8) {
                        best.exhausted = true;
                        return best;
                    }
                }
                best.work += 8;
                std::uint64_t left = 0;
                std::uint64_t right = 0;
                std::memcpy(&left, data_.data() + candidate + length, 8);
                std::memcpy(&right, data_.data() + position + length, 8);
                if (left != right) {
                    const std::uint64_t difference = left ^ right;
                    if constexpr (std::endian::native == std::endian::little) {
                        length += static_cast<unsigned>(std::countr_zero(difference)) / 8;
                        mismatch = true;
                    } else if constexpr (std::endian::native == std::endian::big) {
                        length += static_cast<unsigned>(std::countl_zero(difference)) / 8;
                        mismatch = true;
                    }
                    break;
                }
                length += 8;
            }
            while (!mismatch && length < maximum) {
                if constexpr (Checked) {
                    if (best.work == allowance) {
                        best.exhausted = true;
                        return best;
                    }
                }
                ++best.work;
                if (data_[candidate + length] != data_[position + length]) break;
                ++length;
            }
            if (length >= 3 && length > best.length) {
                best.length = static_cast<unsigned>(length);
                best.distance = static_cast<unsigned>(position - candidate);
                if (length == maximum || length >= nice_length_) break;
            }
        }
        link = previous_[candidate];
    }
    return best;
}
[[gnu::noinline]] DeflateStream::Match DeflateStream::search_checked(
    std::size_t position, std::size_t maximum, unsigned attempts, unsigned link,
    unsigned tag, unsigned minimum_length, std::uint64_t allowance) const noexcept {
    // Keep the exact-budget fallback out of each expanded hot query. Its
    // semantics remain identical; only the placement of checks differs.
    const Match result = search_match<true>(position, maximum, attempts, link, tag, minimum_length, allowance);
    return result;
}
[[gnu::always_inline]] inline DeflateStream::Match DeflateStream::find_match(
    std::size_t position, std::size_t end, unsigned link, unsigned tag,
    unsigned minimum_length, std::uint64_t allowance) const noexcept {
    Match result{};
    result.length = minimum_length;
    const std::size_t maximum = std::min<std::size_t>(258, end - position);
    if (maximum < 3 || maximum <= minimum_length || (link & 65535U) == 65535U) return result;
    const unsigned attempts = minimum_length >= good_length_ ? chain_limit_ / 4 : chain_limit_;
    // One visit plus at most maximum byte work and one re-read word per
    // candidate, including the mixed-endian scalar fallback. Proving the
    // whole query fits removes changing quota checks from its inner loops;
    // a small remaining budget uses the same search with exact checks.
    const std::uint64_t maximum_work = static_cast<std::uint64_t>(attempts) * (1 + maximum + 8);
    if (allowance >= maximum_work) {
        result = search_match<false>(position, maximum, attempts, link, tag, minimum_length, allowance);
    } else {
        result = search_checked(position, maximum, attempts, link, tag, minimum_length, allowance);
    }
    return result;
}
void DeflateStream::append_token(unsigned value, unsigned distance, std::size_t& count) noexcept {
    // Every token consumes at least one input byte. tokenize reserves work for
    // block_live_ appends, and block_live_ never exceeds tokens_.size().
    tokens_[count] = Token{static_cast<std::uint16_t>(value), static_cast<std::uint16_t>(distance)};
    ++count;
    if (distance == 0) {
        ++literal_frequency_[value];
    } else {
        const unsigned length = length_symbols[value];
        const unsigned code = distance_symbol(distance);
        ++literal_frequency_[257 + length];
        ++distance_frequency_[code];
        extra_cost_ += length_extra[length];
        if (code >= 4) extra_cost_ += code / 2 - 1;
    }
}
bool DeflateStream::tokenize() noexcept {
    token_count_ = 0;
    std::size_t count = 0;
    extra_cost_ = 0;
    literal_frequency_.fill(0);
    distance_frequency_.fill(0);
    literal_frequency_[256] = 1;
    const std::size_t end = history_live_ + block_live_;
    if (!charge(block_live_)) return false;
    if (options_.strategy == DeflateStrategy::huffman_only) {
        for (std::size_t position = history_live_; position < end; ++position) {
            append_token(std::to_integer<unsigned>(data_[position]), 0, count);
        }
        token_count_ = count;
        return true;
    }
    if (options_.strategy == DeflateStrategy::rle) {
        std::size_t position = history_live_;
        while (position < end) {
            std::size_t length = 0;
            const std::size_t maximum = std::min<std::size_t>(258, end - position);
            if (position != 0) {
                while (length < maximum) {
                    if (!charge(1)) return false;
                    if (data_[position + length] != data_[position - 1]) break;
                    ++length;
                }
            }
            if (length >= 3) {
                append_token(static_cast<unsigned>(length), 1, count);
                position += length;
            } else {
                append_token(std::to_integer<unsigned>(data_[position]), 0, count);
                ++position;
            }
        }
        token_count_ = count;
        return true;
    }
    const std::size_t key_end = end >= 2 ? end - 2 : 0;
    const std::size_t history_tail_count = std::min<std::size_t>(history_live_, 2);
    if (!charge(block_live_ + history_tail_count)) return false;
    std::size_t history_begin = history_live_ - history_tail_count;
    const unsigned maximum_buckets = 1U << (options_.memory_level + 7);
    const unsigned useful_buckets = std::bit_ceil(std::max(256U, 2 * static_cast<unsigned>(end)));
    const unsigned desired_buckets = std::min(maximum_buckets, useful_buckets);
    if (key_end != 0 && (!hash_ready_ || desired_buckets > hash_count_)) {
        hash_count_ = desired_buckets;
        hash_shift_ = static_cast<unsigned>((std::bit_width(hash_count_ - 1) + 2) / 3);
        if (!charge(hash_count_ + history_live_)) return false;
        std::fill_n(heads_.begin(), hash_count_, absent);
        hash_ready_ = true;
        history_begin = 0;
    }
    // Rebased links retain the indexed history. Only the last two positions
    // lacked three bytes of lookahead when the preceding block was encoded.
    const std::size_t history_key_end = std::min(history_live_, key_end);
    for (std::size_t position = history_begin; position < history_key_end; ++position) {
        insert(position);
    }
    std::size_t position = history_live_;
    const bool lazy = level_ >= 4;
    const bool filtered = options_.strategy == DeflateStrategy::filtered;
    const std::uint64_t allowance = options_.work_limit.value - work_total_.value;
    std::uint64_t remaining = allowance;
    Match lookahead{};
    while (position < key_end) {
        const Prefix prefix = prefix_at(position);
        const unsigned link = heads_[prefix.bucket];
        // Probe and insert share the prefix calculation and head load. Search
        // starts from the saved old head, so it cannot match the current byte.
        previous_[position] = link;
        heads_[prefix.bucket] = (prefix.tag << 16U) | static_cast<std::uint32_t>(position);
        Match match = lookahead;
        lookahead = {};
        if (match.length == 0) {
            match = find_match(position, end, link, prefix.tag, 0, remaining);
            remaining -= match.work;
            if (match.exhausted) {
                work_total_.value += allowance - remaining;
                return fail(DeflateError::work_limit_exceeded);
            }
        }
        if ((filtered && match.length <= 5) || (match.length == 3 && match.distance > 4096)) match = {};
        if (lazy && match.length >= 3 && match.length < lazy_limit_ && end - position > 3) {
            const Prefix next_prefix = prefix_at(position + 1);
            // Lookahead can change the decision only by exceeding this match.
            // A winning result remains valid after emitting one literal: the
            // current position was already inserted before this search.
            const Match next = find_match(position + 1, end, heads_[next_prefix.bucket],
                next_prefix.tag, match.length, remaining);
            remaining -= next.work;
            if (next.exhausted) {
                work_total_.value += allowance - remaining;
                return fail(DeflateError::work_limit_exceeded);
            }
            if (next.length > match.length) {
                lookahead = next;
                match = {};
            }
        }
        if (match.length < 3) {
            append_token(std::to_integer<unsigned>(data_[position]), 0, count);
            ++position;
            continue;
        }
        append_token(match.length, match.distance, count);
        // Speed levels omit interior positions of long greedy matches. The
        // next literal/match boundary is still inserted in the ordinary path.
        // Lazy levels retain every candidate position for compression quality.
        if (lazy || match.length <= lazy_limit_) {
            const std::size_t insert_end = std::min(position + match.length, key_end);
            for (std::size_t index = position + 1; index < insert_end; ++index) {
                insert(index);
            }
        }
        position += match.length;
    }
    for (; position < end; ++position) append_token(std::to_integer<unsigned>(data_[position]), 0, count);
    token_count_ = count;
    work_total_.value += allowance - remaining;
    return true;
}

bool DeflateStream::WeightOrder::operator()(const WeightedSymbol& left, const WeightedSymbol& right) const noexcept {
    if (left.weight != right.weight) return left.weight < right.weight;
    return left.symbol < right.symbol;
}
bool DeflateStream::build_tree(std::span<const std::uint32_t> frequencies,
    std::span<std::uint8_t> lengths, std::span<std::uint16_t> codes, unsigned maximum) noexcept {
    const unsigned alphabet = static_cast<unsigned>(frequencies.size());
    if (!charge(64U * alphabet + 512U)) return false;
    std::fill(lengths.begin(), lengths.end(), 0);
    std::fill(codes.begin(), codes.end(), 0);
    unsigned active = 0;
    for (unsigned symbol = 0; symbol < alphabet; ++symbol) {
        if (frequencies[symbol] == 0) continue;
        sorted_[active] = WeightedSymbol{frequencies[symbol], symbol};
        ++active;
    }
    // Two leaves give a complete tree even for empty/no-match blocks. Dummy
    // leaves affect the tree only; original frequencies still determine cost.
    for (unsigned symbol = 0; active < 2 && symbol < alphabet; ++symbol) {
        if (frequencies[symbol] != 0) continue;
        sorted_[active] = WeightedSymbol{1, symbol};
        ++active;
    }
    std::sort(sorted_.begin(), sorted_.begin() + active, WeightOrder{});
    for (unsigned index = 0; index < active; ++index) {
        nodes_[index] = Node{sorted_[index].weight, 0, absent};
    }
    // Sorted leaves and newly formed parents are two monotone queues. Each
    // node is selected once. Equal weights prefer a leaf, then queue order;
    // this also minimizes maximum depth among unconstrained optimal trees.
    unsigned leaf = 0;
    unsigned parent = active;
    unsigned next = active;
    const unsigned node_count = 2 * active - 1;
    while (next < node_count) {
        std::array<unsigned, 2> children{};
        for (unsigned child = 0; child < 2; ++child) {
            if (leaf < active && (parent == next || nodes_[leaf].weight <= nodes_[parent].weight)) {
                children[child] = leaf;
                ++leaf;
            } else {
                children[child] = parent;
                ++parent;
            }
        }
        nodes_[next] = Node{nodes_[children[0]].weight + nodes_[children[1]].weight, 0, absent};
        nodes_[children[0]].parent = next;
        nodes_[children[1]].parent = next;
        ++next;
    }
    // Parent indices are greater than child indices. One reverse traversal
    // replaces walking from each leaf to the root separately.
    for (unsigned index = node_count - 1; index != 0;) {
        --index;
        nodes_[index].depth = nodes_[nodes_[index].parent].depth + 1;
    }
    std::array<unsigned, 16> counts{};
    unsigned used = 0;
    for (unsigned index = 0; index < active; ++index) {
        const unsigned depth = std::min(nodes_[index].depth, maximum);
        ++counts[depth];
        used += 1U << (maximum - depth);
    }
    // Repair the Kraft sum after clamping deep leaves. Splitting one shorter
    // leaf preserves its weight; removing one maximum-depth leaf reduces the
    // sum by exactly one slot while preserving the total number of leaves.
    const unsigned capacity = 1U << maximum;
    while (used > capacity) {
        unsigned depth = maximum - 1;
        while (depth != 0 && counts[depth] == 0) --depth;
        if (depth == 0 || counts[maximum] == 0) return fail(DeflateError::workspace_overflow);
        --counts[depth];
        counts[depth + 1] += 2;
        --counts[maximum];
        --used;
    }
    unsigned index = 0;
    for (unsigned depth = maximum; depth != 0; --depth) {
        for (unsigned count = 0; count < counts[depth]; ++count) {
            lengths[sorted_[index].symbol] = static_cast<std::uint8_t>(depth);
            ++index;
        }
    }
    std::array<unsigned, 16> next_code{};
    unsigned code = 0;
    for (unsigned depth = 1; depth <= maximum; ++depth) {
        code = (code + counts[depth - 1]) << 1U;
        next_code[depth] = code;
    }
    for (unsigned symbol = 0; symbol < alphabet; ++symbol) {
        const unsigned depth = lengths[symbol];
        if (depth == 0) continue;
        codes[symbol] = static_cast<std::uint16_t>(reversed(next_code[depth], depth));
        ++next_code[depth];
    }
    return true;
}

bool DeflateStream::prepare_dynamic() noexcept {
    if (!build_tree(literal_frequency_, literal_length_, literal_code_, 15)) return false;
    if (!build_tree(distance_frequency_, distance_length_, distance_code_, 15)) return false;
    literal_count_ = 286;
    while (literal_count_ > 257 && literal_length_[literal_count_ - 1] == 0) --literal_count_;
    distance_count_ = 30;
    while (distance_count_ > 1 && distance_length_[distance_count_ - 1] == 0) --distance_count_;
    std::array<std::uint8_t, 316> lengths{};
    std::copy_n(literal_length_.begin(), literal_count_, lengths.begin());
    std::copy_n(distance_length_.begin(), distance_count_, lengths.begin() + literal_count_);
    const unsigned count = literal_count_ + distance_count_;
    std::array<std::uint32_t, 19> frequencies{};
    run_count_ = 0;
    unsigned position = 0;
    while (position < count) {
        const unsigned value = lengths[position];
        unsigned remaining = 1;
        while (position + remaining < count && lengths[position + remaining] == value) ++remaining;
        position += remaining;
        if (value != 0) {
            runs_[run_count_] = CodeRun{value, 0, 0};
            ++run_count_;
            ++frequencies[value];
            --remaining;
        }
        while (remaining != 0) {
            CodeRun run{value, 0, 0};
            unsigned taken = 1;
            if (value == 0 && remaining >= 11) {
                taken = std::min(remaining, 138U);
                run = CodeRun{18, taken - 11, 7};
            } else if (value == 0 && remaining >= 3) {
                taken = std::min(remaining, 10U);
                run = CodeRun{17, taken - 3, 3};
            } else if (value != 0 && remaining >= 3) {
                taken = std::min(remaining, 6U);
                run = CodeRun{16, taken - 3, 2};
            }
            runs_[run_count_] = run;
            ++run_count_;
            ++frequencies[run.symbol];
            remaining -= taken;
        }
    }
    if (!build_tree(frequencies, run_length_, run_code_, 7)) return false;
    code_count_ = 19;
    while (code_count_ > 4 && run_length_[code_order[code_count_ - 1]] == 0) --code_count_;
    dynamic_cost_ = 3 + 5 + 5 + 4 + 3 * code_count_ + extra_cost_;
    for (unsigned symbol = 0; symbol < 286; ++symbol) {
        dynamic_cost_ += literal_frequency_[symbol] * literal_length_[symbol];
    }
    for (unsigned symbol = 0; symbol < 30; ++symbol) {
        dynamic_cost_ += distance_frequency_[symbol] * distance_length_[symbol];
    }
    for (unsigned index = 0; index < run_count_; ++index) {
        dynamic_cost_ += run_length_[runs_[index].symbol] + runs_[index].bits;
    }
    return true;
}

bool DeflateStream::write_bits(unsigned value, unsigned count) noexcept {
    if (!charge(1)) return false;
    // Callers supply at most 16 bits, with fewer than eight bits carried in.
    bit_buffer_ |= (value & ((1U << count) - 1U)) << bit_count_;
    bit_count_ += count;
    while (bit_count_ >= 8) {
        if (pending_size_ == pending_.size()) return fail(DeflateError::workspace_overflow);
        pending_[pending_size_] = static_cast<std::byte>(bit_buffer_ & 255U);
        ++pending_size_;
        bit_buffer_ >>= 8U;
        bit_count_ -= 8;
    }
    return true;
}
bool DeflateStream::align_bits() noexcept {
    if (bit_count_ == 0) return true;
    const bool result = write_bits(0, 8 - bit_count_);
    return result;
}
bool DeflateStream::emit_tokens(bool dynamic) noexcept {
    const std::span<const std::uint8_t> literal_lengths = dynamic ? literal_length_ : fixed_tables.literal_lengths;
    const std::span<const std::uint16_t> literal_codes = dynamic ? literal_code_ : fixed_tables.literal_codes;
    const std::span<const std::uint8_t> distance_lengths = dynamic ? distance_length_ : fixed_tables.distance_lengths;
    const std::span<const std::uint16_t> distance_codes = dynamic ? distance_code_ : fixed_tables.distance_codes;
    std::uint64_t token_bits = extra_cost_;
    for (unsigned symbol = 0; symbol < literal_frequency_.size(); ++symbol) {
        token_bits += literal_frequency_[symbol] * literal_lengths[symbol];
    }
    for (unsigned symbol = 0; symbol < distance_frequency_.size(); ++symbol) {
        token_bits += distance_frequency_[symbol] * distance_lengths[symbol];
    }
    const std::uint64_t whole_bytes = (bit_count_ + token_bits) / 8;
    if (whole_bytes > pending_.size() - pending_size_) return fail(DeflateError::workspace_overflow);
    if (!charge(4 * token_count_)) return false;
    // Frequencies include EOB and prove the entire output extent. Each token
    // has at most four fields (15 + 5 + 15 + 13 bits); the carried prefix has
    // fewer than eight. Pack once, drain whole bytes, and publish once.
    std::uint64_t buffered = bit_buffer_;
    unsigned live = bit_count_;
    std::size_t written = pending_size_;
    for (std::size_t index = 0; index < token_count_; ++index) {
        const Token token = tokens_[index];
        std::uint64_t code = 0;
        unsigned bits = 0;
        if (token.distance == 0) {
            code = literal_codes[token.value];
            bits = literal_lengths[token.value];
        } else {
            const unsigned length = length_symbols[token.value];
            const unsigned symbol = 257 + length;
            code = literal_codes[symbol];
            bits = literal_lengths[symbol];
            code |= static_cast<std::uint64_t>(token.value - length_base[length]) << bits;
            bits += length_extra[length];
            const unsigned distance = distance_symbol(token.distance);
            code |= static_cast<std::uint64_t>(distance_codes[distance]) << bits;
            bits += distance_lengths[distance];
            code |= static_cast<std::uint64_t>(token.distance - distance_base[distance]) << bits;
            bits += distance < 4 ? 0 : distance / 2 - 1;
        }
        buffered |= code << live;
        live += bits;
        while (live >= 8) {
            pending_[written] = static_cast<std::byte>(buffered & 255U);
            ++written;
            buffered >>= 8U;
            live -= 8;
        }
    }
    bit_buffer_ = static_cast<std::uint32_t>(buffered);
    bit_count_ = live;
    pending_size_ = written;
    const bool result = write_bits(literal_codes[256], literal_lengths[256]);
    return result;
}
bool DeflateStream::encode_block(bool final) noexcept {
    const unsigned padding = (8 - ((bit_count_ + 3) % 8)) % 8;
    const std::uint64_t stored_cost = 3 + padding + 32 + 8 * block_live_;
    unsigned type = 0;
    if (level_ != 0) {
        if (!tokenize()) return false;
        std::uint64_t best_cost = 3 + extra_cost_;
        // A dynamic header needs at least 29 bits before its encoded length
        // sequence; every used literal/length and distance needs at least one.
        // This lower bound can rule it out without building any Huffman tree.
        std::uint64_t dynamic_lower_bound = 29 + extra_cost_;
        for (unsigned symbol = 0; symbol < 286; ++symbol) {
            best_cost += literal_frequency_[symbol] * fixed_length(symbol);
            dynamic_lower_bound += literal_frequency_[symbol];
        }
        for (unsigned symbol = 0; symbol < 30; ++symbol) {
            best_cost += distance_frequency_[symbol] * 5;
            dynamic_lower_bound += distance_frequency_[symbol];
        }
        if (final) {
            best_cost += (8 - ((bit_count_ + best_cost) % 8)) % 8;
            dynamic_lower_bound += (8 - ((bit_count_ + dynamic_lower_bound) % 8)) % 8;
        }
        type = 1;
        if (options_.strategy != DeflateStrategy::fixed && dynamic_lower_bound < std::min(best_cost, stored_cost)) {
            if (!prepare_dynamic()) return false;
            std::uint64_t candidate_cost = dynamic_cost_;
            if (final) candidate_cost += (8 - ((bit_count_ + candidate_cost) % 8)) % 8;
            if (candidate_cost < best_cost) {
                best_cost = candidate_cost;
                type = 2;
            }
        }
        if (stored_cost <= best_cost) type = 0;
    }
    if (!write_bits((final ? 1U : 0U) | (type << 1U), 3)) return false;
    if (type == 0) {
        if (!align_bits()) return false;
        const unsigned length = static_cast<unsigned>(block_live_);
        if (!write_bits(length, 16) || !write_bits(length ^ 65535U, 16)) return false;
        if (block_live_ > pending_.size() - pending_size_) return fail(DeflateError::workspace_overflow);
        if (!charge(block_live_)) return false;
        std::memcpy(pending_.data() + pending_size_, data_.data() + history_live_, block_live_);
        pending_size_ += block_live_;
    } else if (type == 1) {
        if (!emit_tokens(false)) return false;
    } else {
        if (!write_bits(literal_count_ - 257, 5) || !write_bits(distance_count_ - 1, 5) ||
            !write_bits(code_count_ - 4, 4)) return false;
        for (unsigned index = 0; index < code_count_; ++index) {
            if (!write_bits(run_length_[code_order[index]], 3)) return false;
        }
        for (unsigned index = 0; index < run_count_; ++index) {
            const CodeRun run = runs_[index];
            if (!write_bits(run_code_[run.symbol], run_length_[run.symbol])) return false;
            if (!write_bits(run.extra, run.bits)) return false;
        }
        if (!emit_tokens(true)) return false;
    }
    if (!final) {
        const std::size_t end = history_live_ + block_live_;
        const std::size_t retained = std::min(window_size_, end);
        const std::size_t discarded = end - retained;
        if (discarded != 0) {
            if (!charge(retained)) return false;
            if (hash_ready_) {
                if (!charge(retained + hash_count_)) return false;
                rebase_links(std::span<const std::uint32_t>{previous_}.subspan(discarded, retained),
                    std::span<std::uint32_t>{previous_}.first(retained),
                    static_cast<unsigned>(discarded), static_cast<unsigned>(retained));
                const std::span<std::uint32_t> heads = std::span<std::uint32_t>{heads_}.first(hash_count_);
                rebase_links(heads, heads, static_cast<unsigned>(discarded), static_cast<unsigned>(retained));
            }
            std::memmove(data_.data(), data_.data() + discarded, retained);
        }
        history_live_ = retained;
    }
    block_live_ = 0;
    return true;
}
bool DeflateStream::finish_boundary(DeflateFlush flush) noexcept {
    if (flush == DeflateFlush::finish) {
        if (!encode_block(true) || !align_bits()) return false;
        if (options_.format == DeflateFormat::zlib) {
            const std::uint32_t checksum = (adler_high_ << 16U) | adler_low_;
            for (unsigned shift = 24; ; shift -= 8) {
                if (!write_bits((checksum >> shift) & 255U, 8)) return false;
                if (shift == 0) break;
            }
        }
        phase_ = Phase::finishing;
        return true;
    }
    if (block_live_ != 0 && !encode_block(false)) return false;
    // An empty stored block exposes all preceding data at a byte boundary.
    if (!write_bits(0, 3) || !align_bits() || !write_bits(0, 16) || !write_bits(65535, 16)) return false;
    phase_ = Phase::syncing;
    return true;
}

DeflateResult DeflateStream::process(std::span<const std::byte> input,
    std::span<std::byte> output, DeflateFlush flush) noexcept {
    std::size_t read = 0;
    std::size_t written = 0;
    DeflateStatus status = DeflateStatus::need_input;
    if (phase_ != Phase::failed && phase_ != Phase::done) {
        if (phase_ == Phase::idle) fail(DeflateError::invalid_state);
        else if ((flush != DeflateFlush::none && flush != DeflateFlush::sync && flush != DeflateFlush::finish) ||
            overlaps(input.data(), input.size(), output.data(), output.size()) ||
            overlaps(this, sizeof(*this), input.data(), input.size()) ||
            overlaps(this, sizeof(*this), output.data(), output.size())) fail(DeflateError::invalid_argument);
    }
    while (phase_ != Phase::failed && phase_ != Phase::done) {
        if (pending_read_ != pending_size_) {
            const std::uint64_t allowance = options_.output_limit.value - output_total_.value;
            if (allowance == 0) {
                fail(DeflateError::output_limit_exceeded);
                break;
            }
            if (written == output.size()) {
                status = DeflateStatus::need_output;
                break;
            }
            std::size_t count = std::min(pending_size_ - pending_read_, output.size() - written);
            if (count > allowance) count = static_cast<std::size_t>(allowance);
            if (!charge(count)) break;
            std::memcpy(output.data() + written, pending_.data() + pending_read_, count);
            pending_read_ += count;
            written += count;
            output_total_.value += count;
            if (pending_read_ != pending_size_) continue;
        }
        pending_read_ = 0;
        pending_size_ = 0;
        if (phase_ == Phase::finishing) {
            phase_ = Phase::done;
            break;
        }
        if (phase_ == Phase::syncing) {
            phase_ = Phase::active;
            status = DeflateStatus::flush_complete;
            break;
        }
        if (block_live_ == block_capacity) {
            if (read != input.size()) {
                if (!encode_block(false)) break;
                continue;
            }
            // Keep an exact full block until finality is known. Input/output
            // segmentation consequently cannot change the BFINAL decision.
            if (flush == DeflateFlush::none) break;
            if (!finish_boundary(flush)) break;
            continue;
        }
        if (read != input.size()) {
            const std::uint64_t allowance = options_.input_limit.value - input_total_.value;
            if (allowance == 0) {
                fail(DeflateError::input_limit_exceeded);
                break;
            }
            std::size_t count = std::min(block_capacity - block_live_, input.size() - read);
            if (count > allowance) count = static_cast<std::size_t>(allowance);
            if (!charge(2 * count)) break;
            std::memcpy(data_.data() + history_live_ + block_live_, input.data() + read, count);
            if (options_.format == DeflateFormat::zlib) adler(input.subspan(read, count));
            read += count;
            block_live_ += count;
            input_total_.value += count;
            continue;
        }
        if (flush == DeflateFlush::none) break;
        if (!finish_boundary(flush)) break;
    }
    if (phase_ == Phase::failed) status = DeflateStatus::error;
    if (phase_ == Phase::done) status = DeflateStatus::stream_end;
    const DeflateResult result{status, error_, read, written, input_total_, output_total_, work_total_};
    return result;
}

} // namespace stx_zlib
