#include "setup.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
namespace stx_vorbis::detail {
namespace {
std::uint32_t read(BitReader& reader, const unsigned int bits) {
    std::uint32_t value = 0;
    require(reader.read(bits, value), Status::invalid_header, reader.position());
    return value;
}
double unpack_float(const std::uint32_t packed) {
    const int exponent = static_cast<int>((packed >> 21) & 1023U) - 788;
    double mantissa = static_cast<double>(packed & 0x1fffffU);
    if ((packed & 0x80000000U) != 0) mantissa = -mantissa;
    const double value = std::ldexp(mantissa, exponent);
    require(std::isfinite(value));
    return value;
}
bool power_fits(const std::uint32_t base, const std::uint32_t exponent, const std::uint32_t limit) noexcept {
    std::uint32_t value = 1;
    for (std::uint32_t index = 0; index < exponent; ++index) {
        if (base != 0 && value > limit / base) return false;
        value *= base;
    }
    return value <= limit;
}
std::uint32_t lookup_count(const std::uint32_t entries, const std::uint32_t dimensions) noexcept {
    std::uint32_t low = 1;
    std::uint32_t high = entries;
    while (low < high) {
        const std::uint32_t middle = low + (high - low + 1) / 2;
        if (power_fits(middle, dimensions, entries)) low = middle;
        else high = middle - 1;
    }
    return low;
}
void valid_book(const Setup& setup, const unsigned int index, const bool vector) {
    require(index < setup.books.size());
    const Codebook& book = setup.books[index];
    require(book.used != 0);
    if (vector) require(!book.values.empty());
}
void parse_book(Codebook& book, BitReader& reader, const Limits& limits,
                std::pmr::memory_resource* const memory) {
    require(read(reader, 24) == 0x564342U);
    book.dimensions = read(reader, 16);
    book.entries = read(reader, 24);
    require(book.dimensions > 0 && book.entries > 0);
    require(book.entries <= limits.codebook_entries, Status::resource_limit);
    std::pmr::vector<std::uint8_t> lengths(book.entries, 0, memory);
    if (read(reader, 1) != 0) {
        unsigned int length = read(reader, 5) + 1;
        std::uint32_t index = 0;
        while (index < book.entries) {
            require(length <= 32);
            const unsigned int bits = ilog(book.entries - index);
            const std::uint32_t count = read(reader, bits);
            require(count <= book.entries - index);
            for (std::uint32_t offset = 0; offset < count; ++offset) lengths[index + offset] = static_cast<std::uint8_t>(length);
            index += count;
            ++length;
        }
    } else {
        const bool sparse = read(reader, 1) != 0;
        for (std::uint32_t index = 0; index < book.entries; ++index) {
            if (!sparse || read(reader, 1) != 0) lengths[index] = static_cast<std::uint8_t>(read(reader, 5) + 1);
        }
    }
    build_huffman(book, lengths);
    const unsigned int lookup = read(reader, 4);
    require(lookup <= 2);
    if (lookup == 0) return;
    const double minimum = unpack_float(read(reader, 32));
    const double delta = unpack_float(read(reader, 32));
    const unsigned int value_bits = read(reader, 4) + 1;
    const bool sequence = read(reader, 1) != 0;
    const std::size_t expanded = product(book.entries, book.dimensions, limits.lookup_values);
    const std::size_t count = lookup == 1 ? lookup_count(book.entries, book.dimensions) : expanded;
    std::pmr::vector<std::uint32_t> multiplicands(count, 0, memory);
    for (std::size_t index = 0; index < count; ++index) multiplicands[index] = read(reader, value_bits);
    book.values.resize(expanded);
    for (std::uint32_t entry = 0; entry < book.entries; ++entry) {
        double last = 0;
        std::uint32_t quotient = entry;
        for (std::uint32_t dimension = 0; dimension < book.dimensions; ++dimension) {
            const std::size_t destination = std::size_t{entry} * book.dimensions + dimension;
            std::size_t index = destination;
            if (lookup == 1) { index = quotient % count; quotient /= static_cast<std::uint32_t>(count); }
            const double value = static_cast<double>(multiplicands[index]) * delta + minimum + last;
            require(std::isfinite(value));
            book.values[destination] = value;
            if (sequence) last = value;
        }
    }
}
double bark(const double frequency) noexcept {
    const double result = 13.1 * std::atan(0.00074 * frequency)
        + 2.24 * std::atan(1.85e-8 * frequency * frequency) + 0.0001 * frequency;
    return result;
}
void prepare_bark(std::pmr::vector<double>& destination, const Floor& floor, const unsigned int block) {
    const unsigned int count = block / 2;
    destination.resize(count);
    const double scale = static_cast<double>(floor.bark_size) / bark(static_cast<double>(floor.rate) / 2);
    for (unsigned int index = 0; index < count; ++index) {
        const double frequency = static_cast<double>(floor.rate) * index / (2 * count);
        const double mapped = std::floor(bark(frequency) * scale);
        const double bin = std::min(mapped, static_cast<double>(floor.bark_size - 1));
        destination[index] = 2 * std::cos(std::numbers::pi * bin / floor.bark_size);
    }
}
void parse_floor(Floor& floor, BitReader& reader, const Setup& setup) {
    floor.type = read(reader, 16);
    require(floor.type <= 1);
    if (floor.type == 0) {
        floor.order = read(reader, 8); floor.rate = read(reader, 16); floor.bark_size = read(reader, 16);
        floor.amplitude_bits = read(reader, 6); floor.amplitude_db = read(reader, 8);
        floor.book_count = read(reader, 4) + 1;
        require(floor.order > 0 && floor.rate > 0 && floor.bark_size > 0);
        for (unsigned int index = 0; index < floor.book_count; ++index) {
            floor.books[index] = read(reader, 8);
            valid_book(setup, floor.books[index], true);
        }
        prepare_bark(floor.bark0, floor, setup.identification.blocks[0]);
        prepare_bark(floor.bark1, floor, setup.identification.blocks[1]);
        return;
    }
    floor.partitions = read(reader, 5);
    unsigned int classes = 0;
    for (unsigned int index = 0; index < floor.partitions; ++index) {
        const unsigned int value = read(reader, 4);
        floor.partition_class[index] = value;
        classes = std::max(classes, value + 1);
    }
    for (unsigned int index = 0; index < classes; ++index) {
        FloorClass& value = floor.classes[index];
        value.dimensions = read(reader, 3) + 1;
        value.subclasses = read(reader, 2);
        if (value.subclasses != 0) {
            value.master = read(reader, 8);
            valid_book(setup, value.master, false);
        }
        const unsigned int count = 1U << value.subclasses;
        for (unsigned int child = 0; child < count; ++child) {
            value.books[child] = static_cast<int>(read(reader, 8)) - 1;
            if (value.books[child] >= 0) valid_book(setup, static_cast<unsigned int>(value.books[child]), false);
        }
    }
    floor.multiplier = read(reader, 2) + 1;
    const unsigned int bits = read(reader, 4);
    floor.x[0] = 0; floor.x[1] = 1 << bits; floor.points = 2;
    for (unsigned int part = 0; part < floor.partitions; ++part) {
        const FloorClass& value = floor.classes[floor.partition_class[part]];
        for (unsigned int index = 0; index < value.dimensions; ++index) {
            require(floor.points < floor.x.size());
            floor.x[floor.points] = static_cast<int>(read(reader, bits));
            ++floor.points;
        }
    }
    for (unsigned int index = 0; index < floor.points; ++index) floor.sorted[index] = index;
    // At most 250 points: stable insertion also makes duplicate rejection explicit.
    for (unsigned int index = 1; index < floor.points; ++index) {
        const unsigned int point = floor.sorted[index];
        unsigned int position = index;
        while (position > 0 && floor.x[floor.sorted[position - 1]] > floor.x[point]) {
            floor.sorted[position] = floor.sorted[position - 1]; --position;
        }
        if (position > 0) require(floor.x[floor.sorted[position - 1]] != floor.x[point]);
        floor.sorted[position] = point;
    }
    for (unsigned int index = 2; index < floor.points; ++index) {
        unsigned int low = 0; unsigned int high = 1;
        for (unsigned int previous = 0; previous < index; ++previous) {
            const int x = floor.x[previous];
            if (x < floor.x[index] && x > floor.x[low]) low = previous;
            if (x > floor.x[index] && x < floor.x[high]) high = previous;
        }
        floor.low[index] = low; floor.high[index] = high;
    }
}
}
void SetupDeleter::operator()(const Setup* const setup) const noexcept {
    std::destroy_at(setup);
    (*memory).deallocate(const_cast<Setup*>(setup), sizeof(Setup), alignof(Setup));
}
void validate_header(const std::span<const std::uint8_t> packet, const unsigned int type) {
    require(packet.size() >= 7 && packet[0] == type);
    require(std::memcmp(packet.data() + 1, "vorbis", 6) == 0);
}
Identification parse_identification(const std::span<const std::uint8_t> packet, const Limits& limits) {
    validate_header(packet, 1);
    BitReader reader(packet.subspan(7));
    require(read(reader, 32) == 0);
    Identification result{};
    result.channels = read(reader, 8); result.rate = read(reader, 32);
    require(result.channels > 0 && result.rate > 0);
    require(result.channels <= limits.channels, Status::resource_limit);
    read(reader, 32); read(reader, 32); read(reader, 32);
    const unsigned int small = read(reader, 4); const unsigned int large = read(reader, 4);
    require(small >= 6 && large >= small && large <= 13);
    result.blocks = {1U << small, 1U << large};
    require(read(reader, 1) == 1);
    return result;
}
void build_huffman(Codebook& book, const std::span<const std::uint8_t> lengths) {
    std::array<std::uint64_t, 33> markers{};
    std::uint64_t kraft = 0;
    book.nodes.clear(); book.nodes.emplace_back(); book.used = 0; book.prefix.fill(Prefix{});
    for (std::size_t index = 0; index < lengths.size(); ++index) {
        const unsigned int length = lengths[index];
        require(length <= 32);
        if (length == 0) continue;
        ++book.used;
        kraft += std::uint64_t{1} << (32 - length);
        require(kraft <= (std::uint64_t{1} << 32));
        const std::uint64_t code = markers[length];
        require(code < (std::uint64_t{1} << length));
        for (unsigned int level = length; level > 0; --level) {
            if ((markers[level] & 1U) != 0) {
                if (level == 1) ++markers[level];
                else markers[level] = markers[level - 1] << 1;
                break;
            }
            ++markers[level];
        }
        std::uint64_t previous = code;
        for (unsigned int level = length + 1; level <= 32; ++level) {
            if ((markers[level] >> 1) != previous) break;
            previous = markers[level];
            markers[level] = markers[level - 1] << 1;
        }
        std::size_t node = 0;
        std::uint32_t prefix = 0;
        for (unsigned int bit = 0; bit < length; ++bit) {
            require(book.nodes[node].entry < 0);
            const unsigned int direction = static_cast<unsigned int>((code >> (length - bit - 1)) & 1U);
            prefix |= direction << bit;
            std::int32_t child = book.nodes[node].children[direction];
            if (child < 0) {
                require(book.nodes.size() < static_cast<std::size_t>(INT32_MAX), Status::resource_limit);
                child = static_cast<std::int32_t>(book.nodes.size());
                book.nodes[node].children[direction] = child;
                book.nodes.emplace_back();
            }
            node = static_cast<std::size_t>(child);
        }
        require(book.nodes[node].entry < 0 && book.nodes[node].children[0] < 0 && book.nodes[node].children[1] < 0);
        book.nodes[node].entry = static_cast<std::int32_t>(index);
        if (length <= 10) {
            const unsigned int increment = 1U << length;
            for (unsigned int value = prefix; value < 1024; value += increment)
                book.prefix[value] = Prefix{static_cast<std::int32_t>(index), static_cast<std::uint8_t>(length)};
        }
    }
    require(kraft == (std::uint64_t{1} << 32) || book.used == 0 || (book.used == 1 && kraft == (std::uint64_t{1} << 31)));
}
Status Codebook::decode(BitReader& reader, std::uint32_t& entry) const noexcept {
    std::uint32_t value = 0;
    if (reader.peek(10, value)) {
        const Prefix& found = prefix[value];
        if (found.bits != 0) {
            const bool skipped = reader.skip(found.bits);
            (void)skipped;
            entry = static_cast<std::uint32_t>(found.entry);
            return Status::ok;
        }
    }
    std::size_t node = 0;
    for (unsigned int depth = 0; depth < 32; ++depth) {
        if (!reader.read(1, value)) return Status::truncated;
        const std::int32_t child = nodes[node].children[value];
        if (child < 0) return Status::invalid_packet;
        node = static_cast<std::size_t>(child);
        if (nodes[node].entry >= 0) { entry = static_cast<std::uint32_t>(nodes[node].entry); return Status::ok; }
    }
    return Status::invalid_packet;
}
void parse_setup(Setup& setup, const std::span<const std::uint8_t> packet, const Limits& limits,
                 std::pmr::memory_resource* const memory, const Synthesis synthesis) {
    require(synthesis_available(synthesis), Status::unsupported);
    validate_header(packet, 5);
    BitReader reader(packet.subspan(7));
    const unsigned int books = read(reader, 8) + 1;
    setup.books.reserve(books);
    for (unsigned int index = 0; index < books; ++index) {
        setup.books.emplace_back(memory);
        parse_book(setup.books.back(), reader, limits, memory);
    }
    const unsigned int times = read(reader, 6) + 1;
    for (unsigned int index = 0; index < times; ++index) require(read(reader, 16) == 0);
    const unsigned int floors = read(reader, 6) + 1;
    setup.floors.reserve(floors);
    for (unsigned int index = 0; index < floors; ++index) {
        setup.floors.emplace_back(memory);
        parse_floor(setup.floors.back(), reader, setup);
    }
    const unsigned int residues = read(reader, 6) + 1;
    setup.residues.resize(residues);
    for (Residue& residue : setup.residues) {
        residue.type = read(reader, 16); require(residue.type <= 2);
        residue.begin = read(reader, 24); residue.end = read(reader, 24);
        require(residue.begin <= residue.end);
        residue.partition_size = read(reader, 24) + 1;
        residue.classifications = read(reader, 6) + 1;
        residue.classbook = read(reader, 8); valid_book(setup, residue.classbook, false);
        std::array<unsigned int, 64> cascade{};
        for (unsigned int index = 0; index < residue.classifications; ++index) {
            cascade[index] = read(reader, 3);
            if (read(reader, 1) != 0) cascade[index] |= read(reader, 5) << 3;
        }
        for (unsigned int index = 0; index < residue.classifications; ++index) {
            for (unsigned int stage = 0; stage < 8; ++stage) {
                residue.books[index][stage] = -1;
                if ((cascade[index] & (1U << stage)) == 0) continue;
                const unsigned int book = read(reader, 8);
                valid_book(setup, book, true);
                residue.books[index][stage] = static_cast<int>(book);
            }
        }
    }
    const unsigned int mappings = read(reader, 6) + 1;
    setup.mappings.reserve(mappings);
    for (unsigned int index = 0; index < mappings; ++index) {
        require(read(reader, 16) == 0);
        setup.mappings.emplace_back(memory);
        Mapping& mapping = setup.mappings.back();
        mapping.submaps = read(reader, 1) != 0 ? read(reader, 4) + 1 : 1;
        if (read(reader, 1) != 0) {
            const unsigned int count = read(reader, 8) + 1;
            mapping.couplings.resize(count);
            const unsigned int bits = ilog(setup.identification.channels - 1);
            for (Coupling& coupling : mapping.couplings) {
                coupling.magnitude = read(reader, bits); coupling.angle = read(reader, bits);
                require(coupling.magnitude != coupling.angle && coupling.magnitude < setup.identification.channels
                        && coupling.angle < setup.identification.channels);
            }
        }
        require(read(reader, 2) == 0);
        if (mapping.submaps > 1) {
            for (unsigned int channel = 0; channel < setup.identification.channels; ++channel) {
                mapping.mux[channel] = read(reader, 4); require(mapping.mux[channel] < mapping.submaps);
            }
        }
        for (unsigned int submap = 0; submap < mapping.submaps; ++submap) {
            read(reader, 8); // Time submap is reserved but explicitly unused in Vorbis I.
            mapping.floors[submap] = read(reader, 8); mapping.residues[submap] = read(reader, 8);
            require(mapping.floors[submap] < floors && mapping.residues[submap] < residues);
        }
    }
    const unsigned int modes = read(reader, 6) + 1;
    setup.modes.resize(modes);
    for (Mode& mode : setup.modes) {
        mode.large = read(reader, 1) != 0;
        require(read(reader, 16) == 0 && read(reader, 16) == 0);
        mode.mapping = read(reader, 8); require(mode.mapping < mappings);
    }
    require(read(reader, 1) == 1);
    setup.synthesis = synthesis;
    for (unsigned int index = 0; index < 2; ++index) {
        const unsigned int block = setup.identification.blocks[index];
        if (synthesis == Synthesis::automatic) {
            setup.bfft_plans[index].emplace(block, *memory);
            prepare_window(setup.transforms[index], block);
        } else {
            prepare_transform(setup.transforms[index], block);
        }
    }
    for (unsigned int index = 0; index < 256; ++index)
        setup.inverse_db[index] = std::exp((static_cast<double>(index) - 255) * (140.0 / 256.0) * (std::log(10.0) / 20.0));
}
} // namespace stx_vorbis::detail
