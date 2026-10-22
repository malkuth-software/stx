// Exact match-index experiment, separate from the encoder's bounded level policy.
// Inputs are immutable, fully available buffers. All positions are inserted,
// including positions omitted by the query schedule. No platform intrinsics.
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace {
#ifndef STX_MATCH_INDEX_SHARE_PREFIXES
#define STX_MATCH_INDEX_SHARE_PREFIXES 1
#endif
constexpr bool share_prefixes = STX_MATCH_INDEX_SHARE_PREFIXES != 0;
constexpr std::uint32_t absent = UINT32_MAX;
constexpr std::uint32_t window = 32768;
struct Match final { unsigned length = 0; unsigned distance = 0; };
struct Work final {
    std::uint64_t visits = 0;
    std::uint64_t compared = 0;
    std::uint64_t writes = 0;
    std::uint64_t reused = 0;
    std::uint64_t root_insertions = 0;
    std::uint64_t branch_insertions = 0;
    std::uint64_t duplicate_insertions = 0;
    std::uint64_t root_expirations = 0;
    std::uint64_t branch_expirations = 0;
    std::uint64_t predictor_rejections = 0;
};
struct Carried final {
    std::uint32_t position = 0;
    std::uint32_t source = 0;
    unsigned length = 0;
};
void require(bool valid) { if (!valid) std::abort(); }
unsigned bucket(std::span<const std::uint8_t> input, std::uint32_t position) {
    const unsigned pair = (static_cast<unsigned>(input[position]) << 6) ^ input[position + 1];
    const unsigned result = ((pair << 6) ^ input[position + 2]) & 65535;
    return result;
}
template<bool Count>
unsigned extend(std::span<const std::uint8_t> input, std::uint32_t position,
    std::uint32_t candidate, unsigned length, unsigned maximum, Work& work) {
    while (maximum - length >= 8) {
        std::uint64_t a = 0;
        std::uint64_t b = 0;
        std::memcpy(&a, input.data() + position + length, 8);
        std::memcpy(&b, input.data() + candidate + length, 8);
        if constexpr (Count) work.compared += 8;
        if (a != b) {
            if constexpr (std::endian::native == std::endian::little) {
                length += static_cast<unsigned>(std::countr_zero(a ^ b)) / 8;
                return length;
            } else if constexpr (std::endian::native == std::endian::big) {
                length += static_cast<unsigned>(std::countl_zero(a ^ b)) / 8;
                return length;
            }
            break;
        }
        length += 8;
    }
    while (length < maximum) {
        if constexpr (Count) ++work.compared;
        if (input[position + length] != input[candidate + length]) break;
        ++length;
    }
    return length;
}
struct Chain final {
    std::array<std::uint32_t, 65536> heads{};
    std::array<std::uint32_t, window> previous{};
    Carried carried{};
    void reset() { heads.fill(absent); carried = {}; }
    template<bool Count>
    Match advance(std::span<const std::uint8_t> input, std::uint32_t position, bool query, Work& work) {
        Match best{};
        const unsigned maximum = static_cast<unsigned>(std::min<std::size_t>(258, input.size() - position));
        if (maximum < 3) return best;
        const unsigned hash = bucket(input, position);
        const std::uint32_t first = heads[hash];
        const std::uint32_t oldest = position > window ? position - window : 0;
        const unsigned shift = position - carried.position;
        const unsigned shared = share_prefixes && shift < carried.length ? carried.length - shift : 0;
        const std::uint32_t source = carried.source + shift;
        if (query) {
            for (std::uint32_t candidate = first; candidate != absent && candidate >= oldest;
                 candidate = previous[candidate & (window - 1)]) {
                if constexpr (Count) ++work.visits;
                unsigned length = 0;
                // A winning match must pass this end check before extension.
                if (best.length >= 3) {
                    if constexpr (Count) ++work.compared;
                    if (input[position + best.length] != input[candidate + best.length]) continue;
                }
                const unsigned known = candidate == source ? shared : 0;
                if constexpr (Count) work.reused += known;
                length = extend<Count>(input, position, candidate, known, maximum, work);
                if (length >= 3 && length > best.length) best = Match{length, position - candidate};
                if (length == maximum) break;
            }
        }
        // Search precedes recycling: distance exactly 32768 is still legal.
        previous[position & (window - 1)] = first;
        heads[hash] = position;
        if constexpr (Count) work.writes += 2;
        if constexpr (share_prefixes) {
            if (query) carried = Carried{position, position - best.distance, best.length};
        }
        return best;
    }
};
struct Tree final {
    struct Children final { std::uint32_t left = absent; std::uint32_t right = absent; };
    std::array<std::uint32_t, 65536> heads{};
    std::array<Children, window> children{};
    Carried carried{};
    void reset() { heads.fill(absent); carried = {}; }
    template<bool Count>
    Match advance(std::span<const std::uint8_t> input, std::uint32_t position, bool query, Work& work) {
        Match best{};
        const unsigned maximum = static_cast<unsigned>(std::min<std::size_t>(258, input.size() - position));
        if (maximum < 3) return best;
        const unsigned hash = bucket(input, position);
        std::uint32_t candidate = heads[hash];
        const std::uint32_t oldest = position > window ? position - window : 0;
        const unsigned shift = position - carried.position;
        const unsigned shared = share_prefixes && shift < carried.length ? carried.length - shift : 0;
        const std::uint32_t source = carried.source + shift;
        // Split old nodes into the new root's lesser and greater trees. Local
        // roots keep the distance-window node intact until it is inspected.
        Children root{};
        std::uint32_t* lesser = &root.left;
        std::uint32_t* greater = &root.right;
        unsigned lower_prefix = 0;
        unsigned upper_prefix = 0;
        while (candidate != absent && candidate >= oldest) {
            if constexpr (Count) ++work.visits;
            unsigned known = std::min(lower_prefix, upper_prefix);
            // Carry only a proof for this particular source. Do not seed the
            // answer: newest-first traversal must still settle equal ties.
            if (candidate == source && shared > known) {
                if constexpr (Count) work.reused += shared - known;
                known = shared;
            }
            const unsigned length = extend<Count>(input, position, candidate, known, maximum, work);
            if ((query || share_prefixes) && length >= 3 && length > best.length) best = Match{length, position - candidate};
            Children& links = children[candidate & (window - 1)];
            if (length == maximum) {
                // Newest equal capped key dominates this node, including ties.
                // Read both children before any write may touch an old slot.
                const Children inherited = links;
                *lesser = inherited.left;
                *greater = inherited.right;
                if constexpr (Count) work.writes += 2;
                break;
            }
            if (input[candidate + length] < input[position + length]) {
                *lesser = candidate;
                lesser = &links.right;
                lower_prefix = length;
                candidate = links.right;
            } else {
                *greater = candidate;
                greater = &links.left;
                upper_prefix = length;
                candidate = links.left;
            }
            if constexpr (Count) ++work.writes;
        }
        if (candidate == absent || candidate < oldest) {
            *lesser = absent;
            *greater = absent;
            if constexpr (Count) work.writes += 2;
        }
        children[position & (window - 1)] = root;
        heads[hash] = position;
        if constexpr (Count) work.writes += 3;
        if constexpr (share_prefixes) carried = Carried{position, position - best.distance, best.length};
        if (!query) return {};
        return best;
    }
};
#ifdef STX_MATCH_INDEX_RADIX
#include "radix_index.hpp"
using Candidate = Radix;
#else
using Candidate = Tree;
#endif
Match brute(std::span<const std::uint8_t> input, std::uint32_t position) {
    Match best{};
    const unsigned maximum = static_cast<unsigned>(std::min<std::size_t>(258, input.size() - position));
    const unsigned distances = std::min(position, window);
    for (unsigned distance = 1; distance <= distances; ++distance) {
        unsigned length = 0;
        while (length < maximum && input[position + length] == input[position - distance + length]) ++length;
        if (length >= 3 && length > best.length) best = Match{length, distance};
        if (length == maximum) break;
    }
    return best;
}
template<class Index, bool Count>
// The mutable index is separately allocated and disjoint from every span and
// the counter record. Apply the same optional alias contract to both indexes.
std::uint64_t run(Index&
#ifdef STX_MATCH_INDEX_RESTRICT
    __restrict__
#endif
    index, std::span<const std::uint8_t> input,
    std::span<const std::uint8_t> queries, std::span<const Match> answers, Work& work) {
    index.reset();
    std::uint64_t checksum = 0;
    for (std::uint32_t position = 0; position < input.size(); ++position) {
        const Match match = index.template advance<Count>(input, position, queries[position] != 0, work);
        if constexpr (Count) {
            if (match.length != answers[position].length || match.distance != answers[position].distance) {
                std::fprintf(stderr, "match disagreement position=%u actual=%u/%u expected=%u/%u\n",
                    position, match.length, match.distance, answers[position].length, answers[position].distance);
            }
            require(match.length == answers[position].length && match.distance == answers[position].distance);
        }
        checksum += static_cast<std::uint64_t>(match.length) * 32769 + match.distance;
    }
    return checksum;
}
template<class Index>
double timed(Index& index, std::span<const std::uint8_t> input,
    std::span<const std::uint8_t> queries, std::span<const Match> answers, std::uint64_t expected) {
    Work unused{};
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    double elapsed = 0;
    unsigned repetitions = 0;
    do {
        const std::uint64_t checksum = run<Index, false>(index, input, queries, answers, unused);
        require(checksum == expected);
        ++repetitions;
        const std::chrono::steady_clock::time_point finish = std::chrono::steady_clock::now();
        elapsed = std::chrono::duration<double>(finish - start).count();
    } while (elapsed < 0.05);
    const double seconds = elapsed / repetitions;
    return seconds;
}
std::uint32_t random_next(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
void verify_case(std::span<const std::uint8_t> input, Chain& chain, Candidate& candidate) {
    chain.reset();
    candidate.reset();
    Work ignored{};
    for (std::uint32_t position = 0; position < input.size(); ++position) {
        const Match expected = chain.advance<false>(input, position, true, ignored);
        const Match actual = candidate.advance<false>(input, position, true, ignored);
        if (actual.length != expected.length || actual.distance != expected.distance) {
            std::fprintf(stderr, "fixture disagreement bytes=%zu position=%u actual=%u/%u expected=%u/%u\n",
                input.size(), position, actual.length, actual.distance, expected.length, expected.distance);
            std::abort();
        }
        if (input.size() <= 513 || position == window || position == 2 * window || position + 258 == input.size()) {
            const Match scalar = brute(input, position);
            require(actual.length == scalar.length && actual.distance == scalar.distance);
        }
    }
}
void verify_boundaries(Chain& chain, Candidate& candidate) {
    std::vector<std::uint8_t> input(513);
    // Short keys include every EOF padding length, equal-prefix ties, word
    // boundaries, small alphabets and nonzero final bytes followed by padding.
    for (unsigned kind = 0; kind < 4; ++kind) {
        std::uint32_t state = 0x91e10da5;
        for (std::size_t position = 0; position < input.size(); ++position) {
            const unsigned value = random_next(state);
            if (kind == 0) input[position] = 0;
            else if (kind == 1) input[position] = static_cast<std::uint8_t>(position % 7);
            else if (kind == 2) input[position] = static_cast<std::uint8_t>(value & 3);
            else input[position] = static_cast<std::uint8_t>(value);
        }
        for (std::size_t length = 0; length <= 260; ++length) {
            const std::span<const std::uint8_t> bytes{input};
            verify_case(bytes.first(length), chain, candidate);
        }
        verify_case(input, chain, candidate);
    }
    input.resize(2 * window + 259);
    constexpr std::array<unsigned, 4> periods{1, 7, window - 1, window};
    for (const unsigned period : periods) {
        std::uint32_t state = 0x91e10da5;
        for (std::size_t position = 0; position < input.size(); ++position) {
            if (position < period) input[position] = static_cast<std::uint8_t>(random_next(state));
            else input[position] = input[position - period];
        }
        verify_case(input, chain, candidate);
    }
    std::fprintf(stderr, "boundary_fixtures=1052 exact_answers=PASS\n");
}
void experiment(const char* name, std::span<const std::uint8_t> input, bool dense,
    Chain& chain, Candidate& tree) {
    std::vector<std::uint8_t> queries(input.size(), 0);
    std::vector<Match> answers(input.size());
    chain.reset();
    std::uint32_t next = 0;
    Work ignored{};
    unsigned query_count = 0;
    for (std::uint32_t position = 0; position < input.size(); ++position) {
        const bool query = dense || position == next;
        queries[position] = query ? 1 : 0;
        const Match match = chain.advance<false>(input, position, query, ignored);
        answers[position] = match;
        if (query) {
            ++query_count;
            next = position + std::max(1U, match.length);
            // Independent scalar enumeration also checks tails and first keys.
            if (position < 1024 || position % 4093 == 0 || position + 258 >= input.size() ||
                position == 32768 || position == 65536) {
                const Match expected = brute(input, position);
                require(match.length == expected.length && match.distance == expected.distance);
            }
        }
    }
    Work chain_work{};
    Work tree_work{};
    const std::uint64_t checksum = run<Chain, true>(chain, input, queries, answers, chain_work);
    require(run<Candidate, true>(tree, input, queries, answers, tree_work) == checksum);
#ifdef STX_MATCH_INDEX_RADIX
    std::fprintf(stderr, "radix_work workload=%s schedule=%s root_insertions=%llu branch_insertions=%llu duplicates=%llu root_expirations=%llu branch_expirations=%llu predictor_rejections=%llu\n",
        name, dense ? "every-byte" : "greedy",
        static_cast<unsigned long long>(tree_work.root_insertions),
        static_cast<unsigned long long>(tree_work.branch_insertions),
        static_cast<unsigned long long>(tree_work.duplicate_insertions),
        static_cast<unsigned long long>(tree_work.root_expirations),
        static_cast<unsigned long long>(tree_work.branch_expirations),
        static_cast<unsigned long long>(tree_work.predictor_rejections));
#endif
    // Warm both indexes, then alternate paired samples. Allocation and exact
    // answer checks stay outside timing; reset and skipped-position work stay in.
    timed(chain, input, queries, answers, checksum);
    timed(tree, input, queries, answers, checksum);
    std::array<double, 9> ratios{};
    std::array<double, 9> chain_times{};
    std::array<double, 9> tree_times{};
    for (unsigned sample = 0; sample < ratios.size(); ++sample) {
        if (sample % 2 == 0) {
            chain_times[sample] = timed(chain, input, queries, answers, checksum);
            tree_times[sample] = timed(tree, input, queries, answers, checksum);
        } else {
            tree_times[sample] = timed(tree, input, queries, answers, checksum);
            chain_times[sample] = timed(chain, input, queries, answers, checksum);
        }
        ratios[sample] = chain_times[sample] / tree_times[sample];
    }
    std::sort(ratios.begin(), ratios.end());
    std::sort(chain_times.begin(), chain_times.end());
    std::sort(tree_times.begin(), tree_times.end());
    std::printf("%s,%s,%zu,%u,%.9f,%.9f,%.4f,%.4f,%.4f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
        name, dense ? "every-byte" : "greedy", input.size(), query_count,
        chain_times[4], tree_times[4], ratios[4], ratios[0], ratios[8],
        static_cast<unsigned long long>(chain_work.visits), static_cast<unsigned long long>(tree_work.visits),
        static_cast<unsigned long long>(chain_work.compared), static_cast<unsigned long long>(tree_work.compared),
        static_cast<unsigned long long>(chain_work.writes), static_cast<unsigned long long>(tree_work.writes),
        static_cast<unsigned long long>(chain_work.reused), static_cast<unsigned long long>(tree_work.reused));
    std::fflush(stdout);
}
} // namespace

int main(int argc, char** argv) {
    std::unique_ptr<Chain> chain = std::make_unique<Chain>();
    std::unique_ptr<Candidate> tree = std::make_unique<Candidate>();
    std::fprintf(stderr, "chain_index_bytes=%zu tree_index_bytes=%zu\n", sizeof(Chain), sizeof(Candidate));
    verify_boundaries(*chain, *tree);
    if (argc == 2 && std::strcmp(argv[1], "--verify-only") == 0) return 0;
    std::puts("workload,schedule,bytes,queries,chain_seconds,tree_seconds,tree_speed_ratio,ratio_min,ratio_max,chain_visits,tree_visits,chain_compared,tree_compared,chain_writes,tree_writes,chain_reused,tree_reused");
    constexpr std::array<const char*, 4> names{"entropy", "uniform", "late-mismatch", "crowded-buckets"};
    std::vector<std::uint8_t> input(131072);
    bool files_only = false;
    bool greedy_only = false;
    bool random_only = false;
    bool random_sweep = false;
    int first_file = 1;
    while (first_file < argc) {
        if (std::strcmp(argv[first_file], "--files-only") == 0) files_only = true;
        else if (std::strcmp(argv[first_file], "--greedy-only") == 0) greedy_only = true;
        else if (std::strcmp(argv[first_file], "--random-only") == 0) random_only = true;
        else if (std::strcmp(argv[first_file], "--random-sweep") == 0) random_sweep = true;
        else break;
        ++first_file;
    }
    const unsigned kinds = files_only ? 0U : static_cast<unsigned>(names.size());
    for (unsigned kind = 0; kind < kinds; ++kind) {
        if (random_only && kind != 0) continue;
        std::uint32_t state = 0x91e10da5;
        for (std::size_t position = 0; position < input.size(); ++position) {
            const unsigned value = random_next(state);
            if (kind == 0) input[position] = static_cast<std::uint8_t>(value);
            else if (kind == 1) input[position] = 0;
            else if (kind == 2) input[position] = position % 259 < 255 ? 65 : static_cast<std::uint8_t>(value);
            else {
                const std::size_t part = position % 3;
                if (part == 0) input[position] = static_cast<std::uint8_t>(value & 0xf3);
                else if (part == 1) input[position] = static_cast<std::uint8_t>(((input[position - 1] & 3U) << 6) | (value & 3U));
                else input[position] = static_cast<std::uint8_t>((input[position - 1] & 3) << 6);
            }
        }
        if (!greedy_only) experiment(names[kind], input, true, *chain, *tree);
        experiment(names[kind], input, false, *chain, *tree);
    }
    if (random_sweep) {
        constexpr std::array<std::uint32_t, 3> seeds{0x12345678U, 0x9e3779b9U, 0xdeadbeefU};
        constexpr std::array<std::size_t, 2> sizes{131072, 1048576};
        for (const std::size_t size : sizes) {
            input.resize(size);
            for (const std::uint32_t seed : seeds) {
                std::uint32_t state = seed;
                for (std::size_t position = 0; position < input.size(); ++position) {
                    input[position] = static_cast<std::uint8_t>(random_next(state));
                }
                std::array<char, 64> name{};
                std::snprintf(name.data(), name.size(), "random-%08x-%zu", seed, size);
                if (!greedy_only) experiment(name.data(), input, true, *chain, *tree);
                experiment(name.data(), input, false, *chain, *tree);
            }
        }
    }
    // Optional extracted PNG filter bytes, read before any timed operation.
    for (int file = first_file; file < argc; ++file) {
        std::FILE* source = std::fopen(argv[file], "rb");
        require(source != nullptr);
        require(std::fseek(source, 0, SEEK_END) == 0);
        const long length = std::ftell(source);
        require(length >= 0 && length <= 16 * 1024 * 1024);
        require(std::fseek(source, 0, SEEK_SET) == 0);
        input.resize(static_cast<std::size_t>(length));
        require(std::fread(input.data(), 1, input.size(), source) == input.size());
        require(std::fclose(source) == 0);
        if (!greedy_only) experiment(argv[file], input, true, *chain, *tree);
        experiment(argv[file], input, false, *chain, *tree);
    }
    return 0;
}
