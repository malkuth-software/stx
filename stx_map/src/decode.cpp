// The TMAP 2 decoder (docs/FORMAT.md). It follows tools/bake/tmap_decode.js
// step for step; tests/map.cpp checks the two agree on the embedded file.
// Integer arithmetic only: the heading table is data (heading_table.inc,
// written by tools/bake/make_heading_table.js and checked there against the
// encoder's table), so no floating point and no fused multiply-add can
// change a position.

#include "stx_map/map.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace stx_map {
namespace {

#include "heading_table.inc"

constexpr std::uint32_t heading_count = 4096U;
constexpr std::uint32_t range_top = 1U << 23U;
constexpr std::uint32_t range_initial = 0x7FFFFFFFU;
constexpr std::uint16_t probability_half = 16384U;
constexpr std::uint16_t settle_shift = 5U;
constexpr std::uint64_t hilbert_cells = 1ULL << 30U;
constexpr std::int64_t coordinate_limit = 1LL << 28U;
constexpr std::size_t fixed_header_bytes = 33U;
constexpr std::size_t layer_record_bytes = 23U; // without the name
constexpr std::size_t check_bytes = 4U;

// cos(2 pi k / 4096) * 16384, rounded, from the table's quarter wave.
std::int32_t cosine_at(std::uint32_t heading) noexcept {
    const std::uint32_t k = heading & (heading_count - 1U);
    std::int32_t value = 0;
    if (k <= 1024U) {
        value = quarter_cosine[k];
    } else if (k <= 2048U) {
        value = -quarter_cosine[2048U - k];
    } else if (k <= 3072U) {
        value = -quarter_cosine[k - 2048U];
    } else {
        value = quarter_cosine[4096U - k];
    }
    return value;
}

std::int32_t sine_at(std::uint32_t heading) noexcept {
    const std::int32_t value = cosine_at(heading + heading_count - 1024U);
    return value;
}

// CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320).
struct CrcTable final {
    std::array<std::uint32_t, 256> entries{};
};

constexpr CrcTable make_crc_table() noexcept {
    CrcTable table;
    for (std::uint32_t n = 0U; n < 256U; ++n) {
        std::uint32_t c = n;
        for (std::uint32_t k = 0U; k < 8U; ++k) c = (c & 1U) != 0U ? (0xEDB88320U ^ (c >> 1U)) : (c >> 1U);
        table.entries[n] = c;
    }
    return table;
}

constexpr CrcTable crc_table = make_crc_table();

} // namespace

std::uint32_t crc32(std::span<const std::byte> bytes) noexcept {
    std::uint32_t c = 0xFFFFFFFFU;
    for (std::size_t i = 0U; i < bytes.size(); ++i) {
        const std::uint32_t index = (c ^ static_cast<std::uint32_t>(bytes[i])) & 0xFFU;
        c = crc_table.entries[index] ^ (c >> 8U);
    }
    const std::uint32_t value = c ^ 0xFFFFFFFFU;
    return value;
}

namespace {

std::uint32_t byte_at(std::span<const std::byte> bytes, std::size_t at) noexcept {
    const std::uint32_t value = static_cast<std::uint32_t>(bytes[at]);
    return value;
}

std::uint32_t u16_at(std::span<const std::byte> bytes, std::size_t at) noexcept {
    const std::uint32_t value = byte_at(bytes, at) | (byte_at(bytes, at + 1U) << 8U);
    return value;
}

std::uint32_t u32_at(std::span<const std::byte> bytes, std::size_t at) noexcept {
    const std::uint32_t value = byte_at(bytes, at) | (byte_at(bytes, at + 1U) << 8U) | (byte_at(bytes, at + 2U) << 16U) |
        (byte_at(bytes, at + 3U) << 24U);
    return value;
}

std::uint32_t bits_for(std::uint32_t count) noexcept {
    std::uint32_t bits = 0U;
    while (count > 1U && bits < 32U && ((count - 1U) >> bits) != 0U) ++bits;
    return bits;
}

bool name_character(std::uint32_t c) noexcept {
    const bool fits = (c >= 0x61U && c <= 0x7AU) || (c >= 0x30U && c <= 0x39U) || c == 0x5FU;
    return fits;
}

// Adaptive bit probabilities: per bit the 15-bit chance of a 0 and its
// update count. N is the bit count of one model.
template<std::size_t N>
struct Probabilities final {
    std::array<std::uint16_t, N * 2U> entries{};
};

template<std::size_t N>
void reset(Probabilities<N>& model) noexcept {
    for (std::size_t i = 0U; i < N; ++i) {
        model.entries[i * 2U] = probability_half;
        model.entries[i * 2U + 1U] = 0U;
    }
}

// One layer's models, fresh per layer (tmap_decode.js: gapK, gapBits, endP,
// clsP, mantP, turnK, turnSide, turnBits).
struct LayerModels final {
    Probabilities<64> gap_length;
    Probabilities<128> gap_bits;
    Probabilities<16> end;
    Probabilities<256> length_class;
    Probabilities<256> mantissa;
    Probabilities<768> turn_length;
    Probabilities<48> turn_side;
    Probabilities<1024> turn_bits;
};

void reset(LayerModels& models) noexcept {
    reset(models.gap_length);
    reset(models.gap_bits);
    reset(models.end);
    reset(models.length_class);
    reset(models.mantissa);
    reset(models.turn_length);
    reset(models.turn_side);
    reset(models.turn_bits);
}

// The range decoder over one layer's body. Reads past the body's end give
// zero bytes, as the encoder's flush assumes. `failed` is set once a raw
// slot is out of range and stays set.
class RangeDecoder final {
public:
    explicit RangeDecoder(std::span<const std::byte> body) noexcept : body_(body) {
        for (std::uint32_t i = 0U; i < 4U; ++i) code_ = (code_ << 8U) | next();
        // The reference refuses a first code with its top bit set.
        if (code_ > range_initial) failed_ = true;
    }

    template<std::size_t N>
    std::uint32_t bit(Probabilities<N>& model, std::uint32_t index) noexcept {
        const std::size_t slot = static_cast<std::size_t>(index) * 2U;
        const std::uint32_t p = model.entries[slot];
        std::uint32_t shift = static_cast<std::uint32_t>(model.entries[slot + 1U]) + 1U;
        if (shift < settle_shift) {
            model.entries[slot + 1U] = static_cast<std::uint16_t>(shift);
        } else {
            shift = settle_shift;
        }
        const std::uint32_t bound = (range_ >> 15U) * p;
        std::uint32_t value = 0U;
        if (code_ < bound) {
            range_ = bound;
            model.entries[slot] = static_cast<std::uint16_t>(p + ((32768U - p) >> shift));
        } else {
            code_ -= bound;
            range_ -= bound;
            model.entries[slot] = static_cast<std::uint16_t>(p - (p >> shift));
            value = 1U;
        }
        normalize();
        return value;
    }

    // `count` raw bits (at most 31), up to 8 at a time.
    std::uint32_t raw(std::uint32_t count) noexcept {
        std::uint32_t value = 0U;
        while (count > 0U) {
            const std::uint32_t n = count > 8U ? 8U : count;
            count -= n;
            range_ >>= n;
            if (range_ == 0U) {
                failed_ = true;
                return 0U;
            }
            const std::uint32_t slot = code_ / range_;
            if ((slot >> n) != 0U) {
                failed_ = true;
                return 0U;
            }
            code_ -= slot * range_;
            value = (value << n) | slot;
            normalize();
        }
        return value;
    }

    template<std::size_t N>
    std::uint32_t tree(Probabilities<N>& model, std::uint32_t base, std::uint32_t count) noexcept {
        std::uint32_t node = 1U;
        for (std::uint32_t n = 0U; n < count; ++n) node = node * 2U + bit(model, base + node);
        const std::uint32_t value = node - (1U << count);
        return value;
    }

    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    std::uint32_t next() noexcept {
        if (at_ >= body_.size()) return 0U;
        const std::uint32_t value = static_cast<std::uint32_t>(body_[at_]);
        ++at_;
        return value;
    }

    void normalize() noexcept {
        while (range_ < range_top) {
            range_ <<= 8U;
            code_ = (code_ << 8U) | next();
        }
    }

    std::span<const std::byte> body_;
    std::size_t at_ = 0U;
    std::uint32_t range_ = range_initial;
    std::uint32_t code_ = 0U;
    bool failed_ = false;
};

// The order-15 Hilbert curve's cell at index d (d < 2^30).
struct Cell final {
    std::uint32_t x = 0U;
    std::uint32_t y = 0U;
};

Cell hilbert_cell(std::uint64_t d) noexcept {
    Cell cell;
    for (std::uint64_t s = 1U; s < 32768U; s <<= 1U) {
        const std::uint64_t quadrant = (d / (s * s)) & 3U;
        const std::uint32_t rx = static_cast<std::uint32_t>((quadrant >> 1U) & 1U);
        const std::uint32_t ry = static_cast<std::uint32_t>((quadrant ^ rx) & 1U);
        const std::uint32_t side = static_cast<std::uint32_t>(s);
        if (ry == 0U) {
            if (rx == 1U) {
                cell.x = side - 1U - cell.x;
                cell.y = side - 1U - cell.y;
            }
            const std::uint32_t swapped = cell.x;
            cell.x = cell.y;
            cell.y = swapped;
        }
        cell.x += side * rx;
        cell.y += side * ry;
    }
    return cell;
}

// A value as a bit length, two modelled bits under the top bit, and the rest
// raw (the starts' gaps and the turns' magnitudes).
template<std::size_t L, std::size_t B>
std::uint64_t read_split_value(RangeDecoder& decoder, Probabilities<L>& lengths, std::uint32_t length_base,
                               std::uint32_t length_bits, Probabilities<B>& bits, std::uint32_t bits_base_per_length,
                               std::uint32_t bits_offset, std::uint32_t& length_out) noexcept {
    const std::uint32_t k = decoder.tree(lengths, length_base, length_bits);
    length_out = k;
    if (k == 0U) return 0U;
    std::uint64_t value = 1U;
    const std::uint32_t modelled = k - 1U < 2U ? k - 1U : 2U;
    if (modelled > 0U) {
        const std::uint32_t top = decoder.tree(bits, (bits_offset + k) * bits_base_per_length, modelled);
        value = (value << modelled) | top;
    }
    const std::uint32_t rest = k - 1U - modelled;
    if (rest > 0U) {
        const std::uint32_t low = decoder.raw(rest);
        value = (value << rest) | low;
    }
    return value;
}

struct LayerInput final {
    const MapInfo* info = nullptr;
    std::uint32_t layer = 0U;
    std::span<const std::byte> body;
};

Status decode_layer(const LayerInput& input, std::span<const LayerStorage> storage) noexcept {
    const MapInfo& info = *input.info;
    const LayerInfo& layer = info.layers[input.layer];
    const LayerStorage& out = storage[input.layer];
    LayerModels models;
    reset(models);
    RangeDecoder decoder(input.body);
    const std::uint32_t mantissa_bits = info.mantissa_bits;
    const std::uint32_t mantissa_top = 1U << mantissa_bits;
    std::uint64_t hilbert = 0U;
    std::uint32_t n = 0U;
    for (std::uint32_t line = 0U; line < layer.lines; ++line) {
        // The start: a gap along the Hilbert curve over the start grid.
        std::uint32_t gap_length = 0U;
        const std::uint64_t gap =
            read_split_value(decoder, models.gap_length, 0U, 5U, models.gap_bits, 4U, 0U, gap_length);
        hilbert += gap;
        if (hilbert >= hilbert_cells) return Status::bad_body;
        if (n >= layer.vertices) return Status::bad_body;
        const Cell cell = hilbert_cell(hilbert);
        out.starts[line] = n;
        std::int64_t px = static_cast<std::int64_t>(cell.x) * layer.start_grid;
        std::int64_t py = static_cast<std::int64_t>(cell.y) * layer.start_grid;
        out.x[n] = static_cast<std::int32_t>(px);
        out.y[n] = static_cast<std::int32_t>(py);
        ++n;
        // The steps.
        std::uint32_t previous_class = 0U;
        std::uint32_t previous_size = 0U;
        std::uint32_t previous_side = 0U;
        std::uint32_t heading = 0U;
        for (std::uint32_t step = 0U;; ++step) {
            const std::uint32_t end_context = step < 15U ? step : 15U;
            if (decoder.bit(models.end, end_context) != 0U) break;
            if (decoder.failed()) return Status::bad_body;
            if (n >= layer.vertices) return Status::bad_body;
            const std::uint32_t length_class = decoder.tree(models.length_class, previous_class * 16U, 4U);
            const std::uint32_t mantissa = decoder.tree(models.mantissa, length_class * 16U, mantissa_bits);
            const std::uint32_t bins = info.turn_bins_log2[length_class];
            std::int64_t turn = 0;
            if (step == 0U) {
                turn = decoder.raw(bins);
            } else {
                const std::uint32_t context = length_class * 3U + previous_size;
                const std::uint32_t k = decoder.tree(models.turn_length, context * 16U, 4U);
                if (k > 0U) {
                    const std::uint32_t side = decoder.bit(models.turn_side, context) ^ previous_side;
                    previous_side = side;
                    std::uint64_t magnitude = 1U;
                    const std::uint32_t modelled = k - 1U < 2U ? k - 1U : 2U;
                    if (modelled > 0U) {
                        const std::uint32_t top = decoder.tree(models.turn_bits, (length_class * 16U + k) * 4U, modelled);
                        magnitude = (magnitude << modelled) | top;
                    }
                    const std::uint32_t rest = k - 1U - modelled;
                    if (rest > 0U) {
                        const std::uint32_t low = decoder.raw(rest);
                        magnitude = (magnitude << rest) | low;
                    }
                    turn = side != 0U ? -static_cast<std::int64_t>(magnitude) : static_cast<std::int64_t>(magnitude);
                }
                previous_size = k == 0U ? 0U : (k + 3U <= bins ? 1U : 2U);
            }
            const std::int64_t turn_units = turn * static_cast<std::int64_t>(heading_count >> bins);
            const std::int64_t turned = static_cast<std::int64_t>(heading) + turn_units;
            heading = static_cast<std::uint32_t>(turned & static_cast<std::int64_t>(heading_count - 1U));
            const std::int64_t units = length_class == 0U ? static_cast<std::int64_t>(mantissa)
                : static_cast<std::int64_t>(mantissa_top + mantissa) << (length_class - 1U);
            const std::int64_t length = units * static_cast<std::int64_t>(info.length_unit);
            // Round half up, as Math.round in the reference: floor((a + 8192) / 16384).
            const std::int64_t dx = (length * cosine_at(heading) + 8192) >> 14U;
            const std::int64_t dy = (length * sine_at(heading) + 8192) >> 14U;
            px += dx;
            py += dy;
            if (px > coordinate_limit || px < -coordinate_limit || py > coordinate_limit || py < -coordinate_limit) {
                return Status::bad_body;
            }
            out.x[n] = static_cast<std::int32_t>(px);
            out.y[n] = static_cast<std::int32_t>(py);
            ++n;
            previous_class = length_class;
        }
        if (decoder.failed()) return Status::bad_body;
    }
    if (decoder.failed() || n != layer.vertices) return Status::bad_body;
    out.starts[layer.lines] = n;
    // Ties, in stored order: a line end takes a place on this layer or on a
    // layer it depends on.
    const std::uint32_t line_bits = bits_for(layer.lines);
    for (std::uint32_t t = 0U; t < layer.ties; ++t) {
        const std::uint32_t tie_line = decoder.raw(line_bits);
        const std::uint32_t tie_end = decoder.raw(1U);
        const std::uint32_t target = decoder.raw(4U);
        if (decoder.failed() || tie_line >= layer.lines || target > input.layer) return Status::bad_tie;
        if (target < input.layer && ((static_cast<std::uint32_t>(layer.depends) >> target) & 1U) == 0U) {
            return Status::bad_tie;
        }
        const LayerInfo& target_info = info.layers[target];
        const std::uint32_t vertex = decoder.raw(bits_for(target_info.vertices));
        std::uint32_t fraction = 0U;
        if (decoder.raw(1U) != 0U) {
            fraction = decoder.raw(8U);
            if (fraction == 0U) return Status::bad_tie;
        }
        // A plain tie names a vertex; a fractional one also needs vertex + 1.
        const std::uint64_t needed = static_cast<std::uint64_t>(vertex) + (fraction == 0U ? 1U : 2U);
        if (decoder.failed() || needed > target_info.vertices) return Status::bad_tie;
        const LayerStorage& place = storage[target];
        std::int64_t tx = place.x[vertex];
        std::int64_t ty = place.y[vertex];
        if (fraction != 0U) {
            const std::int64_t along_x = static_cast<std::int64_t>(place.x[vertex + 1U]) - tx;
            const std::int64_t along_y = static_cast<std::int64_t>(place.y[vertex + 1U]) - ty;
            tx += (along_x * static_cast<std::int64_t>(fraction) + 128) >> 8U;
            ty += (along_y * static_cast<std::int64_t>(fraction) + 128) >> 8U;
        }
        const std::uint32_t at = tie_end == 0U ? out.starts[tie_line] : out.starts[tie_line + 1U] - 1U;
        out.x[at] = static_cast<std::int32_t>(tx);
        out.y[at] = static_cast<std::int32_t>(ty);
    }
    return Status::ok;
}

bool same_info(const MapInfo& a, const MapInfo& b) noexcept {
    if (a.layer_count != b.layer_count || a.tolerance_units != b.tolerance_units) return false;
    for (std::size_t i = 0U; i < a.layer_count; ++i) {
        const LayerInfo& x = a.layers[i];
        const LayerInfo& y = b.layers[i];
        const bool same = x.lines == y.lines && x.vertices == y.vertices && x.ties == y.ties &&
            x.body_offset == y.body_offset && x.body_bytes == y.body_bytes && x.depends == y.depends;
        if (!same) return false;
    }
    return true;
}

} // namespace

Status inspect_map(std::span<const std::byte> file, const DecodeLimits& limits, MapInfo& info) noexcept {
    const std::size_t length = file.size();
    if (length < fixed_header_bytes + check_bytes) return Status::not_tmap;
    const bool magic = byte_at(file, 0U) == 0x54U && byte_at(file, 1U) == 0x4DU && byte_at(file, 2U) == 0x41U &&
        byte_at(file, 3U) == 0x50U && byte_at(file, 4U) == 2U;
    if (!magic) return Status::not_tmap;
    if (length > 0xFFFFFFFFU) return Status::bad_counts;
    const std::size_t end = length - check_bytes;
    if (crc32(file.first(end)) != u32_at(file, end)) return Status::bad_check_value;
    info = MapInfo{};
    info.mantissa_bits = static_cast<std::uint8_t>(byte_at(file, 5U));
    info.length_unit = static_cast<std::uint16_t>(u16_at(file, 6U));
    if (info.mantissa_bits > 4U) return Status::bad_header;
    std::size_t at = 8U;
    for (std::size_t e = 0U; e < 16U; ++e) {
        const std::uint32_t bins = byte_at(file, at);
        ++at;
        if (bins > 12U) return Status::bad_header;
        info.turn_bins_log2[e] = static_cast<std::uint8_t>(bins);
    }
    info.units_per_degree = u32_at(file, at);
    info.tolerance_units = u32_at(file, at + 4U);
    at += 8U;
    const std::uint32_t count = byte_at(file, at);
    ++at;
    if (count < 1U || count > max_layers) return Status::bad_header;
    info.layer_count = static_cast<std::uint8_t>(count);
    std::uint64_t total_vertices = 0U;
    for (std::uint32_t i = 0U; i < count; ++i) {
        if (end - at < 3U) return Status::bad_header;
        LayerInfo& layer = info.layers[i];
        const std::uint32_t kind = byte_at(file, at);
        const std::uint32_t flags = byte_at(file, at + 1U);
        const std::uint32_t name_length = byte_at(file, at + 2U);
        at += 3U;
        if (kind > 1U || flags > 1U || name_length < 1U || name_length > max_layer_name) return Status::bad_header;
        if (end - at < name_length + layer_record_bytes - 3U) return Status::bad_header;
        for (std::uint32_t c = 0U; c < name_length; ++c) {
            const std::uint32_t character = byte_at(file, at + c);
            if (!name_character(character)) return Status::bad_header;
            layer.name[c] = static_cast<char>(character);
        }
        at += name_length;
        layer.name_length = static_cast<std::uint8_t>(name_length);
        layer.kind = kind == 0U ? LayerKind::rings : LayerKind::lines;
        layer.optional = flags == 1U;
        layer.start_grid = static_cast<std::uint16_t>(u16_at(file, at));
        layer.depends = static_cast<std::uint16_t>(u16_at(file, at + 2U));
        layer.lines = u32_at(file, at + 4U);
        layer.vertices = u32_at(file, at + 8U);
        layer.ties = u32_at(file, at + 12U);
        layer.body_bytes = u32_at(file, at + 16U);
        at += 20U;
        if ((static_cast<std::uint32_t>(layer.depends) >> i) != 0U) return Status::bad_header;
        if (layer.vertices < layer.lines || layer.vertices > limits.vertices) return Status::bad_counts;
        if (static_cast<std::uint64_t>(layer.ties) > static_cast<std::uint64_t>(layer.lines) * 2U) return Status::bad_counts;
        total_vertices += layer.vertices;
        if (total_vertices > limits.vertices) return Status::bad_counts;
    }
    for (std::uint32_t i = 0U; i < count; ++i) {
        LayerInfo& layer = info.layers[i];
        if (layer.body_bytes > end - at) return Status::bad_header;
        layer.body_offset = static_cast<std::uint32_t>(at);
        at += layer.body_bytes;
    }
    if (at != end) return Status::bad_header;
    return Status::ok;
}

LayerMask default_layers(const MapInfo& info) noexcept {
    LayerMask mask = 0U;
    for (std::size_t i = 0U; i < info.layer_count; ++i) {
        if (!info.layers[i].optional) mask = static_cast<LayerMask>(mask | (1U << i));
    }
    return mask;
}

std::string_view layer_name(const LayerInfo& layer) noexcept {
    const std::string_view name(layer.name.data(), layer.name_length);
    return name;
}

LayerMask layer_named(const MapInfo& info, std::string_view name) noexcept {
    for (std::size_t i = 0U; i < info.layer_count; ++i) {
        if (layer_name(info.layers[i]) == name) return static_cast<LayerMask>(1U << i);
    }
    return 0U;
}

LayerMask with_dependencies(const MapInfo& info, LayerMask mask) noexcept {
    std::uint32_t closed = mask;
    // Dependencies point only to earlier layers: one pass from the last.
    for (std::size_t i = info.layer_count; i > 0U; --i) {
        const std::size_t layer = i - 1U;
        if (((closed >> layer) & 1U) != 0U) closed |= info.layers[layer].depends;
    }
    const LayerMask result = static_cast<LayerMask>(closed);
    return result;
}

Status decode_map(std::span<const std::byte> file, const MapInfo& info, const DecodeLimits& limits, LayerMask mask,
                  std::span<const LayerStorage> storage) noexcept {
    MapInfo checked;
    const Status inspected = inspect_map(file, limits, checked);
    if (inspected != Status::ok) return inspected;
    if (!same_info(checked, info) || storage.size() != checked.layer_count) return Status::invalid_argument;
    const std::uint32_t known = (1U << checked.layer_count) - 1U;
    if ((static_cast<std::uint32_t>(mask) & ~known) != 0U || with_dependencies(checked, mask) != mask) {
        return Status::invalid_argument;
    }
    for (std::uint32_t i = 0U; i < checked.layer_count; ++i) {
        if (((static_cast<std::uint32_t>(mask) >> i) & 1U) == 0U) continue;
        const LayerInfo& layer = checked.layers[i];
        const LayerStorage& out = storage[i];
        const bool fits = out.x.size() >= layer.vertices && out.y.size() >= layer.vertices &&
            out.starts.size() >= static_cast<std::size_t>(layer.lines) + 1U;
        if (!fits) return Status::storage_too_small;
    }
    for (std::uint32_t i = 0U; i < checked.layer_count; ++i) {
        if (((static_cast<std::uint32_t>(mask) >> i) & 1U) == 0U) continue;
        const LayerInfo& layer = checked.layers[i];
        LayerInput input;
        input.info = &checked;
        input.layer = i;
        input.body = file.subspan(layer.body_offset, layer.body_bytes);
        const Status decoded = decode_layer(input, storage);
        if (decoded != Status::ok) return decoded;
    }
    return Status::ok;
}

LayerView view_of(const LayerInfo& info, const LayerStorage& storage) noexcept {
    LayerView view;
    view.kind = info.kind;
    if (storage.x.size() < info.vertices || storage.y.size() < info.vertices ||
        storage.starts.size() < static_cast<std::size_t>(info.lines) + 1U) {
        return view;
    }
    view.x = std::span<const std::int32_t>(storage.x.data(), info.vertices);
    view.y = std::span<const std::int32_t>(storage.y.data(), info.vertices);
    view.starts = std::span<const std::uint32_t>(storage.starts.data(), static_cast<std::size_t>(info.lines) + 1U);
    return view;
}

} // namespace stx_map
