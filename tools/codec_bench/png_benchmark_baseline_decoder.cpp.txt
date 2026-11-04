#include "png_decoder.hpp"
#include "png_writer.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace gui_forms::image {
namespace {
using compression::InflateError;
using compression::InflateOptions;
using compression::InflateResult;
using compression::InflateStatus;
using compression::InflateStream;

constexpr std::uint32_t ihdr = 0x49484452U;
constexpr std::uint32_t plte = 0x504c5445U;
constexpr std::uint32_t idat = 0x49444154U;
constexpr std::uint32_t iend = 0x49454e44U;
constexpr std::uint32_t trns = 0x74524e53U;
constexpr std::uint32_t iccp = 0x69434350U;
constexpr std::uint32_t ztxt = 0x7a545874U;
constexpr std::uint32_t itxt = 0x69545874U;
constexpr std::uint32_t text = 0x74455874U;
constexpr std::uint32_t srgb = 0x73524742U;
constexpr std::uint32_t gama = 0x67414d41U;
constexpr std::uint32_t chrm = 0x6348524dU;

unsigned byte(std::byte value) noexcept {
    return std::to_integer<unsigned>(value);
}
std::uint32_t read32(std::span<const std::byte> data, std::size_t offset) noexcept {
    const std::uint32_t value = (byte(data[offset]) << 24U) |
        (byte(data[offset + 1]) << 16U) | (byte(data[offset + 2]) << 8U) |
        byte(data[offset + 3]);
    return value;
}
unsigned read16(std::span<const std::byte> data, std::size_t offset) noexcept {
    const unsigned value = (byte(data[offset]) << 8U) | byte(data[offset + 1]);
    return value;
}

struct Pass final {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t dx = 1;
    std::uint32_t dy = 1;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t row_bytes = 0;
};
struct Plan final {
    PngSampleLayout layout{};
    std::array<Pass, 7> passes{};
    unsigned pass_count = 0;
    unsigned channels = 0;
    unsigned filter_stride = 0;
    // Palette shape: at most 256 entries, four expanded RGBA samples each.
    std::array<std::array<std::uint16_t, 4>, 256> palette{};
    unsigned palette_count = 0;
    std::array<unsigned, 3> transparency{};
    bool has_transparency = false;
    std::size_t idat_begin = 0;
    std::size_t idat_end = 0;
    std::uint64_t idat_bytes = 0;
};
struct Chunk final {
    std::uint32_t type = 0;
    std::size_t data_offset = 0;
    std::size_t length = 0;
    std::size_t next = 0;
};

// Caller proves the whole chunk envelope before this descriptor is constructed.
Chunk chunk_at(std::span<const std::byte> encoded, std::size_t offset) noexcept {
    Chunk chunk{};
    chunk.type = read32(encoded, offset + 4);
    chunk.length = read32(encoded, offset);
    chunk.data_offset = offset + 8;
    chunk.next = offset + chunk.length + 12;
    return chunk;
}

bool valid_limits(const PngDecodeLimits& limits) noexcept {
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    const bool valid = limits.encoded_bytes < maximum && limits.pixels < maximum &&
        limits.filtered_bytes < maximum && limits.metadata_bytes < maximum &&
        limits.inflate_work.value < maximum && limits.dimension <= 0x7fffffffU;
    return valid;
}

PngDecodeError make_layout(std::span<const std::byte> header,
                          const PngDecodeLimits& limits, Plan& plan) noexcept {
    PngSampleLayout& layout = plan.layout;
    layout.width = read32(header, 0);
    layout.height = read32(header, 4);
    layout.depth = byte(header[8]);
    layout.color = byte(header[9]);
    layout.interlaced = byte(header[12]) != 0;
    const unsigned depth = layout.depth;
    switch (layout.color) {
        case 0: plan.channels = 1; break;
        case 2: plan.channels = 3; break;
        case 3: plan.channels = 1; break;
        case 4: plan.channels = 2; break;
        case 6: plan.channels = 4; break;
        default: return PngDecodeError::invalid_header;
    }
    const bool packed = depth == 1 || depth == 2 || depth == 4;
    const bool valid_depth = layout.color == 3 ? (packed || depth == 8) :
        (depth == 8 || depth == 16 || (layout.color == 0 && packed));
    if (!valid_depth || layout.width == 0 || layout.height == 0 ||
        layout.width > 0x7fffffffU || layout.height > 0x7fffffffU ||
        byte(header[10]) != 0 || byte(header[11]) != 0 || byte(header[12]) > 1) {
        return PngDecodeError::invalid_header;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(layout.width) * layout.height;
    const std::uint64_t size_max = std::numeric_limits<std::size_t>::max();
    if (layout.width > limits.dimension || layout.height > limits.dimension ||
        pixels > limits.pixels || pixels > size_max / (4 * sizeof(std::uint16_t))) {
        return PngDecodeError::resource_limit;
    }
    layout.rgba_samples = static_cast<std::size_t>(pixels * 4);
    plan.filter_stride = std::max(1U, plan.channels * depth / 8);
    constexpr std::array<std::uint32_t, 7> starts_x{0, 4, 0, 2, 0, 1, 0};
    constexpr std::array<std::uint32_t, 7> starts_y{0, 0, 4, 0, 2, 0, 1};
    constexpr std::array<std::uint32_t, 7> steps_x{8, 8, 4, 4, 2, 2, 1};
    constexpr std::array<std::uint32_t, 7> steps_y{8, 8, 8, 4, 4, 2, 2};
    const unsigned count = layout.interlaced ? 7U : 1U;
    for (unsigned index = 0; index < count; ++index) {
        Pass pass{};
        if (layout.interlaced) {
            pass.x = starts_x[index];
            pass.y = starts_y[index];
            pass.dx = steps_x[index];
            pass.dy = steps_y[index];
        }
        if (layout.width <= pass.x || layout.height <= pass.y) continue;
        pass.width = 1 + (layout.width - pass.x - 1) / pass.dx;
        pass.height = 1 + (layout.height - pass.y - 1) / pass.dy;
        const std::uint64_t bits = static_cast<std::uint64_t>(pass.width) * plan.channels * depth;
        const std::uint64_t row = (bits + 7) / 8;
        if (row + 1 > size_max / 2 || row + 1 > limits.filtered_bytes / pass.height) {
            return PngDecodeError::resource_limit;
        }
        const std::uint64_t pass_bytes = (row + 1) * pass.height;
        if (pass_bytes > limits.filtered_bytes - layout.filtered_bytes) {
            return PngDecodeError::resource_limit;
        }
        layout.filtered_bytes += pass_bytes;
        pass.row_bytes = static_cast<std::size_t>(row);
        layout.row_storage_bytes = std::max(layout.row_storage_bytes, 2 * (pass.row_bytes + 1));
        plan.passes[plan.pass_count] = pass;
        ++plan.pass_count;
    }
    return PngDecodeError::none;
}

// Checks PNG keywords and returns the byte after their terminating zero.
bool keyword_end(std::span<const std::byte> data, std::size_t& end) noexcept {
    bool space = true;
    for (std::size_t index = 0; index < data.size() && index <= 79; ++index) {
        const unsigned value = byte(data[index]);
        if (value == 0) {
            if (index == 0 || space) return false;
            end = index + 1;
            return true;
        }
        if (value < 32 || (value > 126 && value < 161)) return false;
        if (value == 32 && space) return false;
        space = value == 32;
    }
    return false;
}

PngDecodeError metadata_field(std::uint32_t type, std::span<const std::byte> data,
                             bool& compressed, std::size_t& start) noexcept {
    compressed = false;
    start = 0;
    if (type != iccp && type != ztxt && type != itxt && type != text) {
        return PngDecodeError::none;
    }
    if (!keyword_end(data, start)) return PngDecodeError::invalid_metadata;
    if (type == text) return PngDecodeError::none;
    if (type == iccp || type == ztxt) {
        if (start == data.size() || data[start] != std::byte{0}) {
            return PngDecodeError::invalid_metadata;
        }
        ++start;
        compressed = true;
        return PngDecodeError::none;
    }
    if (data.size() - start < 2 || byte(data[start]) > 1 || data[start + 1] != std::byte{0}) {
        return PngDecodeError::invalid_metadata;
    }
    compressed = data[start] == std::byte{1};
    start += 2;
    // Language tag and translated keyword remain in the preserved raw chunk.
    for (unsigned field = 0; field < 2; ++field) {
        while (start < data.size() && data[start] != std::byte{0}) ++start;
        if (start == data.size()) return PngDecodeError::invalid_metadata;
        ++start;
    }
    return PngDecodeError::none;
}

PngDecodeError parse(std::span<const std::byte> encoded,
                     const PngDecodeLimits& limits, Plan& plan) noexcept {
    if (!valid_limits(limits)) return PngDecodeError::invalid_argument;
    if (encoded.size() > limits.encoded_bytes) return PngDecodeError::resource_limit;
    constexpr std::array<unsigned, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (encoded.size() < signature.size()) return PngDecodeError::invalid_signature;
    for (std::size_t index = 0; index < signature.size(); ++index) {
        if (byte(encoded[index]) != signature[index]) return PngDecodeError::invalid_signature;
    }
    bool saw_header = false;
    bool saw_palette = false;
    bool saw_idat = false;
    bool ended_idat = false;
    unsigned color_flags = 0;
    std::uint64_t raw_metadata = 0;
    std::size_t offset = 8;
    std::uint32_t count = 0;
    while (offset < encoded.size()) {
        if (count == limits.chunks) return PngDecodeError::resource_limit;
        ++count;
        if (encoded.size() - offset < 12) return PngDecodeError::truncated_chunk;
        const std::uint32_t length = read32(encoded, offset);
        if (length > 0x7fffffffU || length > encoded.size() - offset - 12) {
            return PngDecodeError::truncated_chunk;
        }
        const Chunk chunk = chunk_at(encoded, offset);
        for (std::size_t index = offset + 4; index < offset + 8; ++index) {
            const unsigned value = byte(encoded[index]);
            if (!((value >= 65 && value <= 90) || (value >= 97 && value <= 122))) {
                return PngDecodeError::invalid_chunk_type;
            }
        }
        if ((byte(encoded[offset + 6]) & 32U) != 0) return PngDecodeError::invalid_chunk_type;
        const std::span<const std::byte> checked = encoded.subspan(offset + 4, length + 4ULL);
        if (png_crc32(checked) != read32(encoded, chunk.next - 4)) return PngDecodeError::crc_mismatch;
        const std::span<const std::byte> data = encoded.subspan(chunk.data_offset, chunk.length);
        if (!saw_header && chunk.type != ihdr) return PngDecodeError::invalid_order;
        if (saw_idat && chunk.type != idat) ended_idat = true;
        if ((chunk.type & 0x20000000U) != 0) {
            if (chunk.length > limits.metadata_bytes - raw_metadata) return PngDecodeError::resource_limit;
            raw_metadata += chunk.length;
            ++plan.layout.metadata_records;
            bool compressed = false;
            std::size_t start = 0;
            const PngDecodeError error = metadata_field(chunk.type, data, compressed, start);
            if (error != PngDecodeError::none) return error;
        }
        switch (chunk.type) {
            case ihdr: {
                if (saw_header || length != 13) return PngDecodeError::invalid_header;
                const PngDecodeError error = make_layout(data, limits, plan);
                if (error != PngDecodeError::none) return error;
                saw_header = true;
                break;
            }
            case plte: {
                if (saw_palette || saw_idat || plan.has_transparency || length == 0 ||
                    length > 768 || length % 3 != 0 || plan.layout.color == 0 || plan.layout.color == 4) {
                    return PngDecodeError::invalid_palette;
                }
                plan.palette_count = length / 3;
                if (plan.layout.color == 3 && plan.palette_count > (1U << plan.layout.depth)) {
                    return PngDecodeError::invalid_palette;
                }
                for (unsigned entry = 0; entry < plan.palette_count; ++entry) {
                    for (unsigned channel = 0; channel < 3; ++channel) {
                        plan.palette[entry][channel] = static_cast<std::uint16_t>(byte(data[entry * 3 + channel]) * 257U);
                    }
                    plan.palette[entry][3] = 65535;
                }
                saw_palette = true;
                break;
            }
            case trns: {
                if (plan.has_transparency || saw_idat) return PngDecodeError::invalid_transparency;
                const unsigned color = plan.layout.color;
                if (color == 3) {
                    if (!saw_palette || length == 0 || length > plan.palette_count) return PngDecodeError::invalid_transparency;
                    for (unsigned entry = 0; entry < length; ++entry) {
                        plan.palette[entry][3] = static_cast<std::uint16_t>(byte(data[entry]) * 257U);
                    }
                } else {
                    const unsigned values = color == 0 ? 1U : 3U;
                    if ((color != 0 && color != 2) || length != values * 2) return PngDecodeError::invalid_transparency;
                    const unsigned maximum = (1U << plan.layout.depth) - 1;
                    for (unsigned channel = 0; channel < values; ++channel) {
                        plan.transparency[channel] = read16(data, channel * 2);
                        if (plan.transparency[channel] > maximum) return PngDecodeError::invalid_transparency;
                    }
                }
                plan.has_transparency = true;
                break;
            }
            case idat:
                if (ended_idat) return PngDecodeError::invalid_order;
                if (plan.layout.color == 3 && !saw_palette) return PngDecodeError::invalid_palette;
                if (!saw_idat) plan.idat_begin = offset;
                plan.idat_end = chunk.next;
                plan.idat_bytes += length;
                saw_idat = true;
                break;
            case iend:
                if (length != 0) return PngDecodeError::invalid_order;
                if (!saw_idat || plan.idat_bytes == 0) return PngDecodeError::missing_image;
                if (chunk.next != encoded.size()) return PngDecodeError::trailing_data;
                return PngDecodeError::none;
            case srgb: case gama: case chrm: case iccp: {
                unsigned flag = 1;
                if (chunk.type == gama) flag = 2;
                if (chunk.type == chrm) flag = 4;
                if (chunk.type == iccp) flag = 8;
                if (saw_palette || saw_idat || (color_flags & flag) != 0) return PngDecodeError::invalid_metadata;
                if (chunk.type == srgb && (length != 1 || byte(data[0]) > 3 || (color_flags & 8U) != 0)) return PngDecodeError::invalid_metadata;
                if (chunk.type == gama && (length != 4 || read32(data, 0) == 0)) return PngDecodeError::invalid_metadata;
                if (chunk.type == chrm && length != 32) return PngDecodeError::invalid_metadata;
                if (chunk.type == iccp && (color_flags & 1U) != 0) return PngDecodeError::invalid_metadata;
                color_flags |= flag;
                break;
            }
            default:
                if ((chunk.type & 0x20000000U) == 0) return PngDecodeError::unknown_critical;
                break;
        }
        offset = chunk.next;
    }
    return PngDecodeError::missing_image;
}

// Live byte ranges only: unaligned addresses and exact tails are supported.
void unfilter_up(std::span<std::byte> row, std::span<const std::byte> previous) noexcept {
    for (std::size_t index = 0; index < row.size(); ++index) {
        row[index] = static_cast<std::byte>((byte(row[index]) + byte(previous[index])) & 255U);
    }
}
unsigned paeth(unsigned left, unsigned above, unsigned corner) noexcept {
    const int p = static_cast<int>(left) + static_cast<int>(above) - static_cast<int>(corner);
    const int a = p - static_cast<int>(left);
    const int b = p - static_cast<int>(above);
    const int c = p - static_cast<int>(corner);
    const int da = a < 0 ? -a : a;
    const int db = b < 0 ? -b : b;
    const int dc = c < 0 ? -c : c;
    if (da <= db && da <= dc) return left;
    if (db <= dc) return above;
    return corner;
}
template<unsigned Filter>
void unfilter_left(std::span<std::byte> row, std::span<const std::byte> previous,
                   unsigned stride) noexcept {
    const std::size_t prefix = std::min<std::size_t>(stride, row.size());
    for (std::size_t index = 0; index < prefix; ++index) {
        unsigned predictor = 0;
        if constexpr (Filter == 3) predictor = byte(previous[index]) / 2;
        if constexpr (Filter == 4) predictor = byte(previous[index]);
        row[index] = static_cast<std::byte>((byte(row[index]) + predictor) & 255U);
    }
    for (std::size_t index = prefix; index < row.size(); ++index) {
        const unsigned left = byte(row[index - stride]);
        unsigned predictor = left;
        if constexpr (Filter == 3) predictor = (left + byte(previous[index])) / 2;
        if constexpr (Filter == 4) predictor = paeth(left, byte(previous[index]), byte(previous[index - stride]));
        row[index] = static_cast<std::byte>((byte(row[index]) + predictor) & 255U);
    }
}
PngDecodeError unfilter(unsigned filter, std::span<std::byte> row,
                        std::span<const std::byte> previous, unsigned stride) noexcept {
    switch (filter) {
        case 0: break;
        case 1: unfilter_left<1>(row, previous, stride); break;
        case 2: unfilter_up(row, previous); break;
        case 3: unfilter_left<3>(row, previous, stride); break;
        case 4: unfilter_left<4>(row, previous, stride); break;
        default: return PngDecodeError::invalid_filter;
    }
    return PngDecodeError::none;
}

template<unsigned Depth>
unsigned sample(std::span<const std::byte> row, std::size_t index) noexcept {
    if constexpr (Depth == 16) {
        const unsigned value = read16(row, index * 2);
        return value;
    } else if constexpr (Depth == 8) {
        const unsigned value = byte(row[index]);
        return value;
    } else {
        const std::size_t bit = index * Depth;
        const unsigned shift = 8 - Depth - static_cast<unsigned>(bit % 8);
        const unsigned value = (byte(row[bit / 8]) >> shift) & ((1U << Depth) - 1);
        return value;
    }
}

template<unsigned Color, unsigned Depth, bool Transparent>
PngDecodeError scatter(const Plan& plan, const Pass& pass, std::uint32_t row_index,
                       std::span<const std::byte> row, std::span<std::uint16_t> rgba) noexcept {
    constexpr unsigned channels = Color == 2 ? 3 : (Color == 4 ? 2 : (Color == 6 ? 4 : 1));
    constexpr unsigned scale = 65535 / ((1U << Depth) - 1);
    const std::size_t y = pass.y + row_index * pass.dy;
    std::size_t destination = (y * plan.layout.width + pass.x) * 4;
    const std::size_t destination_step = pass.dx * 4;
    const std::array<unsigned, 3> transparency = plan.transparency;
    for (std::size_t x = 0; x < pass.width; ++x) {
        const std::size_t source = x * channels;
        const unsigned first = sample<Depth>(row, source);
        if constexpr (Color == 3) {
            if (first >= plan.palette_count) return PngDecodeError::invalid_palette_index;
            for (unsigned channel = 0; channel < 4; ++channel) rgba[destination + channel] = plan.palette[first][channel];
        } else {
            unsigned red = first;
            unsigned green = first;
            unsigned blue = first;
            unsigned alpha = 65535;
            if constexpr (Color == 2 || Color == 6) {
                green = sample<Depth>(row, source + 1);
                blue = sample<Depth>(row, source + 2);
            }
            if constexpr (Color == 4 || Color == 6) {
                alpha = sample<Depth>(row, source + channels - 1) * scale;
            } else if constexpr (Color == 0 && Transparent) {
                if (first == transparency[0]) alpha = 0;
            } else if constexpr (Color == 2 && Transparent) {
                if (red == transparency[0] && green == transparency[1] &&
                    blue == transparency[2]) alpha = 0;
            }
            rgba[destination] = static_cast<std::uint16_t>(red * scale);
            rgba[destination + 1] = static_cast<std::uint16_t>(green * scale);
            rgba[destination + 2] = static_cast<std::uint16_t>(blue * scale);
            rgba[destination + 3] = static_cast<std::uint16_t>(alpha);
        }
        destination += destination_step;
    }
    return PngDecodeError::none;
}
using Scatter = PngDecodeError (*)(const Plan&, const Pass&, std::uint32_t,
                                  std::span<const std::byte>, std::span<std::uint16_t>) noexcept;
template<unsigned Color, bool Transparent = false>
Scatter select_depth(unsigned depth) noexcept {
    if (depth == 8) return &scatter<Color, 8, Transparent>;
    if constexpr (Color != 3) {
        if (depth == 16) return &scatter<Color, 16, Transparent>;
    }
    if constexpr (Color == 0 || Color == 3) {
        if (depth == 1) return &scatter<Color, 1, Transparent>;
        if (depth == 2) return &scatter<Color, 2, Transparent>;
        if (depth == 4) return &scatter<Color, 4, Transparent>;
    }
    return nullptr;
}
Scatter select_scatter(const Plan& plan) noexcept {
    switch (plan.layout.color) {
        case 0: {
            if (plan.has_transparency) {
                const Scatter transparent = select_depth<0, true>(plan.layout.depth);
                return transparent;
            }
            const Scatter selected = select_depth<0>(plan.layout.depth);
            return selected;
        }
        case 2: {
            if (plan.has_transparency) {
                const Scatter transparent = select_depth<2, true>(plan.layout.depth);
                return transparent;
            }
            const Scatter selected = select_depth<2>(plan.layout.depth);
            return selected;
        }
        case 3: {
            const Scatter selected = select_depth<3>(plan.layout.depth);
            return selected;
        }
        case 4: {
            const Scatter selected = select_depth<4>(plan.layout.depth);
            return selected;
        }
        case 6: {
            const Scatter selected = select_depth<6>(plan.layout.depth);
            return selected;
        }
        default: return nullptr;
    }
}

struct IdatReader final {
    std::size_t chunk_offset = 0;
    std::size_t consumed = 0;
    bool ended = false;
};

PngDecodeError read_image_bytes(std::span<const std::byte> encoded, const Plan& plan,
    InflateStream& workspace, IdatReader& reader, std::span<std::byte> output,
    bool drain, PngDecodeResult& result) noexcept {
    std::size_t written = 0;
    while (!reader.ended && (drain || written < output.size())) {
        // Empty IDAT chunks are legal, including after the trailer.
        while (reader.chunk_offset < plan.idat_end) {
            const Chunk chunk = chunk_at(encoded, reader.chunk_offset);
            if (reader.consumed < chunk.length) break;
            reader.chunk_offset = chunk.next;
            reader.consumed = 0;
        }
        std::span<const std::byte> input{};
        bool final = true;
        if (reader.chunk_offset < plan.idat_end) {
            const Chunk chunk = chunk_at(encoded, reader.chunk_offset);
            input = encoded.subspan(chunk.data_offset + reader.consumed, chunk.length - reader.consumed);
            final = chunk.next == plan.idat_end;
        }
        const InflateResult step = workspace.process(input, output.subspan(written), final);
        reader.consumed += step.input_consumed;
        written += step.output_produced;
        result.inflate_work = step.total_work;
        result.inflate_error = step.error;
        if (step.status == InflateStatus::error) return PngDecodeError::inflate_failure;
        if (step.status == InflateStatus::stream_end) {
            reader.ended = true;
            if (step.total_input.value != plan.idat_bytes) return PngDecodeError::trailing_data;
            if (step.total_output.value != plan.layout.filtered_bytes) return PngDecodeError::invalid_filtered_size;
            break;
        }
        if (drain && step.output_produced != 0) return PngDecodeError::invalid_filtered_size;
        if (step.input_consumed == 0 && step.output_produced == 0 && step.status != InflateStatus::need_input) {
            return PngDecodeError::invalid_filtered_size;
        }
    }
    if (!drain && written != output.size()) return PngDecodeError::invalid_filtered_size;
    return PngDecodeError::none;
}

bool overlaps(std::span<const std::byte> left, std::span<const std::byte> right) noexcept {
    if (left.empty() || right.empty()) return false;
    const std::uintptr_t a = reinterpret_cast<std::uintptr_t>(left.data());
    const std::uintptr_t b = reinterpret_cast<std::uintptr_t>(right.data());
    const bool overlap = a <= b ? b - a < left.size() : a - b < right.size();
    return overlap;
}

PngDecodeError decode_metadata(std::span<const std::byte> encoded, const PngDecodeLimits& limits,
    InflateStream& workspace, std::span<std::byte> output,
    std::span<PngMetadataRecord> records, PngDecodeResult& result) noexcept {
    std::size_t offset = 8;
    std::size_t record_index = 0;
    while (offset < encoded.size()) {
        const Chunk chunk = chunk_at(encoded, offset);
        offset = chunk.next;
        if ((chunk.type & 0x20000000U) == 0) continue;
        PngMetadataRecord record{};
        record.type = chunk.type;
        record.encoded_offset = chunk.data_offset;
        record.encoded_length = chunk.length;
        const std::span<const std::byte> data = encoded.subspan(chunk.data_offset, chunk.length);
        std::size_t start = 0;
        const PngDecodeError field_error = metadata_field(chunk.type, data, record.compressed, start);
        if (field_error != PngDecodeError::none) return field_error;
        if (record.compressed) {
            InflateOptions options{};
            options.limits.input_bytes.value = chunk.length - start;
            options.limits.output_bytes.value = limits.metadata_bytes - result.metadata_bytes;
            options.limits.work.value = limits.inflate_work.value - result.inflate_work.value;
            result.inflate_error = workspace.reset(options);
            if (result.inflate_error != InflateError::none) return PngDecodeError::inflate_failure;
            const std::span<const std::byte> input = data.subspan(start);
            const InflateResult step = workspace.process(input, output.subspan(result.metadata_bytes), true);
            result.inflate_error = step.error;
            result.inflate_work.value += step.total_work.value;
            if (step.status == InflateStatus::error) return PngDecodeError::inflate_failure;
            if (step.status != InflateStatus::stream_end) return PngDecodeError::insufficient_storage;
            if (step.input_consumed != input.size()) return PngDecodeError::trailing_data;
            record.decoded_offset = result.metadata_bytes;
            record.decoded_length = step.output_produced;
            result.metadata_bytes += step.output_produced;
        }
        records[record_index] = record;
        ++record_index;
    }
    return PngDecodeError::none;
}
} // namespace

PngDecodeResult inspect_png_samples(std::span<const std::byte> encoded,
                                   PngDecodeLimits limits) noexcept {
    Plan plan{};
    PngDecodeResult result{};
    result.error = parse(encoded, limits, plan);
    if (result.error == PngDecodeError::none) result.layout = plan.layout;
    return result;
}

PngDecodeResult decode_png_samples(std::span<const std::byte> encoded,
    PngDecodeLimits limits, InflateStream& workspace, std::span<std::byte> rows,
    std::span<std::uint16_t> rgba, std::span<std::byte> metadata,
    std::span<PngMetadataRecord> records) noexcept {
    PngDecodeResult result{};
    const std::span<const InflateStream> workspace_objects(&workspace, 1);
    const std::span<const std::byte> workspace_bytes = std::as_bytes(workspace_objects);
    const std::span<const std::byte> sample_bytes = std::as_bytes(rgba);
    const std::span<const std::byte> record_bytes = std::as_bytes(records);
    const std::array<std::span<const std::byte>, 6> buffers{
        encoded, workspace_bytes, rows, sample_bytes, metadata, record_bytes};
    for (std::size_t first = 0; first < buffers.size(); ++first) {
        for (std::size_t second = first + 1; second < buffers.size(); ++second) {
            if (overlaps(buffers[first], buffers[second])) {
                result.error = PngDecodeError::invalid_argument;
                return result;
            }
        }
    }
    Plan plan{};
    result.error = parse(encoded, limits, plan);
    if (result.error != PngDecodeError::none) return result;
    result.layout = plan.layout;
    if (rows.size() < plan.layout.row_storage_bytes || rgba.size() < plan.layout.rgba_samples ||
        records.size() < plan.layout.metadata_records) {
        result.error = PngDecodeError::insufficient_storage;
        return result;
    }
    InflateOptions options{};
    options.limits.input_bytes.value = plan.idat_bytes;
    options.limits.output_bytes.value = plan.layout.filtered_bytes;
    options.limits.work = limits.inflate_work;
    result.inflate_error = workspace.reset(options);
    if (result.inflate_error != InflateError::none) {
        result.error = PngDecodeError::inflate_failure;
        return result;
    }
    IdatReader reader{.chunk_offset = plan.idat_begin};
    const Scatter scatter_row = select_scatter(plan);
    for (unsigned index = 0; index < plan.pass_count; ++index) {
        const Pass& pass = plan.passes[index];
        std::span<std::byte> current = rows.first(pass.row_bytes + 1);
        std::span<std::byte> previous = rows.subspan(pass.row_bytes + 1, pass.row_bytes + 1);
        std::fill(previous.begin(), previous.end(), std::byte{0});
        for (std::uint32_t y = 0; y < pass.height; ++y) {
            result.error = read_image_bytes(encoded, plan, workspace, reader, current, false, result);
            if (result.error != PngDecodeError::none) return result;
            result.error = unfilter(byte(current[0]), current.subspan(1), previous.subspan(1), plan.filter_stride);
            if (result.error != PngDecodeError::none) return result;
            result.error = scatter_row(plan, pass, y, current.subspan(1), rgba);
            if (result.error != PngDecodeError::none) return result;
            std::swap(current, previous);
        }
    }
    // An empty output span permits trailer consumption but no extra image byte.
    result.error = read_image_bytes(encoded, plan, workspace, reader, {}, true, result);
    if (result.error != PngDecodeError::none) return result;
    result.error = decode_metadata(encoded, limits, workspace, metadata, records, result);
    return result;
}
} // namespace gui_forms::image
