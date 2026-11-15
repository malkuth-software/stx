// stx_map: the embedded file against the reference decoder's vector
// (tests/data/reference.bin, from tools/bake/make_test_data.js), the stated
// error bound against the bake's source lines (tests/data/check_lines.bin),
// layer selection and dependencies, refusals of damaged buffers, coarser
// levels, the spatial index, the projections, and decode timing as a
// workload (printed, never compared).

#include <stx_map/index.hpp>
#include <stx_map/levels.hpp>
#include <stx_map/map.hpp>
#include <stx_map/projection.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifndef STX_MAP_TEST_DATA
#error "STX_MAP_TEST_DATA names the directory holding reference.bin and check_lines.bin"
#endif

namespace {

void require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

std::vector<std::byte> read_file(const char* name) {
    const std::string path = std::string(STX_MAP_TEST_DATA) + "/" + name;
    std::FILE* const file = std::fopen(path.c_str(), "rb");
    require(file != nullptr, "a test data file opens");
    std::vector<std::byte> bytes;
    std::array<std::byte, 65536> block{};
    for (;;) {
        const std::size_t got = std::fread(block.data(), 1U, block.size(), file);
        bytes.insert(bytes.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(got));
        if (got < block.size()) break;
    }
    std::fclose(file);
    return bytes;
}

// A little-endian reader over a test data file; any read past its end fails
// the test.
struct Reader final {
    std::span<const std::byte> bytes;
    std::size_t at = 0U;
};

std::uint32_t read_u8(Reader& reader) {
    require(reader.at < reader.bytes.size(), "test data is long enough");
    const std::uint32_t value = static_cast<std::uint32_t>(reader.bytes[reader.at]);
    ++reader.at;
    return value;
}

std::uint32_t read_u32(Reader& reader) {
    std::uint32_t value = 0U;
    for (std::uint32_t b = 0U; b < 4U; ++b) value |= read_u8(reader) << (8U * b);
    return value;
}

std::int32_t read_i32(Reader& reader) {
    const std::uint32_t bits = read_u32(reader);
    const std::int32_t value = static_cast<std::int32_t>(bits);
    return value;
}

std::uint64_t read_u64(Reader& reader) {
    const std::uint64_t low = read_u32(reader);
    const std::uint64_t high = read_u32(reader);
    const std::uint64_t value = low | (high << 32U);
    return value;
}

// One decoded map: storage for every layer of the file, owned here.
struct DecodedMap final {
    stx_map::MapInfo info;
    std::vector<std::vector<std::int32_t>> x;
    std::vector<std::vector<std::int32_t>> y;
    std::vector<std::vector<std::uint32_t>> starts;
    std::vector<stx_map::LayerStorage> storage;
};

void size_storage(DecodedMap& map) {
    const std::size_t count = map.info.layer_count;
    map.x.assign(count, std::vector<std::int32_t>());
    map.y.assign(count, std::vector<std::int32_t>());
    map.starts.assign(count, std::vector<std::uint32_t>());
    map.storage.assign(count, stx_map::LayerStorage{});
    for (std::size_t i = 0U; i < count; ++i) {
        const stx_map::LayerInfo& layer = map.info.layers[i];
        map.x[i].assign(layer.vertices, 0);
        map.y[i].assign(layer.vertices, 0);
        map.starts[i].assign(static_cast<std::size_t>(layer.lines) + 1U, 0U);
        map.storage[i].x = map.x[i];
        map.storage[i].y = map.y[i];
        map.storage[i].starts = map.starts[i];
    }
}

stx_map::LayerMask all_layers(const stx_map::MapInfo& info) {
    const stx_map::LayerMask mask = static_cast<stx_map::LayerMask>((1U << info.layer_count) - 1U);
    return mask;
}

stx_map::Status decode_into(std::span<const std::byte> file, stx_map::LayerMask mask, DecodedMap& map) {
    const stx_map::DecodeLimits limits;
    const stx_map::Status inspected = stx_map::inspect_map(file, limits, map.info);
    if (inspected != stx_map::Status::ok) return inspected;
    size_storage(map);
    const stx_map::Status decoded = stx_map::decode_map(file, map.info, limits, mask, map.storage);
    return decoded;
}

stx_map::LayerView view(const DecodedMap& map, std::size_t layer) {
    const stx_map::LayerView result = stx_map::view_of(map.info.layers[layer], map.storage[layer]);
    return result;
}

std::size_t layer_index(const stx_map::MapInfo& info, std::string_view name) {
    for (std::size_t i = 0U; i < info.layer_count; ++i) {
        if (stx_map::layer_name(info.layers[i]) == name) return i;
    }
    require(false, "the named layer exists");
    return 0U;
}

// FNV-1a 64 over starts, then x/y pairs, each value as four little-endian
// bytes (make_test_data.js).
struct Fnv final {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
};

void feed(Fnv& fnv, std::uint32_t value) {
    for (std::uint32_t b = 0U; b < 4U; ++b) {
        fnv.hash ^= (value >> (8U * b)) & 0xFFU;
        fnv.hash *= 0x100000001b3ULL;
    }
}

constexpr const char* borders_name = "borders_admin0_ne_default";

// -- the reference vector ----------------------------------------------------

void test_reference() {
    DecodedMap map;
    const std::span<const std::byte> file = stx_map::embedded_world_map();
    stx_map::MapInfo probe;
    require(stx_map::inspect_map(file, stx_map::DecodeLimits{}, probe) == stx_map::Status::ok, "the embedded map inspects");
    require(decode_into(file, all_layers(probe), map) == stx_map::Status::ok, "the embedded map decodes");
    require(map.info.units_per_degree == stx_map::units_per_degree, "units per degree");
    const std::vector<std::byte> bytes = read_file("reference.bin");
    Reader reader;
    reader.bytes = bytes;
    const char magic[] = "STXMAPRF";
    for (std::size_t i = 0U; i < 8U; ++i) require(read_u8(reader) == static_cast<std::uint32_t>(magic[i]), "reference magic");
    require(read_u32(reader) == map.info.layer_count, "reference layer count");
    for (std::size_t i = 0U; i < map.info.layer_count; ++i) {
        const std::uint32_t name_length = read_u8(reader);
        std::string name;
        for (std::uint32_t c = 0U; c < name_length; ++c) name.push_back(static_cast<char>(read_u8(reader)));
        const stx_map::LayerInfo& layer = map.info.layers[i];
        require(stx_map::layer_name(layer) == name, "reference layer name");
        require(read_u8(reader) == static_cast<std::uint32_t>(layer.kind), "reference layer kind");
        require(read_u32(reader) == layer.lines, "reference line count");
        require(read_u32(reader) == layer.vertices, "reference vertex count");
        Fnv fnv;
        for (std::uint32_t k = 0U; k <= layer.lines; ++k) feed(fnv, map.starts[i][k]);
        for (std::uint32_t v = 0U; v < layer.vertices; ++v) {
            feed(fnv, static_cast<std::uint32_t>(map.x[i][v]));
            feed(fnv, static_cast<std::uint32_t>(map.y[i][v]));
        }
        require(read_u64(reader) == fnv.hash, "reference positions (FNV-1a of starts and positions)");
        const std::uint32_t samples = read_u32(reader);
        require(samples == (layer.vertices + 15U) / 16U, "reference sample count");
        for (std::uint32_t s = 0U; s < samples; ++s) {
            require(read_i32(reader) == map.x[i][s * 16U], "reference sample x");
            require(read_i32(reader) == map.y[i][s * 16U], "reference sample y");
        }
    }
    require(reader.at == bytes.size(), "the reference vector is used whole");
}

// -- layer selection -----------------------------------------------------------

void test_layers() {
    const std::span<const std::byte> file = stx_map::embedded_world_map();
    DecodedMap all;
    stx_map::MapInfo probe;
    require(stx_map::inspect_map(file, stx_map::DecodeLimits{}, probe) == stx_map::Status::ok, "inspect");
    require(decode_into(file, all_layers(probe), all) == stx_map::Status::ok, "decode all");
    const std::size_t land = layer_index(all.info, "land");
    const std::size_t lakes = layer_index(all.info, "lakes");
    const std::size_t borders = layer_index(all.info, borders_name);
    require(all.info.layers[land].kind == stx_map::LayerKind::rings, "land is rings");
    require(all.info.layers[lakes].kind == stx_map::LayerKind::rings, "lakes are rings");
    require(all.info.layers[borders].kind == stx_map::LayerKind::lines, "borders are lines");
    require(all.info.layers[borders].optional && !all.info.layers[land].optional, "only borders are optional");
    const stx_map::LayerMask defaults = stx_map::default_layers(all.info);
    require(((defaults >> borders) & 1U) == 0U && ((defaults >> land) & 1U) == 1U, "default layers");
    require(stx_map::layer_named(all.info, "nothing") == 0U, "an unknown name names no layer");
    const stx_map::LayerMask asked = stx_map::layer_named(all.info, borders_name);
    const stx_map::LayerMask closed = stx_map::with_dependencies(all.info, asked);
    require(((closed >> land) & 1U) == 1U, "borders depend on land (ties onto the coast)");
    // A mask without its dependencies is refused.
    DecodedMap partial;
    require(decode_into(file, asked, partial) == stx_map::Status::invalid_argument, "an open mask is refused");
    // The default layers alone decode to the same positions.
    DecodedMap plain;
    require(decode_into(file, defaults, plain) == stx_map::Status::ok, "decode defaults");
    require(plain.x[land] == all.x[land] && plain.y[land] == all.y[land] && plain.starts[land] == all.starts[land],
            "land is the same with or without borders");
    require(all.info.layers[borders].ties > 0U, "the borders carry ties");
}

// -- the error bound -------------------------------------------------------------

struct Point final {
    double x = 0.0;
    double y = 0.0;
};

double segment_distance(Point p, Point a, Point b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length = dx * dx + dy * dy;
    double t = length == 0.0 ? 0.0 : ((p.x - a.x) * dx + (p.y - a.y) * dy) / length;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const double ex = p.x - (a.x + t * dx);
    const double ey = p.y - (a.y + t * dy);
    const double distance = std::sqrt(ex * ex + ey * ey);
    return distance;
}

constexpr double bucket_units = 1024.0; // above the tolerance, so a near segment is in the probe's bucket
constexpr double probe_step_units = 128.0;

std::int64_t bucket_key(double x, double y) {
    const std::int64_t column = static_cast<std::int64_t>(std::floor(x / bucket_units));
    const std::int64_t row = static_cast<std::int64_t>(std::floor(y / bucket_units));
    const std::int64_t key = column * 1000003LL + row;
    return key;
}

// The largest distance from a point of `from` (its vertices and points every
// probe_step_units along its segments) to polyline `to`; a distance beyond
// bucket_units is reported as bucket_units.
double directed_distance(const std::vector<Point>& from, const std::vector<Point>& to) {
    std::unordered_map<std::int64_t, std::vector<std::size_t>> buckets;
    for (std::size_t i = 0U; i + 1U < to.size(); ++i) {
        const double x0 = std::floor(std::fmin(to[i].x, to[i + 1U].x) / bucket_units) - 1.0;
        const double x1 = std::floor(std::fmax(to[i].x, to[i + 1U].x) / bucket_units) + 1.0;
        const double y0 = std::floor(std::fmin(to[i].y, to[i + 1U].y) / bucket_units) - 1.0;
        const double y1 = std::floor(std::fmax(to[i].y, to[i + 1U].y) / bucket_units) + 1.0;
        for (double bx = x0; bx <= x1; bx += 1.0) {
            for (double by = y0; by <= y1; by += 1.0) buckets[bucket_key(bx * bucket_units, by * bucket_units)].push_back(i);
        }
    }
    if (to.size() == 1U) buckets[bucket_key(to[0].x, to[0].y)].push_back(0U);
    double worst = 0.0;
    for (std::size_t i = 0U; i < from.size(); ++i) {
        const std::size_t steps = i + 1U < from.size()
            ? static_cast<std::size_t>(std::floor(std::hypot(from[i + 1U].x - from[i].x, from[i + 1U].y - from[i].y) / probe_step_units))
            : 0U;
        for (std::size_t s = 0U; s <= steps; ++s) {
            Point probe = from[i];
            if (s > 0U) {
                const double share = static_cast<double>(s) / static_cast<double>(steps + 1U);
                probe.x = from[i].x + (from[i + 1U].x - from[i].x) * share;
                probe.y = from[i].y + (from[i + 1U].y - from[i].y) * share;
            }
            double best = bucket_units;
            const std::unordered_map<std::int64_t, std::vector<std::size_t>>::const_iterator found =
                buckets.find(bucket_key(probe.x, probe.y));
            if (found != buckets.end()) {
                for (const std::size_t segment : (*found).second) {
                    const std::size_t next = to.size() == 1U ? 0U : segment + 1U;
                    const double distance = segment_distance(probe, to[segment], to[next]);
                    if (distance < best) best = distance;
                }
            }
            if (best > worst) worst = best;
        }
    }
    return worst;
}

void close_ring(std::vector<Point>& line) {
    const Point first = line.front();
    const Point last = line.back();
    if (first.x != last.x || first.y != last.y) line.push_back(first);
}

void test_error_bound() {
    DecodedMap map;
    const std::span<const std::byte> file = stx_map::embedded_world_map();
    stx_map::MapInfo probe;
    require(stx_map::inspect_map(file, stx_map::DecodeLimits{}, probe) == stx_map::Status::ok, "inspect");
    require(decode_into(file, all_layers(probe), map) == stx_map::Status::ok, "decode all");
    const std::vector<std::byte> bytes = read_file("check_lines.bin");
    Reader reader;
    reader.bytes = bytes;
    const char magic[] = "STXMAPCK";
    for (std::size_t i = 0U; i < 8U; ++i) require(read_u8(reader) == static_cast<std::uint32_t>(magic[i]), "check magic");
    const std::uint32_t tolerance = read_u32(reader);
    require(tolerance == map.info.tolerance_units, "the check data states the file's tolerance");
    const std::uint32_t entries = read_u32(reader);
    require(entries > 0U, "the check data has lines");
    double worst = 0.0;
    for (std::uint32_t e = 0U; e < entries; ++e) {
        const std::uint32_t layer = read_u8(reader);
        const std::uint32_t kind = read_u8(reader);
        const std::uint32_t line = read_u32(reader);
        const std::uint32_t count = read_u32(reader);
        require(layer < map.info.layer_count && line < map.info.layers[layer].lines, "check entry names a line");
        require(kind == static_cast<std::uint32_t>(map.info.layers[layer].kind), "check entry kind");
        std::vector<Point> source(count);
        for (std::uint32_t v = 0U; v < count; ++v) {
            source[v].x = static_cast<double>(read_i32(reader));
            source[v].y = static_cast<double>(read_i32(reader));
        }
        std::vector<Point> decoded;
        for (std::uint32_t v = map.starts[layer][line]; v < map.starts[layer][line + 1U]; ++v) {
            Point point;
            point.x = static_cast<double>(map.x[layer][v]);
            point.y = static_cast<double>(map.y[layer][v]);
            decoded.push_back(point);
        }
        if (kind == 0U) {
            close_ring(source);
            close_ring(decoded);
        }
        const double forward = directed_distance(source, decoded);
        const double backward = directed_distance(decoded, source);
        if (forward > worst) worst = forward;
        if (backward > worst) worst = backward;
    }
    require(reader.at == bytes.size(), "the check data is used whole");
    // The source is quantised to units here (half a unit each way at most).
    std::printf("stx_map: checked %u lines, worst symmetric Hausdorff %.1f units (tolerance %u)\n", entries, worst,
                tolerance);
    require(worst <= static_cast<double>(tolerance) + 1.0, "every checked line lies within the stated tolerance");
}

// -- refusals --------------------------------------------------------------------

void repair_check_value(std::vector<std::byte>& file) {
    const std::size_t end = file.size() - 4U;
    const std::uint32_t crc = stx_map::crc32(std::span<const std::byte>(file.data(), end));
    for (std::size_t b = 0U; b < 4U; ++b) file[end + b] = static_cast<std::byte>((crc >> (8U * b)) & 0xFFU);
}

stx_map::Status inspect_copy(const std::vector<std::byte>& file) {
    stx_map::MapInfo info;
    const stx_map::Status status = stx_map::inspect_map(file, stx_map::DecodeLimits{}, info);
    return status;
}

void put_u32(std::vector<std::byte>& file, std::size_t at, std::uint32_t value) {
    for (std::size_t b = 0U; b < 4U; ++b) file[at + b] = static_cast<std::byte>((value >> (8U * b)) & 0xFFU);
}

void test_refusals() {
    const std::span<const std::byte> embedded = stx_map::embedded_world_map();
    const std::vector<std::byte> original(embedded.begin(), embedded.end());
    stx_map::MapInfo info;
    require(stx_map::inspect_map(original, stx_map::DecodeLimits{}, info) == stx_map::Status::ok, "inspect");

    require(inspect_copy(std::vector<std::byte>()) == stx_map::Status::not_tmap, "an empty buffer");
    std::vector<std::byte> version = original;
    version[4] = std::byte{3};
    require(inspect_copy(version) == stx_map::Status::not_tmap, "version 3");
    for (std::size_t cut = 1U; cut < original.size(); cut += 997U) {
        const std::vector<std::byte> truncated(original.begin(), original.end() - static_cast<std::ptrdiff_t>(cut));
        const stx_map::Status status = inspect_copy(truncated);
        require(status == stx_map::Status::bad_check_value || status == stx_map::Status::not_tmap, "a truncated file");
    }
    for (std::size_t at = 0U; at + 4U < original.size(); at += 331U) {
        std::vector<std::byte> flipped = original;
        flipped[at] ^= std::byte{0x10};
        const stx_map::Status status = inspect_copy(flipped);
        require(status == stx_map::Status::bad_check_value || status == stx_map::Status::not_tmap, "a flipped bit");
    }
    std::vector<std::byte> bins = original;
    bins[8] = std::byte{13};
    repair_check_value(bins);
    require(inspect_copy(bins) == stx_map::Status::bad_header, "13-bit turn bins");
    std::vector<std::byte> mantissa = original;
    mantissa[5] = std::byte{5};
    repair_check_value(mantissa);
    require(inspect_copy(mantissa) == stx_map::Status::bad_header, "five mantissa bits");
    std::vector<std::byte> layers = original;
    layers[32] = std::byte{0};
    repair_check_value(layers);
    require(inspect_copy(layers) == stx_map::Status::bad_header, "no layers");
    std::vector<std::byte> name = original;
    name[36] = std::byte{'A'};
    repair_check_value(name);
    require(inspect_copy(name) == stx_map::Status::bad_header, "a layer name outside [a-z0-9_]");
    // The first layer's record: kind 33, flags 34, name length 35, name 36..,
    // then grid, depends, lines, vertices, ties, body bytes.
    const std::size_t record = 36U + info.layers[0].name_length;
    std::vector<std::byte> depends = original;
    depends[record + 2U] = std::byte{1};
    repair_check_value(depends);
    require(inspect_copy(depends) == stx_map::Status::bad_header, "a layer depending on itself");
    std::vector<std::byte> vertices = original;
    put_u32(vertices, record + 8U, 1U << 23U);
    repair_check_value(vertices);
    require(inspect_copy(vertices) == stx_map::Status::bad_counts, "more vertices than the limit");
    std::vector<std::byte> ties = original;
    put_u32(ties, record + 12U, info.layers[0].lines * 2U + 1U);
    repair_check_value(ties);
    require(inspect_copy(ties) == stx_map::Status::bad_counts, "more ties than line ends");
    std::vector<std::byte> body = original;
    put_u32(body, record + 16U, info.layers[0].body_bytes + 1U);
    repair_check_value(body);
    require(inspect_copy(body) == stx_map::Status::bad_header, "a body past the file");
    stx_map::DecodeLimits small;
    small.vertices = info.layers[0].vertices - 1U;
    stx_map::MapInfo limited;
    require(stx_map::inspect_map(original, small, limited) == stx_map::Status::bad_counts, "the caller's vertex limit");

    // Caller storage and arguments.
    DecodedMap map;
    map.info = info;
    size_storage(map);
    const stx_map::LayerMask mask = stx_map::default_layers(info);
    map.storage[0].x = map.storage[0].x.first(map.storage[0].x.size() - 1U);
    require(stx_map::decode_map(original, info, stx_map::DecodeLimits{}, mask, map.storage) ==
                stx_map::Status::storage_too_small, "short storage");
    size_storage(map);
    require(stx_map::decode_map(original, info, stx_map::DecodeLimits{}, mask,
                                std::span<const stx_map::LayerStorage>(map.storage.data(), 1U)) ==
                stx_map::Status::invalid_argument, "storage for too few layers");
    stx_map::MapInfo other = info;
    other.tolerance_units += 1U;
    require(stx_map::decode_map(original, other, stx_map::DecodeLimits{}, mask, map.storage) ==
                stx_map::Status::invalid_argument, "an info from another file");
    require(stx_map::decode_map(original, info, stx_map::DecodeLimits{}, static_cast<stx_map::LayerMask>(1U << 15U),
                                map.storage) == stx_map::Status::invalid_argument, "a layer the file lacks");

    // Body bytes changed with the check value repaired: every decode either
    // refuses or succeeds, never reads out of bounds (the sanitizer runs
    // see to that) and never writes past the counts.
    const stx_map::LayerMask everything = all_layers(info);
    const std::uint32_t first_body = info.layers[0].body_offset;
    for (std::size_t at = first_body; at + 4U < original.size(); at += 113U) {
        std::vector<std::byte> damaged = original;
        damaged[at] ^= std::byte{0x5A};
        repair_check_value(damaged);
        DecodedMap result;
        const stx_map::Status status = decode_into(damaged, everything, result);
        const bool known = status == stx_map::Status::ok || status == stx_map::Status::bad_body ||
            status == stx_map::Status::bad_tie;
        require(known, "a damaged body is decoded or refused as a body or tie");
    }
}

// -- coarser levels --------------------------------------------------------------------

void test_levels() {
    // A line rising to a peak: at tolerance 100 the peak stays and the
    // points near the chords to it go; at 1000 only the ends remain.
    const std::array<std::int32_t, 5> x{0, 1000, 2000, 3000, 4000};
    const std::array<std::int32_t, 5> y{0, 260, 500, 260, 0};
    const std::array<std::uint32_t, 2> starts{0U, 5U};
    stx_map::LayerView line;
    line.kind = stx_map::LayerKind::lines;
    line.x = x;
    line.y = y;
    line.starts = starts;
    std::array<std::uint8_t, 5> keep{};
    std::vector<std::uint32_t> stack(stx_map::simplify_stack_entries(line) * 2U);
    stx_map::SimplifyScratch scratch;
    scratch.keep = keep;
    scratch.stack = stack;
    std::array<std::int32_t, 5> out_x{};
    std::array<std::int32_t, 5> out_y{};
    std::array<std::uint32_t, 2> out_starts{};
    stx_map::LayerStorage output;
    output.x = out_x;
    output.y = out_y;
    output.starts = out_starts;
    stx_map::SimplifiedCounts counts;
    require(stx_map::simplify_layer(line, 100U, scratch, output, counts) == stx_map::Status::ok, "simplify a bump");
    require(counts.lines == 1U && counts.vertices == 3U, "the bump's peak stays, its shoulders go");
    require(out_x[0] == 0 && out_x[1] == 2000 && out_x[2] == 4000, "ends and peak kept in order");
    require(stx_map::simplify_layer(line, 1000U, scratch, output, counts) == stx_map::Status::ok, "simplify flat");
    require(counts.vertices == 2U, "below the tolerance only the ends remain");
    require(stx_map::simplify_layer(line, 0U, scratch, output, counts) == stx_map::Status::invalid_argument,
            "a zero tolerance");
    scratch.stack = std::span<std::uint32_t>(stack.data(), 1U);
    require(stx_map::simplify_layer(line, 100U, scratch, output, counts) == stx_map::Status::storage_too_small,
            "a short stack");

    // The embedded layers at four times the tolerance: fewer points, ends of
    // border lines kept, every kept point an input point of its line.
    DecodedMap map;
    const std::span<const std::byte> file = stx_map::embedded_world_map();
    stx_map::MapInfo probe;
    require(stx_map::inspect_map(file, stx_map::DecodeLimits{}, probe) == stx_map::Status::ok, "inspect");
    require(decode_into(file, all_layers(probe), map) == stx_map::Status::ok, "decode all");
    for (std::size_t i = 0U; i < map.info.layer_count; ++i) {
        const stx_map::LayerView input = view(map, i);
        std::vector<std::uint8_t> layer_keep(input.x.size());
        std::vector<std::uint32_t> layer_stack(stx_map::simplify_stack_entries(input) * 2U);
        std::vector<std::int32_t> level_x(input.x.size());
        std::vector<std::int32_t> level_y(input.x.size());
        std::vector<std::uint32_t> level_starts(stx_map::line_count(input) + 1U);
        stx_map::SimplifyScratch layer_scratch;
        layer_scratch.keep = layer_keep;
        layer_scratch.stack = layer_stack;
        stx_map::LayerStorage level;
        level.x = level_x;
        level.y = level_y;
        level.starts = level_starts;
        stx_map::SimplifiedCounts level_counts;
        require(stx_map::simplify_layer(input, map.info.tolerance_units * 4U, layer_scratch, level, level_counts) ==
                    stx_map::Status::ok, "simplify an embedded layer");
        require(level_counts.vertices < input.x.size(), "a coarser level has fewer points");
        require(level_counts.lines <= stx_map::line_count(input), "a coarser level has no more lines");
        if (input.kind == stx_map::LayerKind::lines) {
            require(level_counts.lines == stx_map::line_count(input), "border lines are never dropped");
            for (std::size_t k = 0U; k < level_counts.lines; ++k) {
                const std::uint32_t first = input.starts[k];
                const std::uint32_t last = input.starts[k + 1U] - 1U;
                require(level_x[level_starts[k]] == input.x[first] && level_y[level_starts[k]] == input.y[first],
                        "a border keeps its first end");
                require(level_x[level_starts[k + 1U] - 1U] == input.x[last] &&
                            level_y[level_starts[k + 1U] - 1U] == input.y[last], "a border keeps its last end");
            }
        }
    }
}

// -- the spatial index -----------------------------------------------------------------

void test_index() {
    DecodedMap map;
    const std::span<const std::byte> file = stx_map::embedded_world_map();
    stx_map::MapInfo probe;
    require(stx_map::inspect_map(file, stx_map::DecodeLimits{}, probe) == stx_map::Status::ok, "inspect");
    require(decode_into(file, stx_map::default_layers(probe), map) == stx_map::Status::ok, "decode defaults");
    const stx_map::LayerView land = view(map, layer_index(map.info, "land"));
    const stx_map::GridShape shape;
    const std::size_t lines = stx_map::line_count(land);
    std::vector<stx_map::Box> boxes(lines);
    std::vector<std::uint32_t> cell_starts(static_cast<std::size_t>(shape.columns) * shape.rows + 1U);
    std::vector<std::uint32_t> entries(stx_map::index_entry_count(land, shape));
    require(!entries.empty(), "the index has entries");
    stx_map::IndexStorage storage;
    storage.boxes = boxes;
    storage.cell_starts = cell_starts;
    storage.entries = std::span<std::uint32_t>(entries.data(), entries.size() - 1U);
    require(stx_map::build_index(land, shape, storage) == stx_map::Status::storage_too_small, "short entries");
    storage.entries = entries;
    require(stx_map::build_index(land, shape, storage) == stx_map::Status::ok, "build the index");
    std::vector<std::uint32_t> found(lines);
    std::vector<std::uint8_t> seen(lines);
    // Boxes across the world: Europe, the antimeridian edge, a single point,
    // the whole world.
    const std::int32_t d = static_cast<std::int32_t>(stx_map::units_per_degree);
    const std::array<stx_map::Box, 4> queries{
        stx_map::Box{170 * d, 120 * d, 220 * d, 160 * d},
        stx_map::Box{0, 0, 2 * d, 180 * d},
        stx_map::Box{180 * d, 141 * d, 180 * d, 141 * d},
        stx_map::Box{0, 0, 360 * d, 180 * d}};
    for (const stx_map::Box& query : queries) {
        const std::size_t count = stx_map::query_index(shape, storage, query, found);
        require(count <= lines, "a query reports each line at most once");
        for (std::size_t k = 0U; k < lines; ++k) seen[k] = 0U;
        for (std::size_t k = 0U; k < count; ++k) {
            require(seen[found[k]] == 0U, "no line is reported twice");
            seen[found[k]] = 1U;
        }
        for (std::size_t k = 0U; k < lines; ++k) {
            const stx_map::Box& box = boxes[k];
            const bool meets = box.min_x <= query.max_x && query.min_x <= box.max_x && box.min_y <= query.max_y &&
                query.min_y <= box.max_y;
            require(meets == (seen[k] == 1U), "a query reports exactly the lines whose boxes meet it");
        }
    }
    const std::size_t world = stx_map::query_index(shape, storage, queries[3], std::span<std::uint32_t>());
    require(world == lines, "a query into no storage still counts");
}

// -- projections ---------------------------------------------------------------------

void test_projection() {
    const std::int32_t d = static_cast<std::int32_t>(stx_map::units_per_degree);
    const stx_map::WorldPoint corner = stx_map::project(stx_map::Projection::equirectangular, 0, 180 * d);
    require(corner.x == 0.0 && corner.y == 0.0, "equirectangular north-west corner");
    const stx_map::WorldPoint centre = stx_map::project(stx_map::Projection::equirectangular, 180 * d, 90 * d);
    require(centre.x == 0.5 && centre.y == 0.5, "equirectangular centre");
    const stx_map::WorldPoint equator = stx_map::project(stx_map::Projection::web_mercator, 90 * d, 90 * d);
    require(equator.x == 0.25 && std::fabs(equator.y - 0.5) < 1e-12, "Web Mercator equator");
    const stx_map::WorldPoint north = stx_map::project(stx_map::Projection::web_mercator, 0, 180 * d);
    require(std::fabs(north.y) < 1e-6, "Web Mercator clamps the pole to the top edge");
    double previous = 2.0;
    for (std::int32_t latitude = -80; latitude <= 80; latitude += 10) {
        const stx_map::WorldPoint point = stx_map::project(stx_map::Projection::web_mercator, 0, (latitude + 90) * d);
        require(point.y < previous, "Web Mercator y falls northward");
        previous = point.y;
    }
}

// -- workload ---------------------------------------------------------------------------

void report_decode_workload() {
    const std::span<const std::byte> file = stx_map::embedded_world_map();
    DecodedMap map;
    stx_map::MapInfo probe;
    require(stx_map::inspect_map(file, stx_map::DecodeLimits{}, probe) == stx_map::Status::ok, "inspect");
    require(decode_into(file, all_layers(probe), map) == stx_map::Status::ok, "decode all");
    constexpr std::uint32_t repeats = 20U;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (std::uint32_t r = 0U; r < repeats; ++r) {
        const stx_map::Status status = stx_map::decode_map(file, map.info, stx_map::DecodeLimits{}, all_layers(map.info),
                                                           map.storage);
        require(status == stx_map::Status::ok, "repeated decode");
    }
    const std::chrono::steady_clock::time_point stop = std::chrono::steady_clock::now();
    const std::chrono::nanoseconds elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start);
    std::uint64_t vertices = 0U;
    for (std::size_t i = 0U; i < map.info.layer_count; ++i) vertices += map.info.layers[i].vertices;
    // A workload record, not a claim: this build's flags and this machine.
    std::printf("stx_map workload: decode all layers (%zu bytes, %llu vertices) x %u: %lld ns each\n", file.size(),
                static_cast<unsigned long long>(vertices), repeats,
                static_cast<long long>(elapsed.count() / static_cast<long long>(repeats)));
}

} // namespace

int main() {
    test_reference();
    test_layers();
    test_error_bound();
    test_refusals();
    test_levels();
    test_index();
    test_projection();
    report_decode_workload();
    std::puts("stx_map: all tests passed");
    return 0;
}
